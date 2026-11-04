/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Thomas Booker <tw.booker@outlook.com>
 */

#define G_LOG_DOMAIN "phosh-emergency-calls-manager"

#include "phosh-config.h"

#include "calls-emergency-dbus.h"
#include "emergency-calls-manager.h"
#include "emergency-contact.h"
#include "emergency-menu.h"
#include "util.h"
#include "shell-priv.h"

#define CALLS_BUS_NAME "org.gnome.Calls"
#define CALLS_OBJECT_PATH "/org/gnome/Calls"

#define EMERGENCY_CALLS_SETTINGS "mobi.phosh.shell.emergency-calls"

/**
 * PhoshEmergencyCallsManager:
 * @dbus_proxy: The DBus proxy object used for interacting with the emergency calls API.
 *
 * Manages emergency calls and contacts. Contacts are kept in
 * a GListStore containing the emergency contacts form the calls API.
 *
 * #PhoshEmergencyCallsManager provides a GListStore containing the
 * emergency contacts fetched from the calls API at `org.gnome.Calls`.
 * It is designed to be used with [type@EmergencyContactRow] and the
 * `gtk_list_box_bind_model` function.
 *
 * #PhoshEmergencyCallsManager also provides the [method@EmergencyCallsManager.call] method
 * to call an emergency contact.
 * If you are calling an emergency contact it is advised
 * that you use `phosh_emergency_calls_call` or `phosh_emergency_calls_row_call` instead of
 * calling `phosh_emergency_calls_manager_call` directly.
 */

enum {
  PROP_0,
  PROP_EMERGENCY_CONTACTS,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

enum {
  DIAL_ERROR,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

struct _PhoshEmergencyCallsManager {
  PhoshManager         parent;

  PhoshDBusEmergencyCalls *dbus_proxy;
  GCancellable        *cancel;

  GListStore          *emergency_contacts;

  PhoshEmergencyMenu  *dialog;
  GSettings           *settings;
  gboolean             enabled;
};
G_DEFINE_TYPE (PhoshEmergencyCallsManager, phosh_emergency_calls_manager, PHOSH_TYPE_MANAGER);


static void
phosh_emergency_calls_manager_set_if_enabled (PhoshEmergencyCallsManager *self, gboolean enabled)
{
  GAction *action;

  /* If disabled via settings we never enable */
  if (g_settings_get_boolean (self->settings, "enabled") == FALSE) {
    g_debug ("Emergency calls disabled in settings");
    enabled = FALSE;
  }

  if (enabled == self->enabled)
    return;

  self->enabled = enabled;
  g_message ("%s emergency calls", enabled ? "Enabling" : "Disabling");
  action = g_action_map_lookup_action (G_ACTION_MAP (phosh_shell_get_default ()),
                                       "emergency.toggle-menu");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}


static void
close_menu (PhoshEmergencyCallsManager *self)
{
  g_debug ("Closing emergency call menu");

  g_clear_pointer ((PhoshSystemModalDialog**)&self->dialog, phosh_system_modal_dialog_close);
}


static void
on_emergency_menu_done (PhoshEmergencyCallsManager *self)
{
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));

  close_menu (self);
}


static void
on_emergency_menu_activated (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (data);

  if (self->dialog) {
    close_menu (self);
    return;
  }

  self->dialog = phosh_emergency_menu_new ();
  g_signal_connect_swapped (self->dialog, "done",
                            G_CALLBACK (on_emergency_menu_done), self);

  gtk_widget_set_visible (GTK_WIDGET (self->dialog), TRUE);
}


/**
 * on_update_finish:
 *
 * Called when the DBus API returns the emergency contacts.
 * From the phosh_emergency_calls_manager_idle_init function.
 */
static void
on_update_finish (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (user_data);
  PhoshDBusEmergencyCalls *proxy = PHOSH_DBUS_EMERGENCY_CALLS (source_object);
  g_autoptr (GError) err = NULL;
  g_autoptr (GVariant) contacts = NULL;
  GVariantIter iter;
  gboolean success;

  success = phosh_dbus_emergency_calls_call_get_emergency_contacts_finish (proxy,
                                                                           &contacts,
                                                                           res,
                                                                           &err);
  if (success == FALSE) {
    if (phosh_async_error_warn (err, "Failed to get emergency contacts")) {
      /* Return right away on cancel as the manager is already gone */
      return;
    }
    goto out;
  }
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));

  g_list_store_remove_all (self->emergency_contacts);
