/*
 * Copyright (C) 2020 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-location-manager"

#include "phosh-config.h"

#include "app-auth-prompt.h"
#include "geoclue-manager-dbus.h"
#include "location-manager.h"
#include "shell-priv.h"
#include "util.h"

#include <gdesktop-enums.h>
#include <gio/gdesktopappinfo.h>

/**
 * PhoshLocationManager:
 *
 * Provides the org.freedesktop.GeoClue2.Agent DBus interface
 *
 * The #PhoshLocationManager provides the agent interface and authorizes
 * clients based on the org.gnome.system.location 'enabled' gsetting. Note
 * the phosh needs to be enabled as agent in geoclue's config.
 */
#define LOCATION_AGENT_DBUS_NAME  "org.freedesktop.GeoClue2.Agent"
#define LOCATION_AGENT_DBUS_PATH  "/org/freedesktop/GeoClue2/Agent"

#define GEOCLUE_SERVICE           "org.freedesktop.GeoClue2"
#define GEOCLUE_MANAGER_PATH      "/org/freedesktop/GeoClue2/Manager"

/**
 * AccuracyLevel:
 * @LEVEL_NONE: Accuracy level unknown or unset.
 * @LEVEL_COUNTRY: Country-level accuracy.
 * @LEVEL_CITY: City-level accuracy.
 * @LEVEL_NEIGHBORHOOD: neighborhood-level accuracy.
 * @LEVEL_STREET: Street-level accuracy.
 * @LEVEL_EXACT: Exact accuracy. Typically requires GPS receiver.
 *
 * Used to specify level of accuracy requested by, or allowed for a client.
 **/
typedef enum {
  LEVEL_NONE = 0,
  LEVEL_COUNTRY = 1,
  LEVEL_CITY = 4,
  LEVEL_NEIGHBORHOOD = 5,
  LEVEL_STREET = 6,
  LEVEL_EXACT = 8,
} AccuracyLevel;


