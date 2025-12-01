/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

/* Battery Info widget */

#define G_LOG_DOMAIN "phosh-battery-info"

#include "phosh-config.h"

#include "battery-info.h"
#include "upower.h"

#include <math.h>

/**
 * PhoshBatteryInfo:
 *
 * A widget to display the battery status
 */

enum {
  PROP_0,
  PROP_SHOW_DETAIL,
  PROP_PRESENT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


typedef struct _PhoshBatteryInfo {
  PhoshStatusIcon  parent;
  UpClient        *upower;
  UpDevice        *device;
  gboolean         present;
  gboolean         show_detail;
  GCancellable    *cancel;
} PhoshBatteryInfo;


G_DEFINE_TYPE (PhoshBatteryInfo, phosh_battery_info, PHOSH_TYPE_STATUS_ICON)


static void
phosh_battery_info_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshBatteryInfo *self = PHOSH_BATTERY_INFO (object);

  switch (property_id) {
  case PROP_SHOW_DETAIL:
    phosh_battery_info_set_show_detail (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_battery_info_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshBatteryInfo *self = PHOSH_BATTERY_INFO (object);

  switch (property_id) {
  case PROP_SHOW_DETAIL:
    g_value_set_boolean (value, self->show_detail);
    break;
  case PROP_PRESENT:
    g_value_set_boolean (value, self->present);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_property_changed (PhoshBatteryInfo *self,
                     GParamSpec       *pspec,
                     UpDevice         *device)
{
  UpDeviceState state;
  gdouble percentage;
  gint smallest_ten;
  gboolean is_charging;
  gboolean is_charged;
  g_autofree char *icon_name = NULL;
  g_autofree char *info = NULL;

  g_object_get (device, "state", &state, "percentage", &percentage, NULL);

  is_charging = state == UP_DEVICE_STATE_CHARGING;
  smallest_ten = floor (percentage / 10.0) * 10;
  is_charged = state == UP_DEVICE_STATE_FULLY_CHARGED || (is_charging && smallest_ten == 100);
  info = g_strdup_printf ("%d%%", (int) (percentage + 0.5));

  if (is_charged) {
    icon_name = g_strdup ("battery-level-100-charged-symbolic");
  } else {
    if (is_charging)
      icon_name = g_strdup_printf ("battery-level-%d-charging-symbolic", smallest_ten);
    else
      icon_name = g_strdup_printf ("battery-level-%d-symbolic", smallest_ten);
  }
  phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self), icon_name);
  phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), info);
}


static void
on_up_client_new_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshBatteryInfo *self = data;
  g_autoptr (GError) err = NULL;
  UpClient *upower = NULL;

  upower = up_client_new_finish (result, &err);
  if (err != NULL) {
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

  g_object_connect (self->device,
                    "swapped-object-signal::notify::percentage",
                    G_CALLBACK (on_property_changed),
                    self,
                    "swapped-object-signal::notify::state",
                    G_CALLBACK (on_property_changed),
                    self,
                    NULL);

  g_object_bind_property (self,
                          "info",
                          phosh_status_icon_get_extra_widget (PHOSH_STATUS_ICON (self)),
                          "label",
                          G_BINDING_SYNC_CREATE);
  self->present = TRUE;
  on_property_changed (self, NULL, self->device);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
phosh_battery_info_constructed (GObject *object)
{
  PhoshBatteryInfo *self = PHOSH_BATTERY_INFO (object);

  G_OBJECT_CLASS (phosh_battery_info_parent_class)->constructed (object);

  self->cancel = g_cancellable_new ();
  up_client_new_async (self->cancel, on_up_client_new_ready, self);
}


static void
phosh_battery_info_dispose (GObject *object)
{
  PhoshBatteryInfo *self = PHOSH_BATTERY_INFO (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->device);
  g_clear_object (&self->upower);

  G_OBJECT_CLASS (phosh_battery_info_parent_class)->dispose (object);
}


static void
phosh_battery_info_class_init (PhoshBatteryInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = phosh_battery_info_constructed;
  object_class->dispose = phosh_battery_info_dispose;
  object_class->get_property = phosh_battery_info_get_property;
  object_class->set_property = phosh_battery_info_set_property;

  gtk_widget_class_set_css_name (widget_class, "phosh-battery-info");

  /**
   * PhoshBatteryInfo:show-detail
   *
   * Whether to show battery percentage detail
   */
  props[PROP_SHOW_DETAIL] =
    g_param_spec_boolean ("show-detail", "", "",
                          FALSE,
                          G_PARAM_CONSTRUCT |
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  /**
   * PhoshBatteryInfo:present
   *
   * Whether battery information is present
   */
  props[PROP_PRESENT] =
    g_param_spec_boolean ("present", "", "",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_battery_info_init (PhoshBatteryInfo *self)
{
  GtkWidget *percentage = gtk_label_new (NULL);
  phosh_status_icon_set_extra_widget (PHOSH_STATUS_ICON (self), percentage);

  g_object_bind_property (self,
                          "show-detail",
                          percentage,
                          "visible",
                          G_BINDING_SYNC_CREATE);

  phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), "0%");
  phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self), "battery-missing-symbolic");
}


GtkWidget *
phosh_battery_info_new (void)
{
  return g_object_new (PHOSH_TYPE_BATTERY_INFO, NULL);
}


void
phosh_battery_info_set_show_detail (PhoshBatteryInfo *self, gboolean show)
{
  g_return_if_fail (PHOSH_IS_BATTERY_INFO (self));

  if (self->show_detail == show)
    return;

  self->show_detail = !!show;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOW_DETAIL]);
}


gboolean
phosh_battery_info_get_show_detail (PhoshBatteryInfo *self)
{
  g_return_val_if_fail (PHOSH_IS_BATTERY_INFO (self), FALSE);

  return self->show_detail;
}
