/*
 * Copyright (C) 2018 Purism SPC
 *               2023-2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Once based on maynard's panel which is
 * Copyright (C) 2014 Collabora Ltd. *
 * Author: Jonny Lamb <jonny.lamb@collabora.co.uk>
 */

#define G_LOG_DOMAIN "phosh-shell"

#include <stdlib.h>
#include <string.h>

#include <call-ui.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>

#include "phosh-config.h"
#include "ambient.h"
#include "background.h"
#include "brightness-manager.h"
#include "drag-surface.h"
#include "debug-control.h"
#include "shell-priv.h"
#include "app-tracker.h"
#include "battery-info.h"
#include "background-manager.h"
#include "bt-info.h"
#include "bt-manager.h"
#include "connectivity-info.h"
#include "connectivity-manager.h"
#include "calls-manager.h"
#include "cell-broadcast-manager.h"
#include "docked-info.h"
#include "docked-manager.h"
#include "emergency-calls-manager.h"
#include "fader.h"
#include "feedbackinfo.h"
#include "feedback-manager.h"
#include "gnome-shell-manager.h"
#include "gtk-mount-manager.h"
#include "hks-info.h"
#include "home.h"
#include "idle-manager.h"
#include "keyboard-events.h"
#include "launcher-entry-manager.h"
#include "layersurface-priv.h"
#include "location-info.h"
#include "layout-manager.h"
#include "location-manager.h"
#include "lockscreen-manager-priv.h"
#include "default-media-player.h"
#include "mode-manager.h"
#include "monitor-manager.h"
#include "monitor/monitor.h"
#include "mount-manager.h"
#include "osd-window.h"
#include "power-menu-manager.h"
#include "revealer.h"
#include "settings.h"
#include "system-modal-dialog.h"
#include "mpris-manager.h"
#include "network-auth-manager.h"
#include "notifications/notify-manager.h"
#include "notifications/notification-banner.h"
#include "osk-manager.h"
#include "password-entry.h"
#include "phosh-private-client-protocol.h"
#include "phosh-wayland.h"
#include "plugin-loader.h"
#include "polkit-auth-agent.h"
#include "portal-access-manager.h"
#include "proximity.h"
#include "quick-setting.h"
#include "run-command-manager.h"
#include "rotateinfo.h"
#include "rotation-manager.h"
#include "sensor-proxy-manager.h"
#include "screen-saver-manager.h"
#include "screenshot-manager.h"
#include "session-manager.h"
#include "splash-manager.h"
#include "style-manager.h"
#include "suspend-manager.h"
#include "system-prompter.h"
#include "top-panel.h"
#include "top-panel-bg.h"
#include "torch-manager.h"
#include "torch-info.h"
#include "udev-manager.h"
#include "util.h"
#include "vpn-info.h"
#include "wifi-info.h"
#include "wwan-info.h"
#include "wwan/phosh-wwan-ofono.h"
#include "wwan/phosh-wwan-mm.h"
#include "wall-clock.h"

#include "phosh-settings-enums.h"

#define WWAN_BACKEND_KEY "wwan-backend"
#define OSD_HIDE_TIMEOUT 1 /* seconds */

/**
 * PhoshShell:
 *
 * The shell singleton
 *
 * #PhoshShell is responsible for instantiating the GUI
 * parts of the shell#PhoshTopPanel, #PhoshHome,… and the managers that
 * interface with DBus #PhoshMonitorManager, #PhoshFeedbackManager, …
 * and coordinates between them.
 */

enum {
  PROP_0,
  PROP_LOCKED,
  PROP_DOCKED,
  PROP_BUILTIN_MONITOR,
  PROP_PRIMARY_MONITOR,
  PROP_SHELL_STATE,
  PROP_OVERVIEW_VISIBLE,
  PROP_LOG_DOMAINS,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  READY,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

static PhoshShellDebugFlags debug_flags;

typedef struct {
  PhoshDragSurface           *top_panel;
  PhoshDragSurface           *home;
  gboolean                    overview_visible;
  GPtrArray                  *faders; /* for final fade out */
  GStrv log_domains;

  PhoshOsdWindow             *osd;
  gint osd_timeoutid;
  gboolean                    osd_continue;

  GtkWidget                  *notification_banner;

  PhoshUdevManager           *udev_manager;
  PhoshAppTracker            *app_tracker;
  PhoshSessionManager        *session_manager;
  PhoshBackgroundManager     *background_manager;
  PhoshCallsManager          *calls_manager;
  PhoshMonitor               *primary_monitor;
  PhoshMonitor               *builtin_monitor;
  PhoshMonitorManager        *monitor_manager;
  PhoshLockscreenManager     *lockscreen_manager;
  PhoshIdleManager           *idle_manager;
  PhoshOskManager            *osk_manager;
  PhoshToplevelManager       *toplevel_manager;
  PhoshWifiManager           *wifi_manager;
  PhoshPolkitAuthAgent       *polkit_auth_agent;
  PhoshScreenSaverManager    *screen_saver_manager;
  PhoshScreenshotManager     *screenshot_manager;
  PhoshNotifyManager         *notify_manager;
  PhoshFeedbackManager       *feedback_manager;
  PhoshBatteryManager        *battery_manager;
  PhoshBtManager             *bt_manager;
  PhoshMountManager          *mount_manager;
  PhoshWWan                  *wwan;
  PhoshTorchManager          *torch_manager;
  PhoshModeManager           *mode_manager;
  PhoshDockedManager         *docked_manager;
  PhoshGtkMountManager       *gtk_mount_manager;
  PhoshHksManager            *hks_manager;
  PhoshKeyboardEvents        *keyboard_events;
  PhoshLocationManager       *location_manager;
  PhoshGnomeShellManager     *gnome_shell_manager;
  PhoshSplashManager         *splash_manager;
  PhoshRunCommandManager     *run_command_manager;
  PhoshNetworkAuthManager    *network_auth_manager;
  PhoshVpnManager            *vpn_manager;
  PhoshPortalAccessManager   *portal_access_manager;
  PhoshSuspendManager        *suspend_manager;
  PhoshEmergencyCallsManager *emergency_calls_manager;
  PhoshPowerMenuManager      *power_menu_manager;
  PhoshLayoutManager         *layout_manager;
  PhoshStyleManager          *style_manager;
  PhoshLauncherEntryManager  *launcher_entry_manager;
  PhoshCellBroadcastManager  *cell_broadcast_manager;
  PhoshConnectivityManager   *connectivity_manager;
  PhoshMprisManager          *mpris_manager;
  PhoshBrightnessManager     *brightness_manager;
  PhoshDebugControl          *debug_control;

  /* sensors */
  PhoshSensorProxyManager    *sensor_proxy_manager;
  PhoshProximity             *proximity;
  PhoshAmbient               *ambient;
  PhoshRotationManager       *rotation_manager;

  gboolean                    startup_finished;
  guint startup_finished_id;

  GSimpleActionGroup         *action_map;

  /* Mirrors PhoshLockscreenManager's locked property */
  gboolean                    locked;

  /* Mirrors PhoshDockedManager's docked property */
  gboolean                    docked;

  PhoshShellStateFlags        shell_state;

  GSettings                  *settings;
} PhoshShellPrivate;

static void phosh_shell_action_group_iface_init (GActionGroupInterface *iface);
static void phosh_shell_action_map_iface_init (GActionMapInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshShell, phosh_shell, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhoshShell)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_GROUP, phosh_shell_action_group_iface_init)
                         G_IMPLEMENT_INTERFACE (G_TYPE_ACTION_MAP, phosh_shell_action_map_iface_init)
  )

static void
on_top_panel_activated (PhoshShell    *self,
                        PhoshTopPanel *window)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_TOP_PANEL (priv->top_panel));
  phosh_top_panel_toggle_fold (PHOSH_TOP_PANEL (priv->top_panel));
}


static void
update_top_level_layer (PhoshShell *self)
{
  PhoshShellStateFlags state;
  PhoshShellPrivate *priv;
  guint32 layer, current;
  gboolean use_top_layer;

  priv = phosh_shell_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  if (priv->top_panel == NULL)
    return;

  g_return_if_fail (PHOSH_IS_TOP_PANEL (priv->top_panel));
  state = phosh_shell_get_state (self);

  /* When the proximity fader is on we want to remove the top-panel from the
     overlay layer since it uses an exclusive zone and hence the fader is
     drawn below that top-panel. This can be dropped once layer-shell allows
     to specify the z-level */
  use_top_layer = priv->proximity && phosh_proximity_has_fader (priv->proximity);
  if (use_top_layer)
    goto set_layer;

  /* We want the top-bar on the lock screen */
  use_top_layer = !phosh_shell_get_locked (self);
  if (use_top_layer)
    goto set_layer;

  /* If there's a modal dialog make sure it can extend over the top-panel */
  use_top_layer = !!(state & PHOSH_STATE_MODAL_SYSTEM_PROMPT);

 set_layer:
  layer = use_top_layer ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  g_object_get (priv->top_panel, "layer", &current, NULL);
  if (current == layer)
    return;

  g_debug ("Moving top-panel to %s layer", use_top_layer ? "top" : "overlay");
  phosh_top_panel_set_layer (PHOSH_TOP_PANEL (priv->top_panel), layer);
}


static void
on_proximity_fader_changed (PhoshShell *self)
{
  update_top_level_layer (self);
}