enum {
  PROP_0,
  PROP_ENABLED,
  PROP_ACTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PhoshLocationManager {
  PhoshDBusGeoClue2AgentSkeleton parent;

  PhoshDBusGeoClue2Manager      *manager_proxy;
  guint                          dbus_name_id;
  GSettings                     *location_settings;
  gboolean                       enabled;
  gboolean                       active;
  GDBusConnection               *connection;

  /* Current Request */
  GtkWidget                     *prompt;
  GDBusMethodInvocation         *invocation;
  AccuracyLevel                  req_level;

  GCancellable                  *cancel;
} PhoshLocationManager;

static void phosh_location_manager_geoclue2_agent_iface_init (PhoshDBusGeoClue2AgentIface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshLocationManager,
                         phosh_location_manager,
                         PHOSH_DBUS_TYPE_GEO_CLUE2_AGENT_SKELETON,
                         G_IMPLEMENT_INTERFACE (PHOSH_DBUS_TYPE_GEO_CLUE2_AGENT,
                                                phosh_location_manager_geoclue2_agent_iface_init));


static guint
get_max_level (PhoshLocationManager *self)
{
  gint level = LEVEL_NONE;

  if (self->enabled) {
    GDesktopLocationAccuracyLevel val;

    val = g_settings_get_enum (self->location_settings, "max-accuracy-level");
    switch (val) {
    case G_DESKTOP_LOCATION_ACCURACY_LEVEL_COUNTRY:
      level = LEVEL_COUNTRY;
      break;
    case G_DESKTOP_LOCATION_ACCURACY_LEVEL_CITY:
      level = LEVEL_CITY;
      break;
    case G_DESKTOP_LOCATION_ACCURACY_LEVEL_NEIGHBORHOOD:
      level = LEVEL_NEIGHBORHOOD;
      break;
    case G_DESKTOP_LOCATION_ACCURACY_LEVEL_STREET:
      level = LEVEL_STREET;
      break;
    case G_DESKTOP_LOCATION_ACCURACY_LEVEL_EXACT:
      level = LEVEL_EXACT;
      break;
    default:
      g_warn_if_reached ();
    }
  }

  return level;
}


static void
update_accuracy_level (PhoshLocationManager *self, gboolean enabled)
{
  int level;

  self->enabled = enabled;
  level = get_max_level (self);

  g_debug ("Setting accuracy level to %d", level);
  phosh_dbus_geo_clue2_agent_set_max_accuracy_level (
    PHOSH_DBUS_GEO_CLUE2_AGENT (self), level);
}


static void
phosh_location_manager_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);

  switch (property_id) {
  case PROP_ENABLED:
    g_value_set_boolean (value, self->enabled);
    break;
  case PROP_ACTIVE:
    g_value_set_boolean (value, self->active);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_location_manager_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);

  switch (property_id) {
  case PROP_ENABLED:
    update_accuracy_level (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_app_auth_prompt_closed (PhoshLocationManager *self, PhoshAppAuthPrompt *prompt)
{
  gboolean grant_access;

  g_return_if_fail (PHOSH_IS_LOCATION_MANAGER (self));
  g_return_if_fail (PHOSH_IS_APP_AUTH_PROMPT (prompt));

  grant_access = phosh_app_auth_prompt_get_grant_access (GTK_WIDGET (prompt));

  g_debug ("Granting access for %p at level %d: %s",
           self->invocation,
           self->req_level,
           grant_access ? "yes" : "no");

  phosh_dbus_geo_clue2_agent_complete_authorize_app (PHOSH_DBUS_GEO_CLUE2_AGENT (self),
                                                     self->invocation,
                                                     grant_access,
                                                     self->req_level);

  /* TODO: save in permission store */

  self->req_level = LEVEL_NONE;
  self->invocation = NULL;
  self->prompt = NULL;

  return;
}


static gboolean
handle_authorize_app (PhoshDBusGeoClue2Agent *object,
                      GDBusMethodInvocation  *invocation,
                      const char             *arg_desktop_id,
                      guint                   arg_req_accuracy_level)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);
  g_autofree char *desktop_file = NULL;
  g_autofree char *body = NULL;
  g_autofree char *subtitle = NULL;

  g_autoptr (GDesktopAppInfo) app_info = NULL;
  gint level;

  g_debug ("Authorizing %s: %d", arg_desktop_id, self->enabled);

  level = get_max_level (self);
  if (arg_req_accuracy_level > level) {
    g_debug ("Req accuracy level %d > max allowed %d", arg_req_accuracy_level, level);
    phosh_dbus_geo_clue2_agent_complete_authorize_app (object,
                                                       invocation,
                                                       FALSE,
                                                       arg_req_accuracy_level);
    return TRUE;
  }

  desktop_file = g_strjoin (".", arg_desktop_id, "desktop", NULL);
  app_info = g_desktop_app_info_new (desktop_file);
  if (app_info == NULL) {
    g_debug ("Failed to find %s", desktop_file);
    phosh_dbus_geo_clue2_agent_complete_authorize_app (object,
                                                       invocation,
                                                       FALSE,
                                                       arg_req_accuracy_level);

    return TRUE;
  }

  /* TODO: look at location permission store */

  /* Cancel any ongoing prompt */
  if (self->prompt)
    gtk_widget_destroy (GTK_WIDGET (self->prompt));

  if (self->invocation) {
    phosh_dbus_geo_clue2_agent_complete_authorize_app (
      object,
      self->invocation,
      FALSE,
      arg_req_accuracy_level);
  }

  self->req_level = arg_req_accuracy_level;
  self->invocation = invocation;
  subtitle = g_strdup_printf (_("Allow '%s' to access your location information?"),
                              g_app_info_get_display_name (G_APP_INFO (app_info)));

  body = g_desktop_app_info_get_string (app_info, "X-Geoclue-Reason");
  self->prompt = phosh_app_auth_prompt_new (g_app_info_get_icon (G_APP_INFO (app_info)),
                                            _("Geolocation"),
                                            subtitle, body, _("Yes"), _("No"), FALSE, NULL);
  g_signal_connect_object (self->prompt,
                           "closed",
                           G_CALLBACK (on_app_auth_prompt_closed),
                           self,
                           G_CONNECT_SWAPPED);

  /* Show widget when not locked and keep that in sync */
  g_object_bind_property (phosh_shell_get_default (), "locked",
                          self->prompt, "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);


  return TRUE;
}


static guint
handle_get_max_accuracy_level (PhoshDBusGeoClue2Agent *object)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);
  gint level = get_max_level (self);

  g_debug ("Accuracy level %d", level);
  return level;
}


static void
phosh_location_manager_geoclue2_agent_iface_init (PhoshDBusGeoClue2AgentIface *iface)
{
  iface->handle_authorize_app = handle_authorize_app;
  iface->get_max_accuracy_level = handle_get_max_accuracy_level;
}


static void
on_bus_acquired (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  PhoshLocationManager *self;
  GDBusConnection *connection;

  connection = g_bus_get_finish (res, &err);
  if (!connection) {
    phosh_async_error_warn (err, "Failed to connect to system bus");
    return;
  }

  self = PHOSH_LOCATION_MANAGER (user_data);
  self->connection = connection;
  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                    self->connection,
                                    LOCATION_AGENT_DBUS_PATH,
                                    NULL);
  update_accuracy_level (self, self->enabled);
}


