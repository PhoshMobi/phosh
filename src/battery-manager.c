/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-battery-manager"

#include "phosh-config.h"
#include "battery-manager.h"

#include "upower.h"

#include <math.h>

/**
 * PhoshBatteryManager:
 *
 * Track batteries and their charging state
 */

enum {
  PROP_0,
  PROP_PRESENT,
  PROP_ICON_NAME,
  PROP_PERCENT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshBatteryManager {
  PhoshManager  parent;

  UpClient     *upower;
  UpDevice     *device;
  GCancellable *cancel;

  gboolean      present;
  char         *icon_name;
  uint          percent;
};
G_DEFINE_TYPE (PhoshBatteryManager, phosh_battery_manager, PHOSH_TYPE_MANAGER)


static void
on_property_changed (PhoshBatteryManager *self,
                     GParamSpec          *pspec,
                     UpDevice            *device)
{
  UpDeviceState state;
  double percentage;
  int smallest_ten;
  uint percent;
  gboolean is_charging;
  gboolean is_charged;
  g_autofree char *icon_name = NULL;

  g_object_get (device, "state", &state, "percentage", &percentage, NULL);

  is_charging = state == UP_DEVICE_STATE_CHARGING;
  smallest_ten = floor (percentage / 10.0) * 10;
  is_charged = state == UP_DEVICE_STATE_FULLY_CHARGED || (is_charging && smallest_ten == 100);

  if (is_charged) {
    icon_name = g_strdup ("battery-level-100-charged-symbolic");
  } else {
    if (is_charging)
      icon_name = g_strdup_printf ("battery-level-%d-charging-symbolic", smallest_ten);
    else
      icon_name = g_strdup_printf ("battery-level-%d-symbolic", smallest_ten);
  }

  if (g_strcmp0 (self->icon_name, icon_name)) {
    g_free (self->icon_name);
    self->icon_name = g_steal_pointer (&icon_name);
    g_debug ("New icon: %s", self->icon_name);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
  }

  percent = (int) (percentage + 0.5);
  if (self->percent != percent) {
    self->percent = percent;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PERCENT]);
  }
}


static void
on_up_client_new_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshBatteryManager *self = PHOSH_BATTERY_MANAGER (data);
  g_autoptr (GError) err = NULL;
  UpClient *upower = NULL;

  upower = up_client_new_finish (result, &err);
  if (!upower) {
    g_message ("Failed to get UPower Client: %s", err->message);
    return;
  }

  self->upower = upower;

  /* TODO: this is a oversimplified sync call */
  self->device = up_client_get_display_device (self->upower);
  if (self->device == NULL) {
    g_warning ("Failed to get upowerd display device");
    return;
  }

  g_debug ("Got upower display device");
  g_object_connect (self->device,
                    "swapped-object-signal::notify::percentage",
                    G_CALLBACK (on_property_changed),
                    self,
                    "swapped-object-signal::notify::state",
                    G_CALLBACK (on_property_changed),
                    self,
                    NULL);

  self->present = TRUE;
  on_property_changed (self, NULL, self->device);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
phosh_battery_manager_idle_init (PhoshManager *manager)
{
  PhoshBatteryManager *self = PHOSH_BATTERY_MANAGER (manager);

  up_client_new_async (self->cancel, on_up_client_new_ready, self);
}


static void
phosh_battery_manager_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PhoshBatteryManager *self = PHOSH_BATTERY_MANAGER (object);

  switch (property_id) {
  case PROP_PRESENT:
    g_value_set_boolean (value, self->present);
    break;
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  case PROP_PERCENT:
    g_value_set_uint (value, self->percent);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_battery_manager_dispose (GObject *object)
{
  PhoshBatteryManager *self = PHOSH_BATTERY_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->device);
  g_clear_object (&self->upower);

  g_clear_pointer (&self->icon_name, g_free);

  G_OBJECT_CLASS (phosh_battery_manager_parent_class)->dispose (object);
}


static void
phosh_battery_manager_class_init (PhoshBatteryManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshManagerClass *manager_class = PHOSH_MANAGER_CLASS (klass);

  object_class->get_property = phosh_battery_manager_get_property;
  object_class->dispose = phosh_battery_manager_dispose;

  manager_class->idle_init = phosh_battery_manager_idle_init;

  /**
   * PhoshBatteryManager:present
   *
   * Whether battery information is present
   */
  props[PROP_PRESENT] =
    g_param_spec_boolean ("present", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshBatteryManager:icon-name:
   *
   * The battery indicator icon name
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshBatteryManager:percent:
   *
   * The charge percentage of the main battery
   */
  props[PROP_PERCENT] =
    g_param_spec_uint ("percent", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_battery_manager_init (PhoshBatteryManager *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshBatteryManager *
phosh_battery_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_BATTERY_MANAGER, NULL);
}