static void
on_top_panel_state_changed (PhoshShell *self, GParamSpec *pspec, PhoshTopPanel *top_panel)
{
  PhoshShellPrivate *priv;
  PhoshTopPanelState state;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_TOP_PANEL (top_panel));

  priv = phosh_shell_get_instance_private (self);

  state = phosh_top_panel_get_state (PHOSH_TOP_PANEL (priv->top_panel));
  phosh_shell_set_state (self, PHOSH_STATE_SETTINGS, state == PHOSH_TOP_PANEL_STATE_UNFOLDED);
}

static void
update_top_bar_transparency (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);
  PhoshHomeState state;

  if (!priv->top_panel)
    return;

  if (priv->locked) {
    phosh_top_panel_set_bar_transparent (PHOSH_TOP_PANEL (priv->top_panel), TRUE);
    return;
  }

  state = phosh_home_get_state (PHOSH_HOME (priv->home));
  phosh_top_panel_set_bar_transparent (PHOSH_TOP_PANEL (priv->top_panel),
                                       (state != PHOSH_HOME_STATE_FOLDED));
}


static void
on_home_state_changed (PhoshShell *self, GParamSpec *pspec, PhoshHome *home)
{
  PhoshShellPrivate *priv;
  PhoshHomeState state;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_HOME (home));
  priv = phosh_shell_get_instance_private (self);

  update_top_bar_transparency (self);

  state = phosh_home_get_state (PHOSH_HOME (priv->home));
  phosh_shell_set_state (self, PHOSH_STATE_OVERVIEW, state == PHOSH_HOME_STATE_UNFOLDED);
}


static void
on_primary_monitor_power_mode_changed (PhoshShell *self, GParamSpec *pspec, PhoshMonitor *monitor)
{
  PhoshMonitorPowerSaveMode mode;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));

  g_object_get (monitor, "power-mode", &mode, NULL);

  phosh_shell_set_state (self, PHOSH_STATE_BLANKED, mode == PHOSH_MONITOR_POWER_SAVE_MODE_OFF);
}


static void
on_primary_monitor_configured (PhoshShell *self, PhoshMonitor *monitor)
{
  PhoshShellPrivate *priv;
  int height;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  priv = phosh_shell_get_instance_private (self);

  phosh_shell_get_area (self, NULL, &height);
  phosh_layer_surface_set_size (PHOSH_LAYER_SURFACE (priv->top_panel), -1, height);
}


static void
setup_primary_monitor_signal_handlers (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->primary_monitor)
    return;

  g_signal_connect_swapped (priv->primary_monitor,
                            "notify::power-mode",
                            G_CALLBACK (on_primary_monitor_power_mode_changed),
                            self);

  g_signal_connect_object (priv->primary_monitor, "configured",
                           G_CALLBACK (on_primary_monitor_configured),
                           self,
                           G_CONNECT_SWAPPED);

  if (phosh_monitor_is_configured (priv->primary_monitor))
    on_primary_monitor_configured (self, priv->primary_monitor);
}


static void
panels_create (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);
  PhoshWayland *wl = phosh_wayland_get_default ();
  PhoshMonitor *monitor;
  PhoshAppGrid *app_grid;
  guint32 top_layer;

  monitor = phosh_shell_get_primary_monitor (self);
  g_return_if_fail (monitor);

  top_layer = priv->locked ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_TOP;

  /* Top panel */
  priv->top_panel = PHOSH_DRAG_SURFACE (phosh_top_panel_new (
                                          phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                                          phosh_wayland_get_zphoc_layer_shell_effects_v1 (wl),
                                          monitor,
                                          top_layer));
  gtk_widget_set_visible (GTK_WIDGET (priv->top_panel), TRUE);

  /* Home is created after the top-panel so it honors its exclusive zone */
  priv->home = PHOSH_DRAG_SURFACE (phosh_home_new (phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                                                   phosh_wayland_get_zphoc_layer_shell_effects_v1 (wl),
                                                   monitor));
  g_object_bind_property (self, "overview-visible", priv->home, "visible", G_BINDING_SYNC_CREATE);

  g_signal_connect_swapped (priv->top_panel,
                            "activated",
                            G_CALLBACK (on_top_panel_activated),
                            self);

  g_signal_connect_swapped (priv->top_panel,
                            "notify::state",
                            G_CALLBACK (on_top_panel_state_changed),
                            self);

  g_signal_connect_swapped (priv->home,
                            "notify::state",
                            G_CALLBACK (on_home_state_changed),
                            self);

  app_grid = phosh_overview_get_app_grid (phosh_home_get_overview (PHOSH_HOME (priv->home)));
  g_object_bind_property (priv->docked_manager,
                          "enabled",
                          app_grid,
                          "filter-adaptive",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
}


static void
panels_dispose (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_clear_pointer (&priv->top_panel, phosh_cp_widget_destroy);
  g_clear_pointer (&priv->home, phosh_cp_widget_destroy);
}


static void
set_locked (PhoshShell *self, gboolean locked)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (priv->locked == locked)
    return;

  priv->locked = locked;
  phosh_shell_set_state (self, PHOSH_STATE_LOCKED, priv->locked);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LOCKED]);

  /* Hide settings on screen lock, otherwise the user just sees the settings when
     unblanking the screen which can be confusing */
  if (priv->top_panel)
    phosh_top_panel_fold (PHOSH_TOP_PANEL (priv->top_panel));

  update_top_level_layer (self);
  update_top_bar_transparency (self);
}


static void
phosh_shell_set_log_domains (PhoshShell *self, const char *const *log_domains)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_SHELL (self));

  if (!priv->log_domains && !log_domains)
    return;

  if (priv->log_domains && log_domains &&
      g_strv_equal ((const char *const *)priv->log_domains, log_domains))
    return;

  g_strfreev (priv->log_domains);
  priv->log_domains = g_strdupv ((GStrv)log_domains);

  g_log_writer_default_set_debug_domains (log_domains);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LOG_DOMAINS]);
}


static void
phosh_shell_set_property (GObject      *object,
                          guint         property_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  PhoshShell *self = PHOSH_SHELL (object);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  switch (property_id) {
  case PROP_LOCKED:
    /* Only written by lockscreen manager on property sync */
    set_locked (self, g_value_get_boolean (value));
    break;
  case PROP_DOCKED:
    /* Only written by docked manager on property sync */
    priv->docked = g_value_get_boolean (value);
    break;
  case PROP_PRIMARY_MONITOR:
    phosh_shell_set_primary_monitor (self, g_value_get_object (value));
    break;
  case PROP_OVERVIEW_VISIBLE:
    priv->overview_visible = g_value_get_boolean (value);
    break;
  case PROP_LOG_DOMAINS:
    phosh_shell_set_log_domains (self, g_value_get_boxed (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_shell_get_property (GObject    *object,
                          guint       property_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  PhoshShell *self = PHOSH_SHELL (object);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  switch (property_id) {
  case PROP_LOCKED:
    g_value_set_boolean (value, phosh_shell_get_locked (self));
    break;
  case PROP_DOCKED:
    g_value_set_boolean (value, phosh_shell_get_docked (self));
    break;
  case PROP_BUILTIN_MONITOR:
    g_value_set_object (value, phosh_shell_get_builtin_monitor (self));
    break;
  case PROP_PRIMARY_MONITOR:
    g_value_set_object (value, phosh_shell_get_primary_monitor (self));
    break;
  case PROP_SHELL_STATE:
    g_value_set_flags (value, priv->shell_state);
    break;
  case PROP_OVERVIEW_VISIBLE:
    g_value_set_boolean (value, priv->overview_visible);
    break;
  case PROP_LOG_DOMAINS:
    g_value_set_boxed (value, priv->log_domains);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_shell_dispose (GObject *object)
{
  PhoshShell *self = PHOSH_SHELL (object);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_clear_handle_id (&priv->startup_finished_id, g_source_remove);

  panels_dispose (self);
  g_clear_pointer (&priv->faders, g_ptr_array_unref);

  g_clear_pointer (&priv->notification_banner, phosh_cp_widget_destroy);

  /* dispose managers in opposite order of declaration */
  g_clear_object (&priv->debug_control);
  g_clear_object (&priv->brightness_manager);
  g_clear_object (&priv->mpris_manager);
  g_clear_object (&priv->connectivity_manager);
  g_clear_object (&priv->cell_broadcast_manager);
  g_clear_object (&priv->launcher_entry_manager);
  g_clear_object (&priv->power_menu_manager);
  g_clear_object (&priv->emergency_calls_manager);
  g_clear_object (&priv->portal_access_manager);
  g_clear_object (&priv->vpn_manager);
  g_clear_object (&priv->network_auth_manager);
  g_clear_object (&priv->run_command_manager);
  g_clear_object (&priv->splash_manager);
  g_clear_object (&priv->screenshot_manager);
  g_clear_object (&priv->calls_manager);
  g_clear_object (&priv->location_manager);
  g_clear_object (&priv->hks_manager);
  g_clear_object (&priv->gtk_mount_manager);
  g_clear_object (&priv->docked_manager);
  g_clear_object (&priv->mode_manager);
  g_clear_object (&priv->torch_manager);
  g_clear_object (&priv->gnome_shell_manager);
  g_clear_object (&priv->wwan);
  g_clear_object (&priv->mount_manager);
  g_clear_object (&priv->battery_manager);
  g_clear_object (&priv->bt_manager);
  g_clear_object (&priv->feedback_manager);
  g_clear_object (&priv->notify_manager);
  g_clear_object (&priv->screen_saver_manager);
  g_clear_object (&priv->polkit_auth_agent);
  g_clear_object (&priv->wifi_manager);
  g_clear_object (&priv->toplevel_manager);
  g_clear_object (&priv->osk_manager);
  g_clear_object (&priv->idle_manager);
  g_clear_object (&priv->lockscreen_manager);
  g_clear_object (&priv->monitor_manager);
  g_clear_object (&priv->builtin_monitor);
  g_clear_object (&priv->primary_monitor);
  g_clear_object (&priv->background_manager);
  g_clear_object (&priv->keyboard_events);
  g_clear_object (&priv->app_tracker);
  g_clear_object (&priv->suspend_manager);
  g_clear_object (&priv->layout_manager);
  g_clear_object (&priv->style_manager);
  g_clear_object (&priv->udev_manager);

  /* sensors */
  g_clear_object (&priv->proximity);
  g_clear_object (&priv->rotation_manager);
  g_clear_object (&priv->sensor_proxy_manager);

  phosh_system_prompter_unregister ();
  g_clear_object (&priv->session_manager);

  g_clear_object (&priv->action_map);
  g_clear_object (&priv->settings);

  G_OBJECT_CLASS (phosh_shell_parent_class)->dispose (object);
}


static void
phosh_shell_finalize (GObject *object)
{
  PhoshShell *self = PHOSH_SHELL (object);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_clear_pointer (&priv->log_domains, g_strfreev);

  cui_uninit ();
  G_OBJECT_CLASS (phosh_shell_parent_class)->finalize (object);
}


static void
on_num_toplevels_changed (PhoshShell           *self,
                          GParamSpec           *pspec,
                          PhoshToplevelManager *toplevel_manager)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (toplevel_manager));

  priv = phosh_shell_get_instance_private (self);
  /* all toplevels gone, show the overview */
  if (!phosh_toplevel_manager_get_num_toplevels (toplevel_manager))
    phosh_home_set_state (PHOSH_HOME (priv->home), PHOSH_HOME_STATE_UNFOLDED);
}


static void
on_toplevel_added (PhoshShell *self, PhoshToplevel *unused, PhoshToplevelManager *toplevel_manager)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (toplevel_manager));

  priv = phosh_shell_get_instance_private (self);
  if (phosh_toplevel_manager_get_num_toplevels (toplevel_manager) == 1)
    phosh_home_set_state (PHOSH_HOME (priv->home), PHOSH_HOME_STATE_FOLDED);
}


static void
on_new_notification (PhoshShell         *self,
                     PhoshNotification  *notification,
                     PhoshNotifyManager *manager)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_NOTIFICATION (notification));
  g_return_if_fail (PHOSH_IS_NOTIFY_MANAGER (manager));

  priv = phosh_shell_get_instance_private (self);

  /* Clear existing banner */
  g_clear_pointer (&priv->notification_banner, phosh_cp_widget_destroy);
  if (phosh_notify_manager_get_show_notification_banner (manager, notification) &&
      phosh_top_panel_get_state (PHOSH_TOP_PANEL (priv->top_panel)) == PHOSH_TOP_PANEL_STATE_FOLDED &&
      !priv->locked) {
    priv->notification_banner = phosh_notification_banner_new (notification);
    g_signal_connect (priv->notification_banner,
                      "destroy",
                      G_CALLBACK (gtk_widget_destroyed),
                      &priv->notification_banner);

    gtk_widget_set_visible (GTK_WIDGET (priv->notification_banner), TRUE);
  }
}