#define CONTACT_FORMAT "(&s&si@a{sv})"
  g_variant_iter_init (&iter, contacts);
  while (TRUE) {
    g_autoptr (PhoshEmergencyContact) contact = NULL;
    g_autoptr (GVariant) properties = NULL;
    const char *id = NULL;
    const char *name = NULL;
    gint32 source;

    if (g_variant_iter_next (&iter, CONTACT_FORMAT, &id, &name, &source, &properties) == FALSE)
      break;

    contact = phosh_emergency_contact_new (id, name, source, properties);
    g_list_store_append (self->emergency_contacts, contact);
    /* At least one emergency contact found */
    success = TRUE;
  }
#undef CONTACT_FORMAT

 out:
  phosh_emergency_calls_manager_set_if_enabled (self, success);
}

/**
 * on_call_emergency_contact_finish:
 *
 * Called when the DBus API has processed the request to call an
 * emergency contact or emergency service.  From the
 * #phosh_emergency_calls_manager_call function.
 */
static void
on_call_emergency_contact_finish (GObject      *object,
                                  GAsyncResult *res,
                                  gpointer      data)
{
  PhoshDBusEmergencyCalls *proxy;
  PhoshEmergencyCallsManager *self;
  gboolean success;
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_DBUS_IS_EMERGENCY_CALLS (object));
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (data));
  proxy = PHOSH_DBUS_EMERGENCY_CALLS (object);
  self = PHOSH_EMERGENCY_CALLS_MANAGER (data);

  success = phosh_dbus_emergency_calls_call_call_emergency_contact_finish (proxy, res, &err);
  if (!success) {
    g_signal_emit (self, signals[DIAL_ERROR], 0, err);

    phosh_async_error_warn (err, "Failed to call emergency contact");
    return;
  } else {
    phosh_shell_set_locked (phosh_shell_get_default (), TRUE);
    close_menu (self);
  }
}


/**
 * phosh_emergency_calls_manager_update:
 * @self: The #PhoshEmergencyCallsManager
 *
 * This function sends a request to the emergency contact DBus API and
 * updates the list when it gets a response from the API.
 *
 * NOTE: This function does NOT create a new list it removes the
 * existing items from the list and adds the new ones from the DBus
 * API.
 */
static void
phosh_emergency_calls_manager_update (PhoshEmergencyCallsManager *self)
{
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));
  g_return_if_fail (G_IS_DBUS_PROXY (self->dbus_proxy));

  phosh_dbus_emergency_calls_call_get_emergency_contacts (
    self->dbus_proxy,
    self->cancel,
    on_update_finish,
    self);
}


static void
emergency_calls_manager_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (object);

  switch (property_id) {
  case PROP_EMERGENCY_CONTACTS:
    g_value_set_object (value, G_OBJECT (self->emergency_contacts));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
on_emergency_numbers_changed (PhoshEmergencyCallsManager *self)
{
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));

  phosh_emergency_calls_manager_update (self);
}

static void
on_name_owner_changed (PhoshEmergencyCallsManager *self)
{
  g_autofree char *name_owner = NULL;

  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));

  name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (self->dbus_proxy));
  if (name_owner)
    phosh_emergency_calls_manager_update (self);
  else
    phosh_emergency_calls_manager_set_if_enabled (self, FALSE);
}

/**
 * on_proxy_new_finish:
 *
 * Called when the DBus proxy is ready
 */
static void
on_proxy_new_finish (GObject *source_object, GAsyncResult *res, gpointer user_data)

{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (user_data);
  PhoshDBusEmergencyCalls *proxy;
  g_autoptr (GError) err = NULL;

  proxy = phosh_dbus_emergency_calls_proxy_new_for_bus_finish (res, &err);
  if (proxy == NULL) {
    phosh_async_error_warn (err, "Failed to get connect to emergency contacts DBus proxy");
    return;
  }

  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));
  self->dbus_proxy = proxy;

  g_signal_connect_swapped (self->dbus_proxy,
                            "notify::g-name-owner",
                            G_CALLBACK (on_name_owner_changed),
                            self);
  g_signal_connect_swapped (self->dbus_proxy,
                            "emergency-numbers-changed",
                            G_CALLBACK (on_emergency_numbers_changed),
                            self);
  phosh_emergency_calls_manager_update (self);
}


