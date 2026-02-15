/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Gotam Gorabh <gautamy672@gmail.com>
 */

#include "location-quick-setting.h"
#include "quick-setting.h"
#include "status-icon.h"

#include <glib/gi18n.h>

#define LOCATION_SETTINGS "org.gnome.system.location"
#define ENABLED_KEY "enabled"

/**
 * PhoshLocationQuickSetting:
 *
 * A quick setting to toggle location services on/off
 */
struct _PhoshLocationQuickSetting {
  PhoshQuickSetting        parent;

  GSettings               *settings;
  PhoshStatusIcon         *info;
};

G_DEFINE_TYPE (PhoshLocationQuickSetting, phosh_location_quick_setting, PHOSH_TYPE_QUICK_SETTING);

static void
on_clicked (PhoshLocationQuickSetting *self)
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

  icon_name = enabled ? "location-services-active-symbolic" : "location-services-disabled-symbolic";
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

  label = enabled ? _("Location On") : _("Location Off");
  g_value_set_string (to_value, label);
  return TRUE;
}

static void
phosh_location_quick_setting_finalize (GObject *object)
{
  PhoshLocationQuickSetting *self = PHOSH_LOCATION_QUICK_SETTING (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_location_quick_setting_parent_class)->finalize (object);
}


static void
phosh_location_quick_setting_class_init (PhoshLocationQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_location_quick_setting_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/location-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_location_quick_setting_init (PhoshLocationQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (LOCATION_SETTINGS);

  g_settings_bind (self->settings, "enabled",
                   self, "active",
                   G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

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
}
