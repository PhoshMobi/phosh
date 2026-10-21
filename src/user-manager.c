/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-user-manager"

#include "phosh-config.h"

#include "user-manager.h"
#include "util.h"

#include <json-glib/json-glib.h>

#include <gio/gio.h>

/* JSON user record */
#define UNLOCK_INFO_OBJ "mobi.phosh.unlockInfo"
#define PIN_LENGTH_ENTRY "pinLength"
/* DBus object */
#define USER_RECORD_DBUS_PROP "UserRecord"

/**
 * PhoshUserManager:
 *
 * Current user backed by systemd-homed
 */

enum {
  PROP_0,
  PROP_USERNAME,
  PROP_PIN_LENGTH,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

struct _PhoshUserManager {
  GObject       parent;

  GDBusProxy   *homed_proxy;
  GCancellable *cancel;
  JsonParser   *json_parser;

  char         *username;
  int pin_length;
};
G_DEFINE_TYPE (PhoshUserManager, phosh_user_manager, G_TYPE_OBJECT)


static void
on_user_record_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (user_data);
  g_autoptr (GVariant) reply = NULL, prop = NULL;
  g_autoptr (GError) err = NULL;
  const char *record;
  gboolean incomplete;
  JsonNode *node;
  JsonObject *root, *privileged, *unlock_info;
  int length = 0;

  reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &err);
  if (!reply) {
    g_debug ("Can't get property: %s, is systemd-homed running?", err->message);
    goto out;
  }

  g_variant_get (reply, "(v)", &prop);
  if (!prop) {
    g_warning ("systemd-homed didn't return a property");
    goto out;
  }

  g_variant_get (prop, "(&sb)", &record, &incomplete);
  if (incomplete) {
    g_warning ("systemd-homed returned an incomplete user record for %s", self->username);
    goto out;
  }

  if (self->json_parser == NULL)
    self->json_parser = json_parser_new ();

  if (!json_parser_load_from_data (self->json_parser, record, -1, &err)) {
    g_warning ("Failed to parse user JSON: %s", err->message);
    goto out;
  }

  node = json_parser_get_root (self->json_parser);
  if (!JSON_NODE_HOLDS_OBJECT (node)) {
    g_warning ("Not a JSON object");
    goto out;
  }
  root = json_node_get_object (node);

  if (!json_object_has_member (root, "privileged")) {
    g_warning ("User record does not contain a privileged section");
    goto out;
  }

  node = json_object_get_member (root, "privileged");
  if (!node || !JSON_NODE_HOLDS_OBJECT (node)) {
    g_warning ("User record does not contain a valid privileged section");
    goto out;
  }
  privileged = json_node_get_object (node);

  node = json_object_get_member (privileged, UNLOCK_INFO_OBJ);
  if (!node) {
    g_debug ("User record does not contain an unlock info section");
    goto out;
  }
  if (!JSON_NODE_HOLDS_OBJECT (node)) {
    g_warning ("User record does not contain a valid unlock info section");
    goto out;
  }
  unlock_info = json_node_get_object (node);

  if (json_object_has_member (unlock_info, PIN_LENGTH_ENTRY)) {
    node = json_object_get_member (unlock_info, PIN_LENGTH_ENTRY);
    length = json_node_get_int (node);
    g_debug ("User record specifies pin length");
  }

 out:
  if (self->pin_length != length) {
    self->pin_length = length;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PIN_LENGTH]);
  }
}


static void
update_user_record (PhoshUserManager *self)
{
  g_debug ("Refreshing properties of user '%s'", self->username);

  /* UserRecord is not refreshed on changes so we need to refetch it */
  g_dbus_connection_call (g_dbus_proxy_get_connection (self->homed_proxy),
                          g_dbus_proxy_get_name (self->homed_proxy),
                          g_dbus_proxy_get_object_path (self->homed_proxy),
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         g_dbus_proxy_get_interface_name (self->homed_proxy),
                                         USER_RECORD_DBUS_PROP),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          self->cancel,
                          on_user_record_ready,
                          self);
}


static void
on_user_record_changed (PhoshUserManager *self, GVariant *changed, char **invalidated)
{
  g_debug ("Refreshing user record for '%s'", self->username);

  if (!g_strv_contains ((const char * const*)invalidated, "UserRecord")) {
    g_debug ("Ignoring homed record property change for '%s'", self->username);
    return;
  }

  update_user_record (self);
}


static void
on_homed_proxy_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (user_data);
  g_autoptr (GError) err = NULL;

  self->homed_proxy = g_dbus_proxy_new_for_bus_finish (res, &err);
  if (self->homed_proxy == NULL) {
    phosh_dbus_service_error_warn (err, "Failed to connect to systemd-homed");
    return;
  }

  g_debug ("Got homed proxy for user '%s'", self->username);
  g_signal_connect_object (self->homed_proxy,
                           "g-properties-changed",
                           G_CALLBACK (on_user_record_changed),
                           self,
                           G_CONNECT_SWAPPED);

  /* Initial sync of properties */
  update_user_record (self);
}


static void
set_username (PhoshUserManager *self, const char *username)
{
  g_autofree char *object_path = NULL;
  g_autofree char *escaped = NULL;

  g_set_str (&self->username, username);

  if (self->username == NULL) {
    g_warning ("Can't get empty user");
    return;
  }

  escaped = g_dbus_escape_object_path (username);
  object_path = g_strdup_printf ("/org/freedesktop/home1/home/%s", escaped);
  g_debug ("Listening for user changes at '%s'", object_path);

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                            G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                            NULL,
                            "org.freedesktop.home1",
                            object_path,
                            "org.freedesktop.home1.Home",
                            self->cancel,
                            on_homed_proxy_ready,
                            self);
}


static void
phosh_user_manager_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (object);

  switch (property_id) {
  case PROP_USERNAME:
    set_username (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_user_manager_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (object);

  switch (property_id) {
  case PROP_USERNAME:
    g_value_set_string (value, self->username);
    break;
  case PROP_PIN_LENGTH:
    g_value_set_int (value, self->pin_length);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_user_manager_dispose (GObject *object)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  g_clear_object (&self->homed_proxy);
  g_clear_object (&self->json_parser);

  G_OBJECT_CLASS (phosh_user_manager_parent_class)->dispose (object);
}


static void
phosh_user_manager_finalize (GObject *object)
{
  PhoshUserManager *self = PHOSH_USER_MANAGER (object);

  g_clear_pointer (&self->username, g_free);

  G_OBJECT_CLASS (phosh_user_manager_parent_class)->finalize (object);
}


static void
phosh_user_manager_class_init (PhoshUserManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_user_manager_get_property;
  object_class->set_property = phosh_user_manager_set_property;
  object_class->dispose = phosh_user_manager_dispose;
  object_class->finalize = phosh_user_manager_finalize;

  props[PROP_USERNAME] =
    g_param_spec_string ("username", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_PIN_LENGTH] =
    g_param_spec_int ("pin-length", "", "",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_user_manager_init (PhoshUserManager *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshUserManager *
phosh_user_manager_new (const char *username)
{
  return g_object_new (PHOSH_TYPE_USER_MANAGER,
                       "username", username,
                       NULL);
}
