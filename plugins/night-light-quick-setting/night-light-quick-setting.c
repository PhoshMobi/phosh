/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "night-light-quick-setting.h"
#include "plugin-shell.h"

#include <glib/gi18n.h>

/**
 * PhoshNightLightQuickSetting:
 *
 * Enable and disable nightlight.
 */

struct _PhoshNightLightQuickSetting {
  PhoshQuickSetting        parent;

  GSettings               *settings;
  PhoshStatusIcon         *info;
};

G_DEFINE_TYPE (PhoshNightLightQuickSetting, phosh_night_light_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static void
on_clicked (PhoshNightLightQuickSetting *self)
{
  gboolean enabled = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));

  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), !enabled);
}


static gboolean
transform_to_icon_name (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  gboolean enabled = g_value_get_boolean (from_value);
  const char *icon_name;

  icon_name = enabled ? "night-light-symbolic" : "night-light-disabled-symbolic";
  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static gboolean
transform_to_label (GBinding     *binding,
                    const GValue *from_value,
                    GValue       *to_value,
                    gpointer      user_data)
{
  gboolean enabled = g_value_get_boolean (from_value);
  const char *label;

  label = enabled ? _("Night Light On") : _("Night Light Off");
  g_value_set_string (to_value, label);
  return TRUE;
}


static void
phosh_lockscreen_finalize (GObject *object)
{
  PhoshNightLightQuickSetting *self = PHOSH_NIGHT_LIGHT_QUICK_SETTING (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_night_light_quick_setting_parent_class)->finalize (object);
}


static void
phosh_night_light_quick_setting_class_init (PhoshNightLightQuickSettingClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_lockscreen_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/night-light-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshNightLightQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_night_light_quick_setting_init (PhoshNightLightQuickSetting *self)
{
  PhoshMonitorManager *monitor_manager;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new ("org.gnome.settings-daemon.plugins.color");
  g_settings_bind (self->settings, "night-light-enabled",
                   self, "active",
                   G_SETTINGS_BIND_DEFAULT);

  g_object_bind_property_full (self, "active",
                               self->info, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_to_icon_name,
                               NULL, NULL, NULL);

  g_object_bind_property_full (self, "active",
                               self->info, "info",
                               G_BINDING_SYNC_CREATE,
                               transform_to_label,
                               NULL, NULL, NULL);

  monitor_manager = phosh_shell_get_monitor_manager (phosh_shell_get_default ());
  g_object_bind_property (monitor_manager, "night-light-supported",
                          self, "sensitive",
                          G_BINDING_SYNC_CREATE);
}