static void
on_pb_long_press (PhoshShell *self)
{
  g_return_if_fail (PHOSH_IS_SHELL (self));

  g_action_group_activate_action (G_ACTION_GROUP (self), "power.toggle-menu", NULL);
}


static void
on_notification_activated (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));

  priv = phosh_shell_get_instance_private (self);

  phosh_top_panel_fold (PHOSH_TOP_PANEL (priv->top_panel));
}


static gboolean
on_fade_out_timeout (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), G_SOURCE_REMOVE);

  priv = phosh_shell_get_instance_private (self);

  /* kill all faders if we time out */
  priv->faders = g_ptr_array_remove_range (priv->faders, 0, priv->faders->len);

  return G_SOURCE_REMOVE;
}


/* {{{ OSD */

static gboolean
on_osd_timeout (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);
  gboolean ret;

  ret = priv->osd_continue ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
  if (!priv->osd_continue) {
    g_debug ("Closing osd");
    priv->osd_timeoutid = 0;
    if (priv->osd)
      gtk_widget_destroy (GTK_WIDGET (priv->osd));
  }
  priv->osd_continue = FALSE;
  return ret;
}


static void
on_osd_destroyed (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  priv->osd = NULL;
  g_clear_handle_id (&priv->osd_timeoutid, g_source_remove);
}

/* }}} */

static void
notify_compositor_up_state (PhoshShell *self, enum phosh_private_shell_state state)
{
  struct phosh_private *phosh_private;

  g_debug ("Notify compositor state: %d", state);

  phosh_private = phosh_wayland_get_phosh_private (phosh_wayland_get_default ());
  if (phosh_private &&
      phosh_private_get_version (phosh_private) >= PHOSH_PRIVATE_SET_SHELL_STATE_SINCE_VERSION)
    phosh_private_set_shell_state (phosh_private, state);
}


static void
on_startup_finished (gpointer data)
{
  PhoshShell *self = PHOSH_SHELL (data);
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  notify_compositor_up_state (self, PHOSH_PRIVATE_SHELL_STATE_UP);

  priv->startup_finished_id = 0;
}


static gboolean
setup_idle_cb (PhoshShell *self)
{
  g_autoptr (GError) err = NULL;
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  priv->debug_control = phosh_debug_control_new ();
  priv->app_tracker = phosh_app_tracker_new ();
  priv->splash_manager = phosh_splash_manager_new (priv->app_tracker);
  priv->session_manager = phosh_session_manager_new ();
  priv->mode_manager = phosh_mode_manager_new ();
  priv->wifi_manager = phosh_wifi_manager_new ();
  /* Connecivity manager needs Wi-Fi manager: */
  priv->connectivity_manager = phosh_connectivity_manager_new ();

  priv->sensor_proxy_manager = phosh_sensor_proxy_manager_new (&err);
  if (priv->sensor_proxy_manager)
    priv->ambient = phosh_ambient_new (priv->sensor_proxy_manager);
  else
    g_message ("Failed to connect to sensor-proxy: %s", err->message);

  priv->layout_manager = phosh_layout_manager_new ();
  /* PhoshHome needs the background manager */
  priv->background_manager = phosh_background_manager_new ();
  panels_create (self);

  g_signal_connect_object (priv->toplevel_manager,
                           "notify::num-toplevels",
                           G_CALLBACK (on_num_toplevels_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_num_toplevels_changed (self, NULL, priv->toplevel_manager);

  g_signal_connect_object (priv->toplevel_manager,
                           "toplevel-added",
                           G_CALLBACK (on_toplevel_added),
                           self,
                           G_CONNECT_SWAPPED);

  /* Screen saver manager needs lock screen manager */
  priv->screen_saver_manager = phosh_screen_saver_manager_new (priv->lockscreen_manager);
  g_signal_connect_swapped (priv->screen_saver_manager,
                            "pb-long-press",
                            G_CALLBACK (on_pb_long_press),
                            self);

  priv->notify_manager = phosh_notify_manager_get_default ();
  g_signal_connect_object (priv->notify_manager,
                           "new-notification",
                           G_CALLBACK (on_new_notification),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (priv->notify_manager,
                           "notification-activated",
                           G_CALLBACK (on_notification_activated),
                           self,
                           G_CONNECT_SWAPPED);

  phosh_shell_get_location_manager (self);
  if (priv->sensor_proxy_manager) {
    priv->proximity = phosh_proximity_new (priv->sensor_proxy_manager,
                                           priv->calls_manager);
    phosh_monitor_manager_set_sensor_proxy_manager (priv->monitor_manager,
                                                    priv->sensor_proxy_manager);
    g_signal_connect_swapped (priv->proximity, "notify::fader",
                              G_CALLBACK (on_proximity_fader_changed), self);
  }

  priv->mount_manager = phosh_mount_manager_new ();
  priv->gtk_mount_manager = phosh_gtk_mount_manager_new ();

  phosh_session_manager_register (priv->session_manager,
                                  PHOSH_APP_ID,
                                  g_getenv ("DESKTOP_AUTOSTART_ID"));
  g_unsetenv ("DESKTOP_AUTOSTART_ID");

  priv->gnome_shell_manager = phosh_gnome_shell_manager_get_default ();
  priv->screenshot_manager = phosh_screenshot_manager_new ();
  priv->run_command_manager = phosh_run_command_manager_new ();
  priv->network_auth_manager = phosh_network_auth_manager_new ();
  priv->portal_access_manager = phosh_portal_access_manager_new ();
  priv->suspend_manager = phosh_suspend_manager_new ();
  priv->emergency_calls_manager = phosh_emergency_calls_manager_new ();
  priv->power_menu_manager = phosh_power_menu_manager_new ();
  priv->cell_broadcast_manager = phosh_cell_broadcast_manager_new ();

  setup_primary_monitor_signal_handlers (self);
  /* Setup event hooks late so state changes in UI files don't trigger feedback */
  phosh_feedback_manager_setup_event_hooks (priv->feedback_manager);

  /* Export the debug interface late so everything is up when the name appears */
  phosh_debug_control_set_exported (priv->debug_control, TRUE);

  /* Delay signaling to the compositor a bit so that idle handlers get a chance to run and
     the user can unlock right away. Ideally we'd not need this */
  priv->startup_finished_id = g_timeout_add_seconds_once (1, on_startup_finished, self);
  g_source_set_name_by_id (priv->startup_finished_id, "[PhoshShell] startup finished");

  priv->startup_finished = TRUE;
  g_signal_emit (self, signals[READY], 0);

  return FALSE;
}


/* Load all types that might be used in UI files */
static void
type_setup (void)
{
  g_type_ensure (PHOSH_TYPE_BATTERY_INFO);
  g_type_ensure (PHOSH_TYPE_BT_INFO);
  g_type_ensure (PHOSH_TYPE_CONNECTIVITY_INFO);
  g_type_ensure (PHOSH_TYPE_DOCKED_INFO);
  g_type_ensure (PHOSH_TYPE_FEEDBACK_INFO);
  g_type_ensure (PHOSH_TYPE_HKS_INFO);
  g_type_ensure (PHOSH_TYPE_LOCATION_INFO);
  g_type_ensure (PHOSH_TYPE_DEFAULT_MEDIA_PLAYER);
  g_type_ensure (PHOSH_TYPE_PASSWORD_ENTRY);
  g_type_ensure (PHOSH_TYPE_QUICK_SETTING);
  g_type_ensure (PHOSH_TYPE_REVEALER);
  g_type_ensure (PHOSH_TYPE_ROTATE_INFO);
  g_type_ensure (PHOSH_TYPE_SETTINGS);
  g_type_ensure (PHOSH_TYPE_SYSTEM_MODAL);
  g_type_ensure (PHOSH_TYPE_SYSTEM_MODAL_DIALOG);
  g_type_ensure (PHOSH_TYPE_TORCH_INFO);
  g_type_ensure (PHOSH_TYPE_VPN_INFO);
  g_type_ensure (PHOSH_TYPE_WIFI_INFO);
  g_type_ensure (PHOSH_TYPE_WWAN_INFO);
}


static void
phosh_shell_set_builtin_monitor (PhoshShell *self, PhoshMonitor *monitor)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor) || monitor == NULL);
  priv = phosh_shell_get_instance_private (self);

  if (priv->builtin_monitor == monitor)
    return;

  if (priv->builtin_monitor) {
    g_clear_object (&priv->builtin_monitor);

    if (priv->rotation_manager)
      phosh_rotation_manager_set_monitor (priv->rotation_manager, NULL);
  }

  g_debug ("New builtin monitor is %s", monitor ? monitor->name : "(none)");
  g_set_object (&priv->builtin_monitor, monitor);

  if (monitor) {
    if (priv->rotation_manager)
      phosh_rotation_manager_set_monitor (priv->rotation_manager, monitor);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BUILTIN_MONITOR]);
}