static void
on_add_agent_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  PhoshDBusGeoClue2Manager *manager = PHOSH_DBUS_GEO_CLUE2_MANAGER (source_object);
  g_autoptr (GError) err = NULL;

  if (phosh_dbus_geo_clue2_manager_call_add_agent_finish (manager, res, &err))
    g_debug ("Added ourself as geoclue agent");
  else
    g_warning ("Failed to add agent: %s", err->message);
}


static void
on_agent_in_use_changed (PhoshLocationManager *self)
{
  gboolean in_use;

  g_return_if_fail (PHOSH_IS_LOCATION_MANAGER (self));
  g_return_if_fail (PHOSH_DBUS_IS_GEO_CLUE2_MANAGER (self->manager_proxy));

  in_use = phosh_dbus_geo_clue2_manager_get_in_use (self->manager_proxy);
  g_debug ("In use: %d", in_use);

  if (in_use == self->active)
    return;

  self->active = in_use;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
}


static void
on_manager_proxy_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (user_data);
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_IS_LOCATION_MANAGER (self));

  self->manager_proxy = phosh_dbus_geo_clue2_manager_proxy_new_for_bus_finish (res, &err);
  if (self->manager_proxy == NULL) {
    g_warning ("Failed to create proxy to %s: %s", GEOCLUE_MANAGER_PATH, err->message);
    return;
  }

  phosh_dbus_geo_clue2_manager_call_add_agent (self->manager_proxy,
                                               /* Agent whitelisted in geoclue conf */
                                               "sm.puri.Phosh",
                                               self->cancel,
                                               on_add_agent_ready,
                                               NULL);
  g_signal_connect_swapped (self->manager_proxy,
                            "notify::in-use",
                            G_CALLBACK (on_agent_in_use_changed),
                            self);
  on_agent_in_use_changed (self);
}


static void
on_manager_name_appeared (GDBusConnection      *connection,
                          const char           *name,
                          const char           *name_owner,
                          PhoshLocationManager *self)
{
  g_return_if_fail (PHOSH_IS_LOCATION_MANAGER (self));
  g_debug ("%s appeared", name);

  phosh_dbus_geo_clue2_manager_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  GEOCLUE_SERVICE,
                                                  GEOCLUE_MANAGER_PATH,
                                                  self->cancel,
                                                  on_manager_proxy_ready,
                                                  self);
}

static void
on_manager_name_vanished (GDBusConnection      *connection,
                          const char           *name,
                          PhoshLocationManager *self)
{
  g_return_if_fail (PHOSH_IS_LOCATION_MANAGER (self));

  g_debug ("%s vanished", name);
  g_clear_object (&self->manager_proxy);
}


static void
phosh_location_manager_dispose (GObject *object)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  /* Close dialog and cancel pending request if ongoing */
  g_clear_pointer (&self->prompt, phosh_cp_widget_destroy);

  if (self->invocation) {
    phosh_dbus_geo_clue2_agent_complete_authorize_app (PHOSH_DBUS_GEO_CLUE2_AGENT (self),
                                                       self->invocation,
                                                       FALSE,
                                                       LEVEL_NONE);
    self->invocation = NULL;
  }

  g_clear_handle_id (&self->dbus_name_id, g_bus_unwatch_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  g_clear_object (&self->location_settings);
  g_clear_object (&self->manager_proxy);
  g_clear_object (&self->connection);

  G_OBJECT_CLASS (phosh_location_manager_parent_class)->dispose (object);
}


static void
phosh_location_manager_constructed (GObject *object)
{
  PhoshLocationManager *self = PHOSH_LOCATION_MANAGER (object);

  G_OBJECT_CLASS (phosh_location_manager_parent_class)->constructed (object);

  self->location_settings = g_settings_new ("org.gnome.system.location");
  g_settings_bind (self->location_settings, "enabled",
                   self, "enabled",
                   G_SETTINGS_BIND_DEFAULT);

  g_bus_get (G_BUS_TYPE_SYSTEM, self->cancel, on_bus_acquired, self);

  self->dbus_name_id = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                         GEOCLUE_SERVICE,
                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                         (GBusNameAppearedCallback) on_manager_name_appeared,
                                         (GBusNameVanishedCallback) on_manager_name_vanished,
                                         self,
                                         NULL);
}


static void
phosh_location_manager_class_init (PhoshLocationManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_location_manager_constructed;
  object_class->dispose = phosh_location_manager_dispose;

  object_class->set_property = phosh_location_manager_set_property;
  object_class->get_property = phosh_location_manager_get_property;

  /**
   * LocationManager:enabled:
   *
   * Whether location services are enabled
   */
  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * LocationManager:active:
   *
   * Whether location services are currently active
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_location_manager_init (PhoshLocationManager *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshLocationManager *
phosh_location_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_LOCATION_MANAGER, NULL);
}
