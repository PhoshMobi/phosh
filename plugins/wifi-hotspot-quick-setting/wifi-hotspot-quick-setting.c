/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "wifi-hotspot-quick-setting.h"
#include "wifi-hotspot-status-page.h"
#include "plugin-shell.h"

#include <glib/gi18n.h>

/**
 * PhoshWifiHotspotQuickSetting:
 *
 * Toggle Wi-Fi hotspot.
 */

struct _PhoshWifiHotspotQuickSetting {
  PhoshQuickSetting parent;

  PhoshStatusIcon  *info;
  gboolean          connecting;
  PhoshWifiManager *wifi;
};

G_DEFINE_TYPE (PhoshWifiHotspotQuickSetting, phosh_wifi_hotspot_quick_setting,
               PHOSH_TYPE_QUICK_SETTING);


static void
on_clicked (PhoshWifiHotspotQuickSetting *self)
{
  gboolean active;

  self->connecting = TRUE;
  active = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));
  phosh_wifi_manager_set_hotspot_master (self->wifi, !active);
}


static void
update_sensitivity_cb (PhoshWifiHotspotQuickSetting *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  gboolean sensitive;

  if (phosh_shell_get_locked (shell)) {
    gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);
    return;
  }

  sensitive = phosh_wifi_manager_get_enabled (self->wifi);
  gtk_widget_set_sensitive (GTK_WIDGET (self), sensitive);
}


static void
update_info_cb (PhoshWifiHotspotQuickSetting *self)
{
  gboolean wifi_enabled;
  gboolean hotspot_enabled;
  const char *info;
  const char *icon_name;
  NMActiveConnectionState state;

  wifi_enabled = phosh_wifi_manager_get_enabled (self->wifi);
  hotspot_enabled = phosh_wifi_manager_is_hotspot_master (self->wifi);
  state = phosh_wifi_manager_get_state (self->wifi);

  g_debug ("State: %d, Hotspot: %d Wi-Fi: %d", state, hotspot_enabled, wifi_enabled);

  /* Translators: A Wi-Fi hotspot */
  if (hotspot_enabled)
    info = _("Hotspot On");
  else
    info = _("Hotspot Off");

  /* Show the acquiring icon only if the state change occurred
   * through user clicking the quick setting */
  if (self->connecting &&
      (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATING ||
       state == NM_ACTIVE_CONNECTION_STATE_DEACTIVATING)) {
    icon_name = "network-wireless-hotspot-acquiring-symbolic";
  } else {
    if (hotspot_enabled)
      icon_name = "network-wireless-hotspot-symbolic";
    else
      icon_name = "network-wireless-hotspot-disabled-symbolic";
    self->connecting = FALSE;
  }

  phosh_status_icon_set_info (PHOSH_STATUS_ICON (self->info), info);
  phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self->info), icon_name);
}


static void
phosh_wifi_hotspot_quick_setting_class_init (PhoshWifiHotspotQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (PHOSH_TYPE_WIFI_HOTSPOT_STATUS_PAGE);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/wifi-hotspot-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_wifi_hotspot_quick_setting_init (PhoshWifiHotspotQuickSetting *self)
{
  g_autoptr (GtkCssProvider) css_provider = NULL;
  PhoshShell *shell = phosh_shell_get_default ();

  gtk_widget_init_template (GTK_WIDGET (self));

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider,
                                       "/mobi/phosh/plugins/wifi-hotspot-quick-setting/style.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/wifi-hotspot-quick-setting/icons");

  self->wifi = phosh_shell_get_wifi_manager (shell);
  if (self->wifi == NULL) {
    g_warning ("Failed to get Wi-Fi manager");
    return;
  }

  g_object_bind_property (self->wifi, "is-hotspot-master",
                          self, "active",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_signal_connect_object (shell,
                           "notify::locked",
                           G_CALLBACK (update_sensitivity_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->wifi,
                           "notify::enabled",
                           G_CALLBACK (update_sensitivity_cb),
                           self,
                           G_CONNECT_SWAPPED);
  update_sensitivity_cb (self);

  g_signal_connect_object (self->wifi,
                           "notify::state",
                           G_CALLBACK (update_info_cb),
                           self,
                           G_CONNECT_SWAPPED);
  update_info_cb (self);
}