/* Find a new builtin monitor that differs from old, otherwise NULL */
static PhoshMonitor *
find_new_builtin_monitor (PhoshShell *self, PhoshMonitor *old)
{
  PhoshShellPrivate *priv;
  PhoshMonitor *monitor = NULL;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  for (int i = 0; i < phosh_monitor_manager_get_num_monitors (priv->monitor_manager); i++) {
    PhoshMonitor *tmp = phosh_monitor_manager_get_monitor (priv->monitor_manager, i);
    if (phosh_monitor_is_builtin (tmp) && tmp != old) {
      monitor = tmp;
      break;
    }
  }

  return monitor;
}


static void
on_monitor_added (PhoshShell *self, PhoshMonitor *monitor)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  priv = phosh_shell_get_instance_private (self);

  g_debug ("Monitor %p (%s) added", monitor, monitor->name);

  /* Set built-in monitor if not set already */
  if (!priv->builtin_monitor && phosh_monitor_is_builtin (monitor))
    phosh_shell_set_builtin_monitor (self, monitor);

  /*
   * on_monitor_added() gets connected in phosh_shell_constructed() but
   * we can't use phosh_shell_set_primary_monitor() yet since the
   * shell object is not yet up and we can't move panels, etc. so
   * ignore that case. This is not a problem since phosh_shell_constructed()
   * sets the primary monitor explicitly.
   */
  if (!priv->startup_finished)
    return;

  /* Set primary monitor if unset */
  if (priv->primary_monitor == NULL)
    phosh_shell_set_primary_monitor (self, monitor);
}


static void
on_monitor_removed (PhoshShell *self, PhoshMonitor *monitor)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));
  priv = phosh_shell_get_instance_private (self);

  if (priv->builtin_monitor == monitor) {
    PhoshMonitor *new_builtin;

    g_debug ("Builtin monitor %p (%s) removed", monitor, monitor->name);

    new_builtin = find_new_builtin_monitor (self, monitor);
    phosh_shell_set_builtin_monitor (self, new_builtin);
  }

  if (priv->primary_monitor == monitor) {
    g_debug ("Primary monitor %p (%s) removed", monitor, monitor->name);

    /* Prefer built in monitor when primary is gone... */
    if (priv->builtin_monitor) {
      phosh_shell_set_primary_monitor (self, priv->builtin_monitor);
      return;
    }

    /* ...just pick the first one available otherwise */
    for (int i = 0; i < phosh_monitor_manager_get_num_monitors (priv->monitor_manager); i++) {
      PhoshMonitor *new_primary = phosh_monitor_manager_get_monitor (priv->monitor_manager, i);
      if (new_primary != monitor) {
        phosh_shell_set_primary_monitor (self, new_primary);
        return;
      }
    }

    /* We did not find another monitor so all monitors are gone */
    phosh_shell_set_primary_monitor (self, NULL);
  }
}


static void
on_keyboard_events_pressed (PhoshShell *self, const char *combo)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!phosh_calls_manager_has_incoming_call (priv->calls_manager))
    return;

  if (!g_settings_get_boolean (priv->settings, "quick-silent"))
    return;

  if (g_strcmp0 (combo, "XF86AudioLowerVolume"))
    return;

  g_debug ("Vol down pressed, silencing device");
  phosh_feedback_manager_set_profile (priv->feedback_manager, "silent");
}


static void
phosh_shell_constructed (GObject *object)
{
  PhoshShell *self = PHOSH_SHELL (object);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);
  g_autoptr (GError) err = NULL;
  guint id;

  G_OBJECT_CLASS (phosh_shell_parent_class)->constructed (object);

  priv->monitor_manager = phosh_monitor_manager_new (NULL);
  g_signal_connect_swapped (priv->monitor_manager,
                            "monitor-added",
                            G_CALLBACK (on_monitor_added),
                            self);
  g_signal_connect_swapped (priv->monitor_manager,
                            "monitor-removed",
                            G_CALLBACK (on_monitor_removed),
                            self);

  /* Make sure all outputs are up to date */
  phosh_wayland_roundtrip (phosh_wayland_get_default ());

  if (phosh_monitor_manager_get_num_monitors (priv->monitor_manager)) {
    PhoshMonitor *monitor = find_new_builtin_monitor (self, NULL);

    /* Setup builtin monitor if not set via 'monitor-added' */
    if (!priv->builtin_monitor && monitor) {
      phosh_shell_set_builtin_monitor (self, monitor);
      g_debug ("Builtin monitor %p, configured: %d",
               priv->builtin_monitor,
               phosh_monitor_is_configured (priv->builtin_monitor));
    }

    /* Setup primary monitor, prefer builtin */
    /* Can't invoke phosh_shell_set_primary_monitor () since this involves
       updating the panels as well and we need to init more of the shell first */
    if (priv->builtin_monitor)
      priv->primary_monitor = g_object_ref (priv->builtin_monitor);
    else
      priv->primary_monitor = g_object_ref (phosh_monitor_manager_get_monitor (priv->monitor_manager, 0));
  } else {
    g_error ("Need at least one monitor");
  }

  priv->calls_manager = phosh_calls_manager_new ();
  priv->launcher_entry_manager = phosh_launcher_entry_manager_new ();

  priv->lockscreen_manager = phosh_lockscreen_manager_new (priv->calls_manager);
  g_object_bind_property (priv->lockscreen_manager, "locked",
                          self, "locked",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  priv->idle_manager = phosh_idle_manager_get_default ();

  priv->faders = g_ptr_array_new_with_free_func ((GDestroyNotify) (gtk_widget_destroy));

  phosh_system_prompter_register ();
  priv->polkit_auth_agent = phosh_polkit_auth_agent_new ();

  priv->feedback_manager = phosh_feedback_manager_new ();
  priv->keyboard_events = phosh_keyboard_events_new (&err);
  if (priv->keyboard_events) {
    g_signal_connect_swapped (priv->keyboard_events,
                              "pressed",
                              G_CALLBACK (on_keyboard_events_pressed),
                              self);
  } else {
    g_warning ("Failed to initialize keyboard events: %s", err->message);
  }

  id = g_idle_add ((GSourceFunc) setup_idle_cb, self);
  g_source_set_name_by_id (id, "[PhoshShell] idle");
}

/* {{{ Action Map/Group */

static char **
phosh_shell_list_actions (GActionGroup *group)
{
  PhoshShell *self = PHOSH_SHELL (group);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  /* may be NULL after dispose has run */
  if (!priv->action_map)
    return g_new0 (char *, 0 + 1);

  return g_action_group_list_actions (G_ACTION_GROUP (priv->action_map));
}

