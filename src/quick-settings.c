/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-quick-settings"

#include "phosh-config.h"

#include "quick-settings.h"

#include "bt-status-page.h"
#include "feedback-status-page.h"
#include "plugin-loader.h"
#include "quick-setting.h"
#include "quick-settings-box.h"
#include "shell-priv.h"
#include "wifi-status-page.h"

#define CUSTOM_QUICK_SETTINGS_SCHEMA "sm.puri.phosh.plugins"
#define CUSTOM_QUICK_SETTINGS_KEY "quick-settings"

/**
 * PhoshQuickSettings:
 *
 * A widget to display quick-settings using PhoshQuickSettingsBox.
 *
 * `PhoshQuickSettings` holds the default quick-settings and those loaded as plugins. It manages
 * their interaction with user by launching appropriate actions.
 *
 * For example, tapping a Wi-Fi quick-setting would toggle its off/on state. Long pressing a
 * rotation quick-setting would change the rotation configuration.
 */

struct _PhoshQuickSettings {
  GtkBin parent;

  PhoshQuickSettingsBox *box;

  GSettings *plugin_settings;
  PhoshPluginLoader *plugin_loader;
  GPtrArray *custom_quick_settings;
};

G_DEFINE_TYPE (PhoshQuickSettings, phosh_quick_settings, GTK_TYPE_BIN);


static void
on_wwan_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshWWan *wwan;
  gboolean enabled;

  wwan = phosh_shell_get_wwan (shell);
  g_return_if_fail (PHOSH_IS_WWAN (wwan));

  enabled = phosh_wwan_is_enabled (wwan);
  phosh_wwan_set_enabled (wwan, !enabled);
}


static void
on_wifi_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshWifiManager *manager;
  gboolean enabled;

  manager = phosh_shell_get_wifi_manager (shell);
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (manager));

  enabled = phosh_wifi_manager_get_enabled (manager);
  phosh_wifi_manager_set_enabled (manager, !enabled);
}


static void
on_bt_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshBtManager *manager;
  gboolean enabled;

  manager = phosh_shell_get_bt_manager (shell);
  g_return_if_fail (PHOSH_IS_BT_MANAGER (manager));

  enabled = phosh_bt_manager_get_enabled (manager);
  phosh_bt_manager_set_enabled (manager, !enabled);
}


static void
on_battery_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  GActionGroup *group;

  group = gtk_widget_get_action_group (GTK_WIDGET (self), "settings");
  g_return_if_fail (group);
  g_action_group_activate_action (group,
                                  "launch-panel",
                                  g_variant_new ("(s@av)", "power", g_variant_new ("av", NULL)));
}


static void
on_rotate_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshRotationManager *rotation_manager;
  PhoshRotationManagerMode mode;
  PhoshMonitorTransform transform;
  gboolean locked;

  rotation_manager = phosh_shell_get_rotation_manager (shell);
  g_return_if_fail (rotation_manager);
  mode = phosh_rotation_manager_get_mode (rotation_manager);

  switch (mode) {
  case PHOSH_ROTATION_MANAGER_MODE_OFF:
    transform = phosh_rotation_manager_get_transform (rotation_manager) ?
      PHOSH_MONITOR_TRANSFORM_NORMAL : PHOSH_MONITOR_TRANSFORM_270;
    phosh_rotation_manager_set_transform (rotation_manager, transform);
    break;
  case PHOSH_ROTATION_MANAGER_MODE_SENSOR:
    locked = phosh_rotation_manager_get_orientation_locked (rotation_manager);
    phosh_rotation_manager_set_orientation_locked (rotation_manager, !locked);
    break;
  default:
    g_assert_not_reached ();
  }
}


static void
on_rotate_long_pressed (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshRotationManager *rotation_manager;
  PhoshRotationManagerMode mode;

  rotation_manager = phosh_shell_get_rotation_manager (shell);
  g_return_if_fail (rotation_manager);

  mode = phosh_rotation_manager_get_mode (rotation_manager);
  switch (mode) {
  case PHOSH_ROTATION_MANAGER_MODE_OFF:
    mode = PHOSH_ROTATION_MANAGER_MODE_SENSOR;
    break;
  case PHOSH_ROTATION_MANAGER_MODE_SENSOR:
    mode = PHOSH_ROTATION_MANAGER_MODE_OFF;
    break;
  default:
    g_assert_not_reached ();
  }
  g_debug ("Changing rotation mode to %d", mode);
  phosh_rotation_manager_set_mode (rotation_manager, mode);
}


static void
on_feedback_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshFeedbackManager *manager;

  manager = phosh_shell_get_feedback_manager (shell);
  g_return_if_fail (PHOSH_IS_FEEDBACK_MANAGER (manager));

  phosh_feedback_manager_toggle (manager);
}


static void
on_torch_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshTorchManager *manager;

  manager = phosh_shell_get_torch_manager (shell);
  g_return_if_fail (PHOSH_IS_TORCH_MANAGER (manager));

  phosh_torch_manager_toggle (manager);
}


