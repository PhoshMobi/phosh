/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-brightness-settings"

#include "phosh-config.h"

#include "brightness-settings.h"
#include "shell-priv.h"

#define POWER_SCHEMA "org.gnome.settings-daemon.plugins.power"
#define KEY_AMBIENT_ENABLED "ambient-enabled"

/**
 * PhoshBrightnessSettings:
 *
 * A widget to display brightness controls.
 */

struct _PhoshBrightnessSettings {
  GtkBin           parent;

  GtkSwitch       *auto_switch;
  GtkImage        *image;
  GtkScale        *scale;
  GtkToggleButton *toggle_btn;
  GtkStack        *toggle_stack;

  GSettings       *settings;
};

G_DEFINE_TYPE (PhoshBrightnessSettings, phosh_brightness_settings, GTK_TYPE_BIN);


static void
on_auto_brightness_activated (PhoshBrightnessSettings *self, GtkListBoxRow *row)
{
  gtk_switch_set_active (self->auto_switch, !gtk_switch_get_active (self->auto_switch));
}


static void
phosh_brightness_settings_dispose (GObject *object)
{
  PhoshBrightnessSettings *self = PHOSH_BRIGHTNESS_SETTINGS (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_brightness_settings_parent_class)->dispose (object);
}


static void
phosh_brightness_settings_class_init (PhoshBrightnessSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_brightness_settings_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/brightness-settings.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshBrightnessSettings, auto_switch);
  gtk_widget_class_bind_template_child (widget_class, PhoshBrightnessSettings, image);
  gtk_widget_class_bind_template_child (widget_class, PhoshBrightnessSettings, scale);
  gtk_widget_class_bind_template_child (widget_class, PhoshBrightnessSettings, toggle_btn);
  gtk_widget_class_bind_template_child (widget_class, PhoshBrightnessSettings, toggle_stack);

  gtk_widget_class_bind_template_callback (widget_class, on_auto_brightness_activated);

  gtk_widget_class_set_css_name (widget_class, "phosh-brightness-settings");
}


static gboolean
transform_toggle_to_stack_child_name (GBinding     *binding,
                                      const GValue *from_value,
                                      GValue       *to_value,
                                      gpointer      data)
{
  gboolean active = g_value_get_boolean (from_value);

  g_value_set_string (to_value, active ? "show-details" : "hide-details");

  return TRUE;
}


static void
phosh_brightness_settings_init (PhoshBrightnessSettings *self)
{
  PhoshShell *shell;
  PhoshBrightnessManager *brightness_manager;
  GtkAdjustment *adjustment;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (POWER_SCHEMA);

  shell = phosh_shell_get_default ();
  brightness_manager = phosh_shell_get_brightness_manager (shell);
  adjustment = phosh_brightness_manager_get_adjustment (brightness_manager);
  gtk_range_set_adjustment (GTK_RANGE (self->scale), adjustment);

  g_settings_bind (self->settings, KEY_AMBIENT_ENABLED, self->auto_switch, "active",
                   G_SETTINGS_BIND_GET | G_SETTINGS_BIND_SET);

  g_object_bind_property (brightness_manager, "icon-name",
                          self->image, "icon-name",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (brightness_manager, "auto-brightness-enabled",
                          self->scale, "has-origin",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  g_object_bind_property (brightness_manager, "has-brightness-control",
                          self, "visible",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (self->toggle_btn, "active",
                               self->toggle_stack, "visible-child-name",
                               G_BINDING_SYNC_CREATE,
                               transform_toggle_to_stack_child_name, NULL, NULL, NULL);
}


GtkWidget *
phosh_brightness_settings_new (void)
{
  return g_object_new (PHOSH_TYPE_BRIGHTNESS_SETTINGS, NULL);
}


void
phosh_brightness_settings_hide_details (PhoshBrightnessSettings *self)
{
  g_return_if_fail (PHOSH_IS_BRIGHTNESS_SETTINGS (self));

  gtk_toggle_button_set_active (self->toggle_btn, FALSE);
}