static gboolean
phosh_shell_query_action (GActionGroup        *group,
                          const char          *action_name,
                          gboolean            *enabled,
                          const GVariantType **parameter_type,
                          const GVariantType **state_type,
                          GVariant           **state_hint,
                          GVariant           **state)
{
  PhoshShell *self = PHOSH_SHELL (group);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return FALSE;

  return g_action_group_query_action (G_ACTION_GROUP (priv->action_map),
                                      action_name,
                                      enabled,
                                      parameter_type,
                                      state_type,
                                      state_hint,
                                      state);
}


static void
_phosh_shell_activate_action (GActionGroup *group,
                              const char   *action_name,
                              GVariant     *parameter)
{
  PhoshShell *self = PHOSH_SHELL (group);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return;

  g_action_group_activate_action (G_ACTION_GROUP (priv->action_map), action_name, parameter);
}


static void
phosh_shell_change_action_state (GActionGroup *group,
                                 const char   *action_name,
                                 GVariant     *state)
{
  PhoshShell *self = PHOSH_SHELL (group);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return;

  g_action_group_change_action_state (G_ACTION_GROUP (priv->action_map), action_name, state);
}


static void
phosh_shell_action_group_iface_init (GActionGroupInterface *iface)
{
  iface->list_actions = phosh_shell_list_actions;
  iface->query_action = phosh_shell_query_action;
  iface->activate_action = _phosh_shell_activate_action;
  iface->change_action_state = phosh_shell_change_action_state;
}


static GAction *
phosh_shell_lookup_action (GActionMap *action_map, const char *action_name)
{
  PhoshShell *self = PHOSH_SHELL (action_map);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return NULL;

  return g_action_map_lookup_action (G_ACTION_MAP (priv->action_map), action_name);
}

static void
phosh_shell_add_action (GActionMap *action_map, GAction *action)
{
  PhoshShell *self = PHOSH_SHELL (action_map);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return;

  g_action_map_add_action (G_ACTION_MAP (priv->action_map), action);
}

static void
phosh_shell_remove_action (GActionMap *action_map, const char *action_name)
{
  PhoshShell *self = PHOSH_SHELL (action_map);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  if (!priv->action_map)
    return;

  g_action_map_remove_action (G_ACTION_MAP (priv->action_map), action_name);
}


static void
phosh_shell_action_map_iface_init (GActionMapInterface *iface)
{
  iface->lookup_action = phosh_shell_lookup_action;
  iface->add_action = phosh_shell_add_action;
  iface->remove_action = phosh_shell_remove_action;
}


static GType
get_lockscreen_type (PhoshShell *self)
{
  return PHOSH_TYPE_LOCKSCREEN;
}

/* }}} */
/* {{{ GObject init */

static void
phosh_shell_class_init (PhoshShellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_shell_constructed;
  object_class->dispose = phosh_shell_dispose;
  object_class->finalize = phosh_shell_finalize;

  object_class->set_property = phosh_shell_set_property;
  object_class->get_property = phosh_shell_get_property;

  klass->get_lockscreen_type = get_lockscreen_type;

  type_setup ();

  /**
   * PhoshShell:locked:
   *
   * Whether the screen is currently locked. This mirrors the property
   * from #PhoshLockscreenManager for easier access.
   */
  props[PROP_LOCKED] =
    g_param_spec_boolean ("locked", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshShell:docked:
   *
   * Whether the device is currently docked. This mirrors the property
   * from #PhoshDockedManager for easier access.
   */
  props[PROP_DOCKED] =
    g_param_spec_boolean ("docked", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshShell:builtin-monitor:
   *
   * The built in monitor. This is a hardware property and hence can
   * only be read. It can be %NULL when not present or disabled.
   *
   * Since: 0.10.1
   */
  props[PROP_BUILTIN_MONITOR] =
    g_param_spec_object ("builtin-monitor", "", "",
                         PHOSH_TYPE_MONITOR,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshShell:primary-monitor:
   *
   * The primary monitor that has the panels, lock screen etc.
   *
   * Since: 0.0.2
   */
  props[PROP_PRIMARY_MONITOR] =
    g_param_spec_object ("primary-monitor", "", "",
                         PHOSH_TYPE_MONITOR,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshShell:shell-state:
   *
   * The state of the shell (locked, modal dialog shown, …)
   *
   * Since: 0.10.0
   */
  props[PROP_SHELL_STATE] =
    g_param_spec_flags ("shell-state", "", "",
                        PHOSH_TYPE_SHELL_STATE_FLAGS,
                        PHOSH_STATE_NONE,
                        G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshShell:overview-visible:
   *
   * Whether to display the `PhoshHome` (overview and home bar)
   */
  props[PROP_OVERVIEW_VISIBLE] =
    g_param_spec_boolean ("overview-visible", "", "",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshShell:log-domains
   *
   * The current log domains
   */
  props[PROP_LOG_DOMAINS] =
    g_param_spec_boxed ("log-domains", "", "",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);


  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshShell::ready:
   * @self: The shell object
   *
   * The ready signal is emitted once when the shell finished starting
   * up.
   *
   * Since: 0.11.0
   */
  signals[READY] = g_signal_new ("ready",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST, 0,
                                 NULL, NULL, NULL,
                                 G_TYPE_NONE, 0);
}


static GDebugKey debug_keys[] =
{
 { .key = "always-splash",
   .value = PHOSH_SHELL_DEBUG_FLAG_ALWAYS_SPLASH,
 },
 { .key = "fake-builtin",
   .value = PHOSH_SHELL_DEBUG_FLAG_FAKE_BUILTIN,
 },
 { .key = "backlight-non-linear",
   .value = PHOSH_SHELL_DEBUG_BACKLIGHT_NON_LINEAR,
 },
};


static void
phosh_shell_init (PhoshShell *self)
{
  const char *messages_debug;
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  messages_debug = g_getenv ("G_MESSAGES_DEBUG");
  if (messages_debug)
    priv->log_domains = g_strsplit (messages_debug, " ", -1);

  cui_init (TRUE);
  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (), "/mobi/phosh/icons");

  priv->overview_visible = TRUE;

  g_io_extension_point_register (PHOSH_EXTENSION_POINT_LOCKSCREEN_WIDGET);
  g_io_extension_point_register (PHOSH_EXTENSION_POINT_QUICK_SETTING_WIDGET);

  debug_flags = g_parse_debug_string (g_getenv ("PHOSH_DEBUG"),
                                      debug_keys,
                                      G_N_ELEMENTS (debug_keys));

  priv->style_manager = phosh_style_manager_new ();
  priv->shell_state = PHOSH_STATE_SETTINGS;
  priv->action_map = g_simple_action_group_new ();
  priv->settings = g_settings_new ("sm.puri.phosh");

  /* We bind this early since a wl_display_roundtrip () would make us miss
     existing toplevels */
  priv->toplevel_manager = phosh_toplevel_manager_new ();
  priv->udev_manager = phosh_udev_manager_get_default ();
}

/* }}} */

PhoshShell *
phosh_shell_new (void)
{
  return g_object_new (PHOSH_TYPE_SHELL, NULL);
}

static gboolean
select_fallback_monitor (gpointer data)
{
  PhoshShell *self = PHOSH_SHELL (data);
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MONITOR_MANAGER (priv->monitor_manager), FALSE);
  phosh_monitor_manager_enable_fallback (priv->monitor_manager);

  return G_SOURCE_REMOVE;
}

/* {{{ Public functions */

void
phosh_shell_set_primary_monitor (PhoshShell *self, PhoshMonitor *monitor)
{
  PhoshShellPrivate *priv;
  PhoshMonitor *m = NULL;
  gboolean needs_notify = FALSE;

  g_return_if_fail (PHOSH_IS_MONITOR (monitor) || monitor == NULL);
  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  if (monitor == priv->primary_monitor)
    return;

  if (priv->primary_monitor) {
    g_signal_handlers_disconnect_by_func (priv->primary_monitor,
                                          G_CALLBACK (on_primary_monitor_configured),
                                          self);
  }
  if (priv->builtin_monitor) {
    g_signal_handlers_disconnect_by_func (priv->builtin_monitor,
                                          G_CALLBACK (on_primary_monitor_power_mode_changed),
                                          self);
  }

  if (monitor != NULL) {
    /* Make sure the new monitor exists */
    for (int i = 0; i < phosh_monitor_manager_get_num_monitors (priv->monitor_manager); i++) {
      m = phosh_monitor_manager_get_monitor (priv->monitor_manager, i);
      if (monitor == m)
        break;
    }
    g_return_if_fail (monitor == m);
  }

  needs_notify = priv->primary_monitor == NULL;
  g_set_object (&priv->primary_monitor, monitor);
  g_debug ("New primary monitor is %s", monitor ? monitor->name : "(none)");

  /* Move panels to the new monitor by recreating the layer-shell surfaces */
  panels_dispose (self);
  if (monitor)
    panels_create (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRIMARY_MONITOR]);

  setup_primary_monitor_signal_handlers (self);

  /* All monitors gone or disabled. See if monitor-manager finds a
   * fallback to enable. Do that in an idle callback so GTK can process
   * pending wayland events for the gone output */
  if (monitor == NULL) {
    guint id;
    /* No monitor - we're not useful atm */
    notify_compositor_up_state (self, PHOSH_PRIVATE_SHELL_STATE_UNKNOWN);
    id = g_idle_add (select_fallback_monitor, self);
    g_source_set_name_by_id (id, "[PhoshShell] select fallback monitor");
  } else {
    if (needs_notify)
      notify_compositor_up_state (self, PHOSH_PRIVATE_SHELL_STATE_UP);
  }
}

/**
 * phosh_shell_get_builtin_monitor:
 * @self: The shell
 *
 * Returns:(transfer none)(nullable): the built in monitor or %NULL if there is no built in monitor
 */
PhoshMonitor *
phosh_shell_get_builtin_monitor (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_MONITOR (priv->builtin_monitor) || priv->builtin_monitor == NULL,
                        NULL);

  return priv->builtin_monitor;
}

/**
 * phosh_shell_get_primary_monitor:
 * @self: The shell
 *
 * Returns:(transfer none)(nullable): the primary monitor or %NULL if there currently are no outputs
 */
PhoshMonitor *
phosh_shell_get_primary_monitor (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  return priv->primary_monitor;
}

/* Manager getters */

/**
 * phosh_shell_get_ambient:
 * @self: The shell singleton
 *
 * Get the ambient light manager
 *
 * Returns: (nullable)(transfer none): The ambient light manager.
 */
PhoshAmbient *
phosh_shell_get_ambient (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_AMBIENT (priv->ambient) || priv->ambient == NULL, NULL);

  return priv->ambient;
}

/**
 * phosh_shell_get_app_tracker:
 * @self: The shell singleton
 *
 * Get the app tracker
 *
 * Returns: (transfer none): The app tracker
 */
PhoshAppTracker *
phosh_shell_get_app_tracker (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_APP_TRACKER (priv->app_tracker), NULL);

  return priv->app_tracker;
}

