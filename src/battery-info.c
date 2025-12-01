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
#include "shell-priv.h"

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
  PhoshStatusIcon parent;
  gboolean        present;
  gboolean        show_detail;
} PhoshBatteryInfo;


G_DEFINE_TYPE (PhoshBatteryInfo, phosh_battery_info, PHOSH_TYPE_STATUS_ICON)


static void
phosh_battery_info_set_present (PhoshBatteryInfo *self, gboolean present)
{
  g_return_if_fail (PHOSH_IS_BATTERY_INFO (self));

  if (self->present == present)
    return;

  self->present = !!present;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


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
  case PROP_PRESENT:
    phosh_battery_info_set_present (self, g_value_get_boolean (value));
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
phosh_battery_info_class_init (PhoshBatteryInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

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
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static gboolean
transform_percent_to_info (GBinding     *binding,
                           const GValue *from_value,
                           GValue       *to_value,
                           gpointer      user_data)
{
  uint percent = g_value_get_uint (from_value);
  char *info;

  info = g_strdup_printf ("%u%%", percent);
  g_value_take_string (to_value, info);
  return TRUE;
}


static void
phosh_battery_info_init (PhoshBatteryInfo *self)
{
  GtkWidget *percentage_label = gtk_label_new (NULL);
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshBatteryManager *battery_manager = phosh_shell_get_battery_manager (shell);

  phosh_status_icon_set_extra_widget (PHOSH_STATUS_ICON (self), percentage_label);
  g_object_bind_property (self,
                          "show-detail",
                          percentage_label,
                          "visible",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (self,
                          "info",
                          phosh_status_icon_get_extra_widget (PHOSH_STATUS_ICON (self)),
                          "label",
                          G_BINDING_SYNC_CREATE);

  phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), "0%");
  phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self), "battery-missing-symbolic");

  g_object_bind_property (battery_manager,
                          "present",
                          self,
                          "present",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (battery_manager,
                          "icon-name",
                          self,
                          "icon-name",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (battery_manager,
                               "percent",
                               self,
                               "info",
                               G_BINDING_SYNC_CREATE,
                               transform_percent_to_info,
                               NULL,
                               NULL,
                               NULL);
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