static void
phosh_emergency_calls_manager_idle_init (PhoshManager *manager)
{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (manager);

  /* Connect to call's emergency call DBus interface */
  phosh_dbus_emergency_calls_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                                G_DBUS_PROXY_FLAGS_NONE,
                                                CALLS_BUS_NAME,
                                                CALLS_OBJECT_PATH,
                                                self->cancel,
                                                on_proxy_new_finish,
                                                self);
}


static void
emergency_calls_manager_dispose (GObject *object)
{
  PhoshEmergencyCallsManager *self = PHOSH_EMERGENCY_CALLS_MANAGER (object);

  g_action_map_remove_action (G_ACTION_MAP (phosh_shell_get_default ()),
                              "emergency.toggle-menu");

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->dbus_proxy);
  g_clear_object (&self->emergency_contacts);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_emergency_calls_manager_parent_class)->dispose (object);
}


static void
phosh_emergency_calls_manager_class_init (PhoshEmergencyCallsManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshManagerClass *manager_class = PHOSH_MANAGER_CLASS (klass);

  object_class->get_property = emergency_calls_manager_get_property;
  object_class->dispose = emergency_calls_manager_dispose;

  manager_class->idle_init = phosh_emergency_calls_manager_idle_init;

 /**
  * PhoshEmergencyCalls:emergency-contacts:
  *
  * The [type@EmergencyContact]s
  */
  props[PROP_EMERGENCY_CONTACTS] =
    g_param_spec_object ("emergency-contacts", "", "",
                         G_TYPE_LIST_STORE,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

 /**
  * PhoshEmergencyCalls::dial-error
  *
  * Failure to dial emergency call number.
  */
  signals[DIAL_ERROR] = g_signal_new ("dial-error",
                                      G_TYPE_FROM_CLASS (klass),
                                      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                                      G_TYPE_NONE, 1, G_TYPE_ERROR);
}


static GActionEntry entries[] = {
  { .name = "emergency.toggle-menu", .activate = on_emergency_menu_activated },
};


static void
phosh_emergency_calls_manager_init (PhoshEmergencyCallsManager *self)
{
  self->emergency_contacts = g_list_store_new (PHOSH_TYPE_EMERGENCY_CONTACT);
  self->cancel = g_cancellable_new ();
  /* Force initial sync */
  self->enabled = -1;

  self->settings = g_settings_new (EMERGENCY_CALLS_SETTINGS);

  g_action_map_add_action_entries (G_ACTION_MAP (phosh_shell_get_default ()),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
  /* Disable until we connected to GNOME calls */
  phosh_emergency_calls_manager_set_if_enabled (self, FALSE);
}


PhoshEmergencyCallsManager *
phosh_emergency_calls_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_EMERGENCY_CALLS_MANAGER, NULL);
}


/**
 * phosh_emergency_calls_manager_get_list_store:
 * @self: The #PhoshEmergencyCallsManager
 *
 * Gets the `GListStore` that contains the currently valid emergency contacts.
 *
 * Returns: (transfer none): List of emergency contacts.
 */
GListStore *
phosh_emergency_calls_manager_get_list_store (PhoshEmergencyCallsManager *self)
{
  g_return_val_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self), NULL);

  return self->emergency_contacts;
}


/**
 * phosh_emergency_calls_manager_call:
 * @self: The EmergencyCallsManager to use to make the call.
 * @id: The id that identifies the emergency contact.
 *
 * Starts an emergency call with the specified id.
 */
void
phosh_emergency_calls_manager_call (PhoshEmergencyCallsManager *self,
                                    const char                 *id)
{
  g_return_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (self));

  g_debug ("Calling emergency contact ID: '%s'", id);
  phosh_dbus_emergency_calls_call_call_emergency_contact (self->dbus_proxy,
                                                          id,
                                                          NULL,
                                                          on_call_emergency_contact_finish,
                                                          self);
}
