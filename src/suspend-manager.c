/*
 * Copyright (C) 2022-2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-suspend-manager"

#include "phosh-config.h"

#include "login1-manager-dbus.h"
#include "util.h"
#include "shell-priv.h"
#include "suspend-manager.h"
#include "wifi-manager.h"

#include <gio/gunixfdlist.h>

#define LOGIN_BUS_NAME "org.freedesktop.login1"
#define LOGIN_OBJECT_PATH "/org/freedesktop/login1"

/**
 * PhoshSuspendManager:
 *
 * Manages suspend and inhibit's suspend when not useful.
 */
struct _PhoshSuspendManager {
  PhoshManager           parent;

  PhoshDBusLoginManager *logind_manager_proxy;
  GHashTable            *inhibitors; /* key: name, value: fd */

  GCancellable          *cancel;
};
G_DEFINE_TYPE (PhoshSuspendManager, phosh_suspend_manager, PHOSH_TYPE_MANAGER)


static void
inhibit_suspend (PhoshSuspendManager *self, const char *what, const char *reason)
{
  PhoshSessionManager *sm = phosh_shell_get_session_manager (phosh_shell_get_default ());
  guint cookie;

  cookie = phosh_session_manager_inhibit (sm, PHOSH_SESSION_INHIBIT_SUSPEND, reason);
  g_hash_table_insert (self->inhibitors, g_strdup (what), GUINT_TO_POINTER (cookie));
}


static void
uninhibit_suspend (PhoshSuspendManager *self, const char *what)
{
  PhoshSessionManager *sm = phosh_shell_get_session_manager (phosh_shell_get_default ());
  gpointer value;

  value = g_hash_table_lookup (self->inhibitors, what);
  if (value)
    phosh_session_manager_uninhibit (sm, GPOINTER_TO_UINT (value));

  g_hash_table_remove (self->inhibitors, what);
}


static void
on_is_hotspot_master_changed (PhoshSuspendManager *self,
                              GParamSpec          *pspec,
                              PhoshWifiManager    *wifi_manager)
{
  gboolean is_hotspot_master;

  g_return_if_fail (PHOSH_IS_SUSPEND_MANAGER (self));
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (wifi_manager));

  is_hotspot_master = phosh_wifi_manager_is_hotspot_master (wifi_manager);

  if (is_hotspot_master) {
    inhibit_suspend (self, "wifi-hotspot", "Wi-Fi hotspot active");
  } else {
    g_debug ("Clearing Wi-Fi hotspot suspend inhibit");
    uninhibit_suspend (self, "wifi-hotspot");
  }
}



static void
uninhibit (gpointer pointer)
{
  int fd = GPOINTER_TO_INT (pointer);

  if (fd >= 0)
    close (fd);
}


static void
on_logind_manager_proxy_new_for_bus_finish (GObject             *source_object,
                                            GAsyncResult        *res,
                                            PhoshSuspendManager *self)
{
  g_autoptr (GError) err = NULL;
  PhoshWifiManager *wifi_manager;
  PhoshDBusLoginManager *proxy;

  proxy = phosh_dbus_login_manager_proxy_new_for_bus_finish (res, &err);
  if (proxy == NULL) {
    phosh_dbus_service_error_warn (err, "Failed to get login1 manager proxy");
    return;
  }

  g_return_if_fail (PHOSH_IS_SUSPEND_MANAGER (self));
  g_debug ("Connected to " LOGIN_OBJECT_PATH);
  self->logind_manager_proxy = proxy;

  wifi_manager = phosh_shell_get_wifi_manager (phosh_shell_get_default());
  g_signal_connect_swapped (wifi_manager,
                            "notify::is-hotspot-master",
                            G_CALLBACK (on_is_hotspot_master_changed),
                            self);
  on_is_hotspot_master_changed (self, NULL, wifi_manager);
}


static void
phosh_suspend_manager_idle_init (PhoshManager *manager)
{
  PhoshSuspendManager *self = PHOSH_SUSPEND_MANAGER (manager);

  phosh_dbus_login_manager_proxy_new_for_bus (
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    LOGIN_BUS_NAME,
    LOGIN_OBJECT_PATH,
    self->cancel,
    (GAsyncReadyCallback) on_logind_manager_proxy_new_for_bus_finish,
    self);
}


static void
phosh_suspend_manager_finalize (GObject *object)
{
  PhoshSuspendManager *self = PHOSH_SUSPEND_MANAGER(object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->logind_manager_proxy);
  g_clear_pointer (&self->inhibitors, g_hash_table_destroy);

  G_OBJECT_CLASS (phosh_suspend_manager_parent_class)->finalize (object);
}


static void
phosh_suspend_manager_class_init (PhoshSuspendManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshManagerClass *manager_class = PHOSH_MANAGER_CLASS (klass);

  object_class->finalize = phosh_suspend_manager_finalize;
  manager_class->idle_init = phosh_suspend_manager_idle_init;
}


static void
on_suspend_finished (PhoshDBusLoginManager *proxy,
                      GAsyncResult         *res,
                      PhoshSessionManager  *self)
{
  g_autoptr (GError) err = NULL;

  if (!phosh_dbus_login_manager_call_suspend_finish (proxy, res, &err))
    g_warning ("Failed to suspend: %s", err->message);
}


static void
on_suspend_activated (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshSuspendManager *self = PHOSH_SUSPEND_MANAGER (data);

  g_return_if_fail (PHOSH_IS_SUSPEND_MANAGER (self));
  g_return_if_fail (PHOSH_DBUS_IS_LOGIN_MANAGER_PROXY (self->logind_manager_proxy));

  phosh_dbus_login_manager_call_suspend (self->logind_manager_proxy,
                                         TRUE,
                                         self->cancel,
                                         (GAsyncReadyCallback)on_suspend_finished,
                                         self);
}



static GActionEntry entries[] = {
  { .name = "suspend.trigger-suspend", .activate = on_suspend_activated },
};


static void
phosh_suspend_manager_init (PhoshSuspendManager *self)
{
  self->cancel = g_cancellable_new ();
  self->inhibitors = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            uninhibit);
  g_action_map_add_action_entries (G_ACTION_MAP (phosh_shell_get_default ()),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
}


PhoshSuspendManager *
phosh_suspend_manager_new (void)
{
  return PHOSH_SUSPEND_MANAGER (g_object_new (PHOSH_TYPE_SUSPEND_MANAGER, NULL));
}
