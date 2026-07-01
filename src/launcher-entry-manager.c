/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-launcher-entry-manager"

#include "idle-manager.h"
#include "launcher-entry-manager.h"
#include "phosh-marshalers.h"
#include "session-presence.h"
#include "shell-priv.h"
#include "util.h"

#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

/**
 * PhoshLauncherEntryManager:
 *
 * Handles the launcher entry DBus API. See
 * https://wiki.ubuntu.com/Unity/LauncherAPI
 *
 * We currently don't own the `com.canonical.Unity` DBus name which is used
 * by clients to refresh their values as most clients don't seem to care.
 */

enum {
  INFO_UPDATED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

typedef struct _PhoshLauncherEntryManager
{
  PhoshManager     parent;

  int              dbus_id;
  GDBusConnection *session_bus;

  GCancellable    *cancel;
} PhoshLauncherEntryManager;

G_DEFINE_TYPE (PhoshLauncherEntryManager, phosh_launcher_entry_manager, PHOSH_TYPE_MANAGER);


static void
on_update (GDBusConnection *connection,
           const char      *sender_name,
           const char      *object_path,
           const char      *interface_name,
           const char      *signal_name,
           GVariant        *parameters,
           gpointer         user_data)
{
#define APP_URI_SCHEME "application://"
  PhoshLauncherEntryManager *self = PHOSH_LAUNCHER_ENTRY_MANAGER (user_data);
  const char *app_uri, *desktop_file;
  g_autoptr (GVariant) properties = NULL;

  if (g_strcmp0 (g_variant_get_type_string (parameters), "(sa{sv})")) {
    g_warning ("Invalid parameters for launcher entry from %s", sender_name);
    return;
  }

  g_variant_get (parameters, "(&s@a{sv})", &app_uri, &properties);

  if (!g_str_has_prefix (app_uri, APP_URI_SCHEME)) {
    g_warning ("Invalid prefix in %s from %s", app_uri, sender_name);
    return;
  }

  desktop_file = &app_uri[strlen (APP_URI_SCHEME)];

  g_debug ("%s: %s: %s", object_path, desktop_file, signal_name);

  g_signal_emit (self, signals[INFO_UPDATED], 0, desktop_file, properties);
#undef APP_URI_SCHEME
}


static void
on_bus_get_finished (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshLauncherEntryManager *self;
  g_autoptr (GError) err = NULL;
  GDBusConnection *session_bus;

  session_bus = g_bus_get_finish (res, &err);
  if (!session_bus) {
    phosh_async_error_warn (err, "Failed to connect to session bus");
    return;
  }

  self = PHOSH_LAUNCHER_ENTRY_MANAGER (user_data);
  self->session_bus = session_bus;

  /* Listen for launcher entry signals */
  self->dbus_id = g_dbus_connection_signal_subscribe (self->session_bus,
                                                      NULL,
                                                      "com.canonical.Unity.LauncherEntry",
                                                      "Update",
                                                      NULL,
                                                      NULL,
                                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                                      on_update,
                                                      self, NULL);
}


static void
phosh_launcher_entry_manager_idle_init (PhoshManager *manager)
{
  PhoshLauncherEntryManager *self = PHOSH_LAUNCHER_ENTRY_MANAGER (manager);

  g_bus_get (G_BUS_TYPE_SESSION, self->cancel, on_bus_get_finished, self);
}


static void
phosh_launcher_entry_manager_finalize (GObject *object)
{
  PhoshLauncherEntryManager *self = PHOSH_LAUNCHER_ENTRY_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  if (self->dbus_id) {
    g_dbus_connection_signal_unsubscribe (self->session_bus, self->dbus_id);
    self->dbus_id = 0;
  }
  g_clear_object (&self->session_bus);

  G_OBJECT_CLASS (phosh_launcher_entry_manager_parent_class)->finalize (object);
}


static void
phosh_launcher_entry_manager_class_init (PhoshLauncherEntryManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshManagerClass *manager_class = PHOSH_MANAGER_CLASS (klass);

  object_class->finalize = phosh_launcher_entry_manager_finalize;

  manager_class->idle_init = phosh_launcher_entry_manager_idle_init;

  signals[INFO_UPDATED] = g_signal_new ("info-updated",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL,
                                        _phosh_marshal_VOID__STRING_VARIANT,
                                        G_TYPE_NONE,
                                        2,
                                        G_TYPE_STRING,
                                        G_TYPE_VARIANT);
  g_signal_set_va_marshaller (signals[INFO_UPDATED],
                              G_TYPE_FROM_CLASS (klass),
                              _phosh_marshal_VOID__STRING_VARIANTv);
}


static void
phosh_launcher_entry_manager_init (PhoshLauncherEntryManager *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshLauncherEntryManager *
phosh_launcher_entry_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_LAUNCHER_ENTRY_MANAGER, NULL);
}