/**
 * phosh_shell_get_background_manager:
 * @self: The shell singleton
 *
 * Get the background manager
 *
 * Returns: (transfer none): The background manager
 */
PhoshBackgroundManager *
phosh_shell_get_background_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_BACKGROUND_MANAGER (priv->background_manager), NULL);

  return priv->background_manager;
}

/**
 * phosh_shell_get_calls_manager:
 * @self: The shell singleton
 *
 * Get the calls manager
 *
 * Returns: (transfer none): The calls manager
 */
PhoshCallsManager *
phosh_shell_get_calls_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_CALLS_MANAGER (priv->calls_manager), NULL);

  return priv->calls_manager;
}

/**
 * phosh_shell_get_connectivity_manager:
 * @self: The shell singleton
 *
 * Get the connectivity manager
 *
 * Returns: (transfer none): The connectivity manager
 */
PhoshConnectivityManager *
phosh_shell_get_connectivity_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_CONNECTIVITY_MANAGER (priv->connectivity_manager), NULL);
  return priv->connectivity_manager;
}

/**
 * phosh_shell_get_emergency_calls_manager:
 * @self: The shell singleton
 *
 * Get the emergency calls manager
 *
 * Returns: (transfer none): The emergency calls manager
 */
PhoshEmergencyCallsManager*
phosh_shell_get_emergency_calls_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_EMERGENCY_CALLS_MANAGER (priv->emergency_calls_manager), NULL);

  return priv->emergency_calls_manager;
}

/**
 * phosh_shell_get_feedback_manager:
 * @self: The shell singleton
 *
 * Get the feedback manager
 *
 * Returns: (transfer none): The feedback manager
 */
PhoshFeedbackManager *
phosh_shell_get_feedback_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_FEEDBACK_MANAGER (priv->feedback_manager), NULL);

  return priv->feedback_manager;
}

/**
 * phosh_shell_get_gtk_mount_manager:
 * @self: The shell singleton
 *
 * Get the GTK mount manager
 *
 * Returns: (transfer none): The GTK mount manager
 */
PhoshGtkMountManager *
phosh_shell_get_gtk_mount_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_GTK_MOUNT_MANAGER (priv->gtk_mount_manager), NULL);

  return priv->gtk_mount_manager;
}

/**
 * phosh_shell_get_launcher_entry_manager:
 * @self: The shell singleton
 *
 * Get the launcher entry manager
 *
 * Returns: (transfer none): The launcher entry manager
 */
PhoshLauncherEntryManager *
phosh_shell_get_launcher_entry_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ENTRY_MANAGER (priv->launcher_entry_manager), NULL);
  return priv->launcher_entry_manager;
}

/**
 * phosh_shell_get_layout_manager:
 * @self: The shell singleton
 *
 * Get the layout manager
 *
 * Returns: (transfer none): The layout manager
 */
PhoshLayoutManager *
phosh_shell_get_layout_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_LAYOUT_MANAGER (priv->layout_manager), NULL);
  return priv->layout_manager;
}

/**
 * phosh_shell_get_lockscreen_manager:
 * @self: The shell singleton
 *
 * Get the lockscreen manager
 *
 * Returns: (transfer none): The lockscreen manager
 */
PhoshLockscreenManager *
phosh_shell_get_lockscreen_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (priv->lockscreen_manager), NULL);
  return priv->lockscreen_manager;
}

/**
 * phosh_shell_get_mode_manager:
 * @self: The shell singleton
 *
 * Get the mode manager
 *
 * Returns: (transfer none): The mode manager
 */
PhoshModeManager *
phosh_shell_get_mode_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MODE_MANAGER (priv->mode_manager), NULL);
  return priv->mode_manager;
}

/**
 * phosh_shell_get_style_manager:
 * @self: The shell singleton
 *
 * Get the style manager
 *
 * Returns: (transfer none): The style manager
 */
PhoshStyleManager *
phosh_shell_get_style_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_assert (PHOSH_IS_STYLE_MANAGER (priv->style_manager));
  return priv->style_manager;
}

/**
 * phosh_shell_get_monitor_manager:
 * @self: The shell singleton
 *
 * Get the monitor manager
 *
 * Returns: (transfer none): The monitor manager
 */
PhoshMonitorManager *
phosh_shell_get_monitor_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MONITOR_MANAGER (priv->monitor_manager), NULL);
  return priv->monitor_manager;
}

/**
 * phosh_shell_get_toplevel_manager:
 * @self: The shell singleton
 *
 * Get the toplevel manager
 *
 * Returns: (transfer none): The toplevel manager
 */
PhoshToplevelManager *
phosh_shell_get_toplevel_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (priv->toplevel_manager), NULL);
  return priv->toplevel_manager;
}

/**
 * phosh_shell_get_screen_saver_manager:
 * @self: The shell singleton
 *
 * Get the screensaver manager
 *
 * Returns: (transfer none): The screensaver manager
 */
PhoshScreenSaverManager *
phosh_shell_get_screen_saver_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self), NULL);
  return priv->screen_saver_manager;
}

/**
 * phosh_shell_get_screenshot_manager:
 * @self: The shell singleton
 *
 * Get the screenshot manager
 *
 * Returns: (transfer none): The screenshot manager
 */
PhoshScreenshotManager *
phosh_shell_get_screenshot_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (priv->screenshot_manager), NULL);
  return priv->screenshot_manager;
}

/**
 * phosh_shell_get_session_manager:
 * @self: The shell singleton
 *
 * Get the session manager
 *
 * Returns: (transfer none): The session manager
 */
PhoshSessionManager *
phosh_shell_get_session_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_SESSION_MANAGER (priv->session_manager), NULL);

  return priv->session_manager;
}

/**
 * phosh_shell_get_splash_manager:
 * @self: The shell singleton
 *
 * Get the splash manager
 *
 * Returns: (transfer none): The splash manager
 */
PhoshSplashManager *
phosh_shell_get_splash_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_SPLASH_MANAGER (priv->splash_manager), NULL);

  return priv->splash_manager;
}

/**
 * phosh_shell_get_wifi_manager:
 * @self: The shell singleton
 *
 * Get the Wifi manager
 *
 * Returns: (transfer none): The Wifi manager
 */
PhoshWifiManager *
phosh_shell_get_wifi_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (priv->wifi_manager), NULL);
  return priv->wifi_manager;
}

/* Manager getters that create them as needed */

/**
 * phosh_shell_get_brightness_manager:
 * @self: The shell singleton
 *
 * Get the brightness manager
 *
 * Returns: (transfer none): The brightness manager
 */
PhoshBrightnessManager *
phosh_shell_get_brightness_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);

  if (!priv->brightness_manager)
    priv->brightness_manager = phosh_brightness_manager_new ();

  g_return_val_if_fail (PHOSH_IS_BRIGHTNESS_MANAGER (priv->brightness_manager), NULL);

  return priv->brightness_manager;
}

/**
 * phosh_shell_get_battery_manager:
 * @self: The shell singleton
 *
 * Get the battery manager
 *
 * Returns: (transfer none): The battery manager
 */
PhoshBatteryManager *
phosh_shell_get_battery_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->battery_manager)
    priv->battery_manager = phosh_battery_manager_new ();

  g_return_val_if_fail (PHOSH_IS_BATTERY_MANAGER (priv->battery_manager), NULL);
  return priv->battery_manager;
}

/**
 * phosh_shell_get_bt_manager:
 * @self: The shell singleton
 *
 * Get the bluetooth manager
 *
 * Returns: (transfer none): The bluetooth manager
 */
PhoshBtManager *
phosh_shell_get_bt_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->bt_manager)
    priv->bt_manager = phosh_bt_manager_new ();

  g_return_val_if_fail (PHOSH_IS_BT_MANAGER (priv->bt_manager), NULL);
  return priv->bt_manager;
}

/**
 * phosh_shell_get_docked_manager:
 * @self: The shell singleton
 *
 * Get the docked manager
 *
 * Returns: (transfer none): The docked manager
 */