static void
on_docked_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshDockedManager *manager;
  gboolean enabled;

  manager = phosh_shell_get_docked_manager (shell);
  g_return_if_fail (PHOSH_IS_DOCKED_MANAGER (manager));

  enabled = phosh_docked_manager_get_enabled (manager);
  phosh_docked_manager_set_enabled (manager, !enabled);
}


static void
on_vpn_clicked (PhoshQuickSettings *self, PhoshQuickSetting *child)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshVpnManager *vpn_manager;

  vpn_manager = phosh_shell_get_vpn_manager (shell);
  g_return_if_fail (PHOSH_IS_VPN_MANAGER (vpn_manager));

  phosh_vpn_manager_toggle_last_connection (vpn_manager);
}


static void
unload_custom_quick_setting (GtkWidget *quick_setting)
{
  PhoshQuickSettingsBox *box = PHOSH_QUICK_SETTINGS_BOX (gtk_widget_get_parent (quick_setting));
  phosh_quick_settings_box_remove (box, PHOSH_QUICK_SETTING (quick_setting));
}


static void
load_custom_quick_settings (PhoshQuickSettings *self, GSettings *settings, char *key)
{
  g_auto (GStrv) plugins = NULL;
  GtkWidget *widget;

  g_ptr_array_remove_range (self->custom_quick_settings, 0, self->custom_quick_settings->len);
  plugins = g_settings_get_strv (self->plugin_settings, CUSTOM_QUICK_SETTINGS_KEY);

  if (plugins == NULL)
    return;

  for (int i = 0; plugins[i]; i++) {
    g_debug ("Loading custom quick setting: %s", plugins[i]);
    widget = phosh_plugin_loader_load_plugin (self->plugin_loader, plugins[i]);

    if (widget == NULL) {
      g_warning ("Custom quick setting '%s' not found", plugins[i]);
    } else {
      phosh_quick_settings_box_add (self->box, PHOSH_QUICK_SETTING (widget));
      g_ptr_array_add (self->custom_quick_settings, widget);
    }
  }
}


static void
phosh_quick_settings_dispose (GObject *object)
{
  PhoshQuickSettings *self = PHOSH_QUICK_SETTINGS (object);

  g_clear_object (&self->plugin_settings);
  g_clear_object (&self->plugin_loader);
  if (self->custom_quick_settings) {
    g_ptr_array_free (self->custom_quick_settings, TRUE);
    self->custom_quick_settings = NULL;
  }

  G_OBJECT_CLASS (phosh_quick_settings_parent_class)->dispose (object);
}


static void
phosh_quick_settings_class_init (PhoshQuickSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_quick_settings_dispose;

  g_type_ensure (PHOSH_TYPE_QUICK_SETTINGS_BOX);
  g_type_ensure (PHOSH_TYPE_QUICK_SETTING);

  g_type_ensure (PHOSH_TYPE_BT_STATUS_PAGE);
  g_type_ensure (PHOSH_TYPE_FEEDBACK_STATUS_PAGE);
  g_type_ensure (PHOSH_TYPE_WIFI_STATUS_PAGE);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/quick-settings.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshQuickSettings, box);

  gtk_widget_class_bind_template_callback (widget_class, on_wwan_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_wifi_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_bt_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_battery_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_rotate_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_rotate_long_pressed);
  gtk_widget_class_bind_template_callback (widget_class, on_feedback_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_torch_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_docked_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_vpn_clicked);
}


static void
phosh_quick_settings_init (PhoshQuickSettings *self)
{
  const char *plugin_dirs[] = { PHOSH_PLUGINS_DIR, NULL};

  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_bind_property (phosh_shell_get_default (), "locked",
                          self->box, "can-show-status",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);

  self->plugin_settings = g_settings_new (CUSTOM_QUICK_SETTINGS_SCHEMA);
  self->plugin_loader = phosh_plugin_loader_new ((GStrv) plugin_dirs,
                                                 PHOSH_EXTENSION_POINT_QUICK_SETTING_WIDGET);
  self->custom_quick_settings = g_ptr_array_new_with_free_func ((GDestroyNotify) unload_custom_quick_setting);

  g_signal_connect_object (self->plugin_settings, "changed::" CUSTOM_QUICK_SETTINGS_KEY,
                           G_CALLBACK (load_custom_quick_settings), self, G_CONNECT_SWAPPED);

  load_custom_quick_settings (self, NULL, NULL);
}


GtkWidget *
phosh_quick_settings_new (void)
{
  return g_object_new (PHOSH_TYPE_QUICK_SETTINGS, NULL);
}


void
phosh_quick_settings_hide_status (PhoshQuickSettings *self)
{
  g_return_if_fail (PHOSH_QUICK_SETTINGS (self));

  phosh_quick_settings_box_hide_status (self->box);
}
