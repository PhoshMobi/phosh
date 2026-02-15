/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "mobile-data-quick-setting.h"
#include "plugin-shell.h"

#include <glib/gi18n.h>

/**
 * PhoshMobileDataQuickSetting:
 *
 * Toggle mobile data
 */

struct _PhoshMobileDataQuickSetting {
  PhoshQuickSetting  parent;

  PhoshStatusIcon   *info;
};

G_DEFINE_TYPE (PhoshMobileDataQuickSetting, phosh_mobile_data_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static void
toggle_sensitive_cb (PhoshMobileDataQuickSetting *self)
{
  PhoshWWan *wwan = phosh_shell_get_wwan (phosh_shell_get_default ());
  gboolean has_data, unlocked, enabled, sensitive;

  has_data = phosh_wwan_has_data (wwan);
  unlocked = phosh_wwan_is_unlocked (wwan);
  enabled = phosh_wwan_is_enabled (wwan);

  sensitive = has_data && unlocked && enabled;
  gtk_widget_set_sensitive (GTK_WIDGET (self), sensitive);
}


static void
on_clicked (PhoshMobileDataQuickSetting *self)
{
  PhoshWWan *wwan = phosh_shell_get_wwan (phosh_shell_get_default ());
  gboolean enabled;

  enabled = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));
  phosh_wwan_set_data_enabled (wwan, !enabled);
}


static gboolean
transform_to_icon_name (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  gboolean enabled = g_value_get_boolean (from_value);
  const char *icon_name;

  icon_name = enabled ? "mobile-data-symbolic" : "mobile-data-disabled-symbolic";
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

  label = enabled ? _("Mobile Data On") : _("Mobile Data Off");
  g_value_set_string (to_value, label);
  return TRUE;
}


static void
phosh_mobile_data_quick_setting_class_init (PhoshMobileDataQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/mobile-data-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshMobileDataQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_mobile_data_quick_setting_init (PhoshMobileDataQuickSetting *self)
{
  PhoshWWan *wwan;

  gtk_widget_init_template (GTK_WIDGET (self));

  wwan = phosh_shell_get_wwan (phosh_shell_get_default ());

  g_object_bind_property (wwan, "data-enabled",
                          self, "active",
                          G_BINDING_SYNC_CREATE);

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

  g_object_connect (wwan,
                    "swapped-object-signal::notify::enabled",
                    toggle_sensitive_cb,
                    self,
                    "swapped-object-signal::notify::has-data",
                    toggle_sensitive_cb,
                    self,
                    "swapped-object-signal::notify::unlocked",
                    toggle_sensitive_cb,
                    self,
                    NULL);
}