PhoshDockedManager *
phosh_shell_get_docked_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->docked_manager) {
    priv->docked_manager = phosh_docked_manager_new (priv->mode_manager);
    g_object_bind_property (priv->docked_manager,
                            "enabled",
                            self,
                            "docked",
                            G_BINDING_SYNC_CREATE);
  }

  g_return_val_if_fail (PHOSH_IS_DOCKED_MANAGER (priv->docked_manager), NULL);
  return priv->docked_manager;
}

/**
 * phosh_shell_get_hks_manager:
 * @self: The shell singleton
 *
 * Get the hardware killswitch manager
 *
 * Returns: (transfer none): The hardware killswitch manager
 */
PhoshHksManager *
phosh_shell_get_hks_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->hks_manager)
    priv->hks_manager = phosh_hks_manager_new ();

  g_return_val_if_fail (PHOSH_IS_HKS_MANAGER (priv->hks_manager), NULL);
  return priv->hks_manager;
}

/**
 * phosh_shell_get_location_manager:
 * @self: The shell singleton
 *
 * Get the location manager
 *
 * Returns: (transfer none): The location manager
 */
PhoshLocationManager *
phosh_shell_get_location_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->location_manager)
    priv->location_manager = phosh_location_manager_new ();

  g_return_val_if_fail (PHOSH_IS_LOCATION_MANAGER (priv->location_manager), NULL);
  return priv->location_manager;
}

/**
 * phosh_shell_get_mpris_manager:
 * @self: The shell singleton
 *
 * Get the MPRIS manager
 *
 * Returns: (transfer none): The MPRIS manager
 */
PhoshMprisManager *
phosh_shell_get_mpris_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->mpris_manager)
    priv->mpris_manager = phosh_mpris_manager_new ();

  g_return_val_if_fail (PHOSH_IS_MPRIS_MANAGER (priv->mpris_manager), NULL);
  return priv->mpris_manager;
}

/**
 * phosh_shell_get_osk_manager:
 * @self: The shell singleton
 *
 * Get the onscreen keyboard manager
 *
 * Returns: (transfer none): The onscreen keyboard manager
 */
PhoshOskManager *
phosh_shell_get_osk_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->osk_manager)
    priv->osk_manager = phosh_osk_manager_new ();

  g_return_val_if_fail (PHOSH_IS_OSK_MANAGER (priv->osk_manager), NULL);
  return priv->osk_manager;
}

/**
 * phosh_shell_get_rotation_manager:
 * @self: The shell singleton
 *
 * Get the rotation manager
 *
 * Returns: (transfer none): The rotation manager
 */
PhoshRotationManager *
phosh_shell_get_rotation_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->rotation_manager) {
    priv->rotation_manager = phosh_rotation_manager_new (priv->sensor_proxy_manager,
                                                         priv->lockscreen_manager,
                                                         priv->builtin_monitor);
    /*
     * Make sure rotation works even if the primary monitor has already appeared
     * when we create the rotation manager.
     */
    phosh_rotation_manager_set_monitor (priv->rotation_manager, priv->primary_monitor);
  }

  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (priv->rotation_manager), NULL);

  return priv->rotation_manager;
}

/**
 * phosh_shell_get_torch_manager:
 * @self: The shell singleton
 *
 * Get the torch manager
 *
 * Returns: (transfer none): The torch manager
 */
PhoshTorchManager *
phosh_shell_get_torch_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->torch_manager)
    priv->torch_manager = phosh_torch_manager_new ();

  g_return_val_if_fail (PHOSH_IS_TORCH_MANAGER (priv->torch_manager), NULL);
  return priv->torch_manager;
}

/**
 * phosh_shell_get_vpn_manager:
 * @self: The shell singleton
 *
 * Get the VPN manager
 *
 * Returns: (transfer none): The VPN manager
 */
PhoshVpnManager *
phosh_shell_get_vpn_manager (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->vpn_manager)
    priv->vpn_manager = phosh_vpn_manager_new ();

  g_return_val_if_fail (PHOSH_IS_VPN_MANAGER (priv->vpn_manager), NULL);
  return priv->vpn_manager;
}

/**
 * phosh_shell_get_wwan:
 * @self: The shell singleton
 *
 * Get the WWAN manager
 *
 * Returns: (transfer none): The WWAN manager
 */
PhoshWWan *
phosh_shell_get_wwan (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  if (!priv->wwan) {
    PhoshWWanBackend backend = g_settings_get_enum (priv->settings, WWAN_BACKEND_KEY);

    switch (backend) {
      default:
      case PHOSH_WWAN_BACKEND_MM:
        priv->wwan = PHOSH_WWAN (phosh_wwan_mm_new());
        break;
      case PHOSH_WWAN_BACKEND_OFONO:
        priv->wwan = PHOSH_WWAN (phosh_wwan_ofono_new());
        break;
    }
  }

  g_return_val_if_fail (PHOSH_IS_WWAN (priv->wwan), NULL);
  return priv->wwan;
}

/**
 * phosh_shell_get_usable_area:
 * @self: The shell
 * @x:(out)(nullable): The x coordinate where client usable area starts
 * @y:(out)(nullable): The y coordinate where client usable area starts
 * @width:(out)(nullable): The width of the client usable area
 * @height:(out)(nullable): The height of the client usable area
 *
 * Gives the usable area in pixels usable by a client on the primary
 * display.
 */
void
phosh_shell_get_usable_area (PhoshShell *self, int *x, int *y, int *width, int *height)
{
  PhoshMonitor *monitor;
  PhoshMonitorMode *mode;
  int w, h;
  float scale;

  g_return_if_fail (PHOSH_IS_SHELL (self));

  monitor = phosh_shell_get_primary_monitor (self);
  g_return_if_fail (monitor);
  mode = phosh_monitor_get_current_mode (monitor);
  g_return_if_fail (mode != NULL);

  scale = MAX (1.0, phosh_monitor_get_fractional_scale (monitor));

  g_debug ("Primary monitor %p scale is %f, mode: %dx%d, transform is %d",
           monitor,
           scale,
           mode->width,
           mode->height,
           monitor->transform);

  switch (phosh_monitor_get_transform (monitor)) {
  case PHOSH_MONITOR_TRANSFORM_NORMAL:
  case PHOSH_MONITOR_TRANSFORM_180:
  case PHOSH_MONITOR_TRANSFORM_FLIPPED:
  case PHOSH_MONITOR_TRANSFORM_FLIPPED_180:
    w = mode->width / scale;
    h = mode->height / scale - PHOSH_TOP_BAR_HEIGHT - PHOSH_HOME_BAR_HEIGHT;
    break;
  default:
    w = mode->height / scale;
    h = mode->width / scale - PHOSH_TOP_BAR_HEIGHT - PHOSH_HOME_BAR_HEIGHT;
    break;
  }

  if (x)
    *x = 0;
  if (y)
    *y = PHOSH_TOP_BAR_HEIGHT;
  if (width)
    *width = w;
  if (height)
    *height = h;
}

/**
 * phosh_shell_get_area:
 * @self: The shell singleton
 * @width:(out)(nullable): The available width
 * @height:(out)(nullable): The available height
 *
 * Gives the currently available screen area on the primary display.
 */
void
phosh_shell_get_area (PhoshShell *self, int *width, int *height)
{
  int w, h;

  phosh_shell_get_usable_area (self, NULL, NULL, &w, &h);

  if (width)
    *width = w;

  if (height)
    *height = h + PHOSH_TOP_BAR_HEIGHT + PHOSH_HOME_BAR_HEIGHT;
}

static PhoshShell *instance;

/**
 * phosh_shell_set_default:
 * @self: The shell to use
 *
 * Set the PhoshShell singleton that is returned by `phosh_shell_get_default()`
 */
void
phosh_shell_set_default (PhoshShell *self)
{
  g_return_if_fail (PHOSH_IS_SHELL (self));

  g_clear_object (&instance);

  instance = self;
  g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
}

/**
 * phosh_shell_get_default:
 *
 * Get the shell singleton
 *
 * Returns:(transfer none): The shell singleton
 */
PhoshShell *
phosh_shell_get_default (void)
{
  if (!instance)
    g_error ("Shell singleton not set");
  return instance;
}

void
phosh_shell_fade_out (PhoshShell *self, guint timeout)
{
  PhoshShellPrivate *priv;
  PhoshMonitorManager *monitor_manager;

  g_debug ("Fading out...");
  g_return_if_fail (PHOSH_IS_SHELL (self));
  monitor_manager = phosh_shell_get_monitor_manager (self);
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (monitor_manager));
  priv = phosh_shell_get_instance_private (self);

  for (int i = 0; i < phosh_monitor_manager_get_num_monitors (monitor_manager); i++) {
    PhoshFader *fader;
    PhoshMonitor *monitor = phosh_monitor_manager_get_monitor (monitor_manager, i);

    fader = phosh_fader_new (monitor);
    g_ptr_array_add (priv->faders, fader);
    gtk_widget_set_visible (GTK_WIDGET (fader), TRUE);
    if (timeout > 0) {
      guint id;

      id = g_timeout_add_seconds (timeout, (GSourceFunc) on_fade_out_timeout, self);
      g_source_set_name_by_id (id, "[PhoshShell] fade out");
    }
  }
}

/**
 * phosh_shell_set_power_save:
 * @self: The shell
 * @enable: Whether power save mode should be enabled
 *
 * Enter power saving mode. This currently blanks all monitors.
 */
