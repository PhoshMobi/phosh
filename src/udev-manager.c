/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-udev-manager"

#include "phosh-config.h"

#include "dbus/login1-session-dbus.h"
#include "monitor/monitor.h"
#include "shell-priv.h"
#include "udev-manager.h"

#define BUS_NAME "org.freedesktop.login1"
#define OBJECT_PATH "/org/freedesktop/login1/session/auto"

#define BACKLIGHT_SUBSYSTEM "backlight"
#define LEDS_SUBSYSTEM "leds"

/**
 * PhoshUdevManager:
 *
 * Manage a udev client for the subsystems we're interested in
 */

enum {
  BACKLIGHT_CHANGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _PhoshUdevManager {
  GObject                parent;

  GUdevClient           *udev_client;

  PhoshDBusLoginSession *session_proxy;
};

G_DEFINE_TYPE (PhoshUdevManager, phosh_udev_manager, G_TYPE_OBJECT)


static GUdevDevice *
phosh_backlight_sysfs_udev_get_type (GList *devices, const char *type)
{
  for (GList *d = devices; d != NULL; d = d->next) {
    GUdevDevice *device = d->data;
    const char *t;

    t = g_udev_device_get_sysfs_attr (device, "type");
    if (g_strcmp0 (t, type) == 0)
      return g_object_ref (device);
  }
  return NULL;
}


static GUdevDevice *
phosh_backlight_sysfs_udev_get_raw (GList      *devices,
                                    const char *connector_name)
{
  for (GList *d = devices; d != NULL; d = d->next) {
    g_autoptr (GUdevDevice) parent = NULL;
    GUdevDevice *device = d->data;
    const char *attr = g_udev_device_get_sysfs_attr (device, "type");
    const char *prop;

    if (g_strcmp0 (attr, "raw") != 0)
      continue;

    parent = g_udev_device_get_parent (device);
    if (!parent)
      continue;

    prop = g_udev_device_get_subsystem (parent);
    if (g_strcmp0 (prop, "drm") != 0)
      continue;

    /* The drm-connector name  `card[n]-[connector-name]` */
    prop = g_udev_device_get_name (parent);
    if (!prop || !g_str_has_suffix (prop, connector_name))
      continue;

    attr = g_udev_device_get_sysfs_attr (parent, "enabled");
    if (g_strcmp0 (attr, "enabled") != 0)
      continue;

    return g_object_ref (device);
  }

  return NULL;
}


static void
on_uevent (PhoshUdevManager *self, const char *action, GUdevDevice *device)
{
  if (g_str_equal (g_udev_device_get_subsystem (device), BACKLIGHT_SUBSYSTEM) &&
      g_str_equal (action, "change")) {
    g_signal_emit (self, signals[BACKLIGHT_CHANGED], 0, device);
  }
}


static void
phosh_udev_manager_finalize (GObject *object)
{
  PhoshUdevManager *self = PHOSH_UDEV_MANAGER (object);

  g_clear_object (&self->session_proxy);
  g_clear_object (&self->udev_client);

  G_OBJECT_CLASS (phosh_udev_manager_parent_class)->finalize (object);
}


static void
phosh_udev_manager_class_init (PhoshUdevManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_udev_manager_finalize;

  signals[BACKLIGHT_CHANGED] = g_signal_new ("backlight-changed",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             1,
                                             G_UDEV_TYPE_DEVICE);
}


static void
phosh_udev_manager_init (PhoshUdevManager *self)
{
  const char * const subsystems[] = { BACKLIGHT_SUBSYSTEM, LEDS_SUBSYSTEM, NULL };
  g_autoptr (GError) err = NULL;

  self->udev_client = g_udev_client_new (subsystems);
  g_signal_connect_object (self->udev_client,
                           "uevent",
                           G_CALLBACK (on_uevent),
                           self,
                           G_CONNECT_SWAPPED);

  /* This happens before any UI is up so a sync call is o.k. */
  self->session_proxy =
    phosh_dbus_login_session_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                     G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                     BUS_NAME,
                                                     OBJECT_PATH,
                                                     NULL,
                                                     &err);
  if (!self->session_proxy)
    g_debug ("Failed to get login1 session proxy: %s", err->message);
}


PhoshUdevManager *
phosh_udev_manager_get_default (void)
{
  static PhoshUdevManager *instance;

  if (instance == NULL) {
    instance = g_object_new (PHOSH_TYPE_UDEV_MANAGER, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
  }
  return instance;
}

/**
 * phosh_udev_manager_find_backlight:
 * @self: The udev maanger
 * @connector_name: The connector name
 *
 * Get the backlight corresponding to the given connector name
 *
 * Returns:(transfer full): The list of torch devices
 */
GUdevDevice *
phosh_udev_manager_find_backlight (PhoshUdevManager *self, const char *connector_name)
{
  g_autolist (GUdevDevice) devices = NULL;
  PhoshMonitorConnectorType connector_type;
  GUdevDevice *device;
  gboolean is_builtin;

  g_return_val_if_fail (PHOSH_IS_UDEV_MANAGER (self), NULL);

  connector_type = phosh_monitor_connector_type_from_name (connector_name);
  is_builtin = phosh_monitor_connector_is_builtin (connector_type);

  if (phosh_shell_get_debug_flags () & PHOSH_SHELL_DEBUG_FLAG_FAKE_BUILTIN)
    is_builtin = TRUE;

  devices = g_udev_client_query_by_subsystem (self->udev_client, BACKLIGHT_SUBSYSTEM);
  if (!devices)
    return NULL;

  /* Prefer the types firmware -> platform -> raw (see g-s-d < 49) */
  if (is_builtin) {
    device = phosh_backlight_sysfs_udev_get_type (devices, "firmware");
    if (device)
      return device;

    device = phosh_backlight_sysfs_udev_get_type (devices, "platform");
    if (device)
      return device;
  }

  device = phosh_backlight_sysfs_udev_get_raw (devices, connector_name);
  if (device)
    return device;

  if (is_builtin)
    device = phosh_backlight_sysfs_udev_get_type (devices, "raw");

  return device;
}

/**
 * phosh_udev_manager_find_torches:
 * @self: The udev maanger
 *
 * Get the torch devices in the the system
 *
 * Returns:(transfer full): The list of torch devices
 */
GList *
phosh_udev_manager_find_torches (PhoshUdevManager *self, GError **err)
{
  g_autoptr (GUdevEnumerator) udev_enumerator = NULL;
  g_autolist (GUdevDevice) device_list = NULL;

  udev_enumerator = g_udev_enumerator_new (self->udev_client);
  g_udev_enumerator_add_match_subsystem (udev_enumerator, LEDS_SUBSYSTEM);
  g_udev_enumerator_add_match_name (udev_enumerator, "*:torch*");
  g_udev_enumerator_add_match_name (udev_enumerator, "*:flash*");

  device_list = g_udev_enumerator_execute (udev_enumerator);
  if (!device_list) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Failed to enumerate LED devices");
    return NULL;
  }

  return g_steal_pointer (&device_list);
}

/**
 * phosh_udev_manager_get_session_proxy:
 * @self: The manager
 *
 * Get Logind's session manager.
 *
 * Returns: (transfer full)(nullable): The session manager
 */
PhoshDBusLoginSession *
phosh_udev_manager_get_session_proxy (PhoshUdevManager *self)
{
  g_return_val_if_fail (PHOSH_IS_UDEV_MANAGER (self), NULL);

  if (self->session_proxy)
    return g_object_ref (self->session_proxy);

  return NULL;
}