void
phosh_shell_enable_power_save (PhoshShell *self, gboolean enable)
{
  PhoshShellPrivate *priv;
  PhoshMonitorPowerSaveMode ps_mode;

  g_debug ("Entering power save mode: %d", enable);

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  ps_mode = enable ? PHOSH_MONITOR_POWER_SAVE_MODE_OFF : PHOSH_MONITOR_POWER_SAVE_MODE_ON;
  phosh_monitor_manager_set_power_save_mode (priv->monitor_manager, ps_mode);
}

/**
 * phosh_shell_started_by_display_manager:
 * @self: The shell
 *
 * Returns: %TRUE if we were started from a display manager. %FALSE otherwise.
 */
gboolean
phosh_shell_started_by_display_manager (PhoshShell *self)
{
  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);

  if (!g_strcmp0 (g_getenv ("GDMSESSION"), "phosh"))
    return TRUE;

  return FALSE;
}

/**
 * phosh_shell_is_startup_finished:
 * @self: The shell
 *
 * Returns: %TRUE if the shell finished startup. %FALSE otherwise.
 */
gboolean
phosh_shell_is_startup_finished (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);
  priv = phosh_shell_get_instance_private (self);

  return priv->startup_finished;
}


void
phosh_shell_add_global_keyboard_action_entries (PhoshShell         *self,
                                                const GActionEntry *entries,
                                                gint                n_entries,
                                                gpointer            user_data)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);
  g_return_if_fail (priv->keyboard_events);

  g_action_map_add_action_entries (G_ACTION_MAP (priv->keyboard_events),
                                   entries,
                                   n_entries,
                                   user_data);
}


void
phosh_shell_remove_global_keyboard_action_entries (PhoshShell *self,
                                                   GStrv       action_names)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);
  g_return_if_fail (priv->keyboard_events);

  for (int i = 0; i < g_strv_length (action_names); i++) {
    g_action_map_remove_action (G_ACTION_MAP (priv->keyboard_events),
                                action_names[i]);
  }
}

/**
 * phosh_shell_is_session_active
 * @self: The shell
 *
 * Whether this shell is part of the active session
 */
gboolean
phosh_shell_is_session_active (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);
  priv = phosh_shell_get_instance_private (self);

  return phosh_session_manager_is_active (priv->session_manager);
}

/**
 * phosh_shell_get_app_launch_context:
 * @self: The shell
 *
 * Returns: (transfer none): an app launch context for the primary display
 */
GdkAppLaunchContext*
phosh_shell_get_app_launch_context (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), NULL);
  priv = phosh_shell_get_instance_private (self);

  return gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (priv->top_panel)));
}

/**
 * phosh_shell_get_state
 * @self: The shell
 *
 * Returns: The current #PhoshShellStateFlags
 */
PhoshShellStateFlags
phosh_shell_get_state (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), PHOSH_STATE_NONE);
  priv = phosh_shell_get_instance_private (self);

  return priv->shell_state;
}

/**
 * phosh_shell_set_state:
 * @self: The shell
 * @state: The #PhoshShellStateFlags to set
 * @enabled: %TRUE to set a shell state, %FALSE to reset
 *
 * Set the shells state.
 */
void
phosh_shell_set_state (PhoshShell          *self,
                       PhoshShellStateFlags state,
                       gboolean             enabled)
{
  PhoshShellPrivate *priv;
  PhoshShellStateFlags old_state;
  g_autofree char *str_state = NULL;
  g_autofree char *str_new_flags = NULL;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  old_state = priv->shell_state;

  if (enabled)
    priv->shell_state = priv->shell_state | state;
  else
    priv->shell_state = priv->shell_state & ~state;

  if (old_state == priv->shell_state)
    return;

  str_state = g_flags_to_string (PHOSH_TYPE_SHELL_STATE_FLAGS,
                                 state);
  str_new_flags = g_flags_to_string (PHOSH_TYPE_SHELL_STATE_FLAGS,
                                     priv->shell_state);

  g_debug ("%s %s %s shells state. New state: %s",
           enabled ? "Adding" : "Removing",
           str_state,
           enabled ? "to" : "from", str_new_flags);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHELL_STATE]);

  if (state & PHOSH_STATE_MODAL_SYSTEM_PROMPT)
    update_top_level_layer (self);
}

void
phosh_shell_lock (PhoshShell *self)
{
  g_return_if_fail (PHOSH_IS_SHELL (self));

  phosh_shell_set_locked (self, TRUE);
}


void
phosh_shell_unlock (PhoshShell *self)
{
  g_return_if_fail (PHOSH_IS_SHELL (self));

  phosh_shell_set_locked (self, FALSE);
}

/**
 * phosh_shell_get_locked:
 * @self: The #PhoshShell singleton
 *
 * Returns: %TRUE if the shell is currently locked, otherwise %FALSE.
 */
gboolean
phosh_shell_get_locked (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);
  priv = phosh_shell_get_instance_private (self);

  return priv->locked;
}

/**
 * phosh_shell_set_locked:
 * @self: The #PhoshShell singleton
 * @locked: %TRUE to lock the shell
 *
 * Lock the shell. We proxy to lockscreen-manager to avoid
 * that other parts of the shell need to care about this
 * abstraction.
 */
void
phosh_shell_set_locked (PhoshShell *self, gboolean locked)
{
  PhoshShellPrivate *priv;

  g_return_if_fail (PHOSH_IS_SHELL (self));
  priv = phosh_shell_get_instance_private (self);

  if (locked == priv->locked)
    return;

  phosh_lockscreen_manager_set_locked (priv->lockscreen_manager, locked);
}

/**
 * phosh_shell_get_show_splash:
 * @self: The #PhoshShell singleton
 *
 * Whether splash screens should be used when apps start
 * Returns: %TRUE when splash should be used, otherwise %FALSE
 */
gboolean
phosh_shell_get_show_splash (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), TRUE);
  priv = phosh_shell_get_instance_private (self);
  g_return_val_if_fail (PHOSH_IS_DOCKED_MANAGER (priv->docked_manager), TRUE);

  if (debug_flags & PHOSH_SHELL_DEBUG_FLAG_ALWAYS_SPLASH)
    return TRUE;

  if (phosh_docked_manager_get_enabled (priv->docked_manager))
    return FALSE;

  return TRUE;
}

/**
 * phosh_shell_get_docked:
 * @self: The #PhoshShell singleton
 *
 * Returns: %TRUE if the device is currently docked, otherwise %FALSE.
 */
gboolean
phosh_shell_get_docked (PhoshShell *self)
{
  PhoshShellPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);
  priv = phosh_shell_get_instance_private (self);

  return priv->docked;
}

/**
 * phosh_shell_get_blanked:
 * @self: The #PhoshShell singleton
 *
 * Returns: %TRUE if the primary output is currently blanked
 */
gboolean
phosh_shell_get_blanked (PhoshShell *self)
{
  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);

  return phosh_shell_get_state (self) & PHOSH_STATE_BLANKED;
}

/**
 * phosh_shell_activate_action:
 * @self: The #PhoshShell singleton
 * @action: The action name
 * @parameter: The action's parameters
 *
 * Activates the given action. If the action is not found %FALSE is returned and a
 * warning is logged.
 *
 * Returns: %TRUE if the action was found
 */
gboolean
phosh_shell_activate_action (PhoshShell *self, const char *action, GVariant *parameter)
{
  g_return_val_if_fail (PHOSH_IS_SHELL (self), FALSE);
  g_return_val_if_fail (action, FALSE);

  if (g_action_group_has_action (G_ACTION_GROUP (self), action) == FALSE) {
    g_warning ("No such action '%s' on shell object", action);
    return FALSE;
  }

  g_action_group_activate_action (G_ACTION_GROUP (self), action, parameter);
  return TRUE;
}

/*
 * phosh_shell_get_debug_flags:
 *
 * Get the debug flags
 *
 * Returns: The global debug flags
 */
PhoshShellDebugFlags
phosh_shell_get_debug_flags (void)
{
  return debug_flags;
}


GType
phosh_shell_get_lockscreen_type (PhoshShell *self)
{
  PhoshShellClass *klass = PHOSH_SHELL_GET_CLASS (self);
  return klass->get_lockscreen_type (self);
}


void
phosh_shell_show_osd (PhoshShell *self,
                      const char *connector,
                      const char *icon,
                      const char *label,
                      double      level,
                      double      max_level)
{
  PhoshShellPrivate *priv = phosh_shell_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_SHELL (self));

  g_debug ("DBus show osd: connector: %s icon: %s, label: %s, level %f/%f",
           connector, icon, label, level, max_level);

  if (priv->osd) {
    priv->osd_continue = TRUE;
    g_object_set (priv->osd,
                  "connector", connector,
                  "label", label,
                  "icon-name", icon,
                  "level", level,
                  "max-level", max_level,
                  NULL);
  } else {
    priv->osd = PHOSH_OSD_WINDOW (phosh_osd_window_new (connector, label, icon, level, max_level));
    g_signal_connect_swapped (priv->osd, "destroy", G_CALLBACK (on_osd_destroyed), self);
    gtk_widget_set_visible (GTK_WIDGET (priv->osd), TRUE);
  }

  if (!priv->osd_timeoutid) {
    priv->osd_timeoutid = g_timeout_add_seconds (OSD_HIDE_TIMEOUT,
                                                 (GSourceFunc) on_osd_timeout,
                                                 self);
    g_source_set_name_by_id (priv->osd_timeoutid, "[phosh] osd-timeout");
  }
}

/* }}} */
