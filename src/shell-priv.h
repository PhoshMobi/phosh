/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "ambient.h"
#include "app-tracker.h"
#include "background-manager.h"
#include "battery-manager.h"
#include "brightness-manager.h"
#include "bt-manager.h"
#include "calls-manager.h"
#include "connectivity-manager.h"
#include "docked-manager.h"
#include "emergency-calls-manager.h"
#include "feedback-manager.h"
#include "gtk-mount-manager.h"
#include "hks-manager.h"
#include "layout-manager.h"
#include "location-manager.h"
#include "plugin-shell.h"
#include "monitor/monitor.h"
#include "osk-manager.h"
#include "rotation-manager.h"
#include "screen-saver-manager.h"
#include "splash-manager.h"
#include "style-manager.h"
#include "toplevel-manager.h"
#include "torch-manager.h"
#include "vpn-manager.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * PhoshShellStateFlags:
 * @PHOSH_STATE_NONE: No other state
 * @PHOSH_STATE_MODAL_SYSTEM_PROMPT: any modal prompt shown
 * @PHOSH_STATE_BLANKED: primary display off
 * @PHOSH_STATE_LOCKED: displays locked
 * @PHOSH_STATE_SETTINGS: settings menu unfolded from top bar
 * @PHOSH_STATE_OVERVIEW: overview unfolded from bottom bar
 *
 * These flags are used to keep track of the state
 * the #PhoshShell is in.
 */
typedef enum {
  PHOSH_STATE_NONE                = 0,
  PHOSH_STATE_MODAL_SYSTEM_PROMPT = 1 << 0,
  PHOSH_STATE_BLANKED             = 1 << 1,
  PHOSH_STATE_LOCKED              = 1 << 2,
  PHOSH_STATE_SETTINGS            = 1 << 3,
  PHOSH_STATE_OVERVIEW            = 1 << 4,
} PhoshShellStateFlags;


/**
 * PhoshShellDebugFlags
 * @PHOSH_SHELL_DEBUG_FLAG_NONE: No debug flags
 * @PHOSH_SHELL_DEBUG_FLAG_ALWAYS_SPLASH: always use splash (even when docked)
 * @PHOSH_SHELL_DEBUG_FLAG_FAKE_BUILTIN: When calculatiog layout treat the first
 *     virtual output like a built-in output.
 * @PHOSH_SHELL_DEBUG_BACKLIGHT_NON_LINEAR: Assume backlight uses non-linear scale
 *
 * These flags are to enable/disable debugging features.
 */
typedef enum {
  PHOSH_SHELL_DEBUG_FLAG_NONE          = 0,
  PHOSH_SHELL_DEBUG_FLAG_ALWAYS_SPLASH = 1 << 0,
  PHOSH_SHELL_DEBUG_FLAG_FAKE_BUILTIN  = 1 << 1,
  PHOSH_SHELL_DEBUG_BACKLIGHT_NON_LINEAR = 1 << 2,
} PhoshShellDebugFlags;


PhoshShellDebugFlags phosh_shell_get_debug_flags (void);
void                 phosh_shell_get_area        (PhoshShell *self, int *width, int *height);
void                 phosh_shell_set_locked      (PhoshShell *self, gboolean locked);
void                 phosh_shell_lock            (PhoshShell *self);
void                 phosh_shell_unlock          (PhoshShell *self);
void                 phosh_shell_set_primary_monitor (PhoshShell *self, PhoshMonitor *monitor);

/* Created by the shell on startup */
PhoshAmbient           *phosh_shell_get_ambient            (PhoshShell *self);
PhoshAppTracker        *phosh_shell_get_app_tracker        (PhoshShell *self);
PhoshBackgroundManager *phosh_shell_get_background_manager (PhoshShell *self);
PhoshBrightnessManager *phosh_shell_get_brightness_manager (PhoshShell *self);
PhoshCallsManager      *phosh_shell_get_calls_manager (PhoshShell *self);
PhoshFeedbackManager   *phosh_shell_get_feedback_manager   (PhoshShell *self);
PhoshGtkMountManager   *phosh_shell_get_gtk_mount_manager  (PhoshShell *self);
PhoshLayoutManager     *phosh_shell_get_layout_manager     (PhoshShell *self);
PhoshModeManager       *phosh_shell_get_mode_manager       (PhoshShell *self);
PhoshSplashManager *    phosh_shell_get_splash_manager     (PhoshShell *self);
PhoshStyleManager      *phosh_shell_get_style_manager      (PhoshShell *self);
PhoshToplevelManager   *phosh_shell_get_toplevel_manager   (PhoshShell *self);
PhoshScreenSaverManager *phosh_shell_get_screen_saver_manager (PhoshShell *self);
/* Created on the fly */
PhoshBtManager         *phosh_shell_get_bt_manager         (PhoshShell *self);
PhoshBatteryManager    *phosh_shell_get_battery_manager    (PhoshShell *self);
PhoshConnectivityManager *phosh_shell_get_connectivity_manager (PhoshShell *self);
PhoshDockedManager     *phosh_shell_get_docked_manager     (PhoshShell *self);
PhoshHksManager        *phosh_shell_get_hks_manager        (PhoshShell *self);
PhoshLocationManager   *phosh_shell_get_location_manager   (PhoshShell *self);
PhoshOskManager        *phosh_shell_get_osk_manager        (PhoshShell *self);
PhoshRotationManager   *phosh_shell_get_rotation_manager   (PhoshShell *self);
PhoshTorchManager      *phosh_shell_get_torch_manager      (PhoshShell *self);
PhoshVpnManager        *phosh_shell_get_vpn_manager        (PhoshShell *self);
PhoshEmergencyCallsManager *phosh_shell_get_emergency_calls_manager (PhoshShell *self);

void                 phosh_shell_enable_power_save (PhoshShell *self, gboolean enable);
gboolean             phosh_shell_started_by_display_manager(PhoshShell *self);
gboolean             phosh_shell_is_startup_finished (PhoshShell *self);
void                 phosh_shell_add_global_keyboard_action_entries (PhoshShell *self,
                                                                     const GActionEntry *actions,
                                                                     gint n_entries,
                                                                     gpointer user_data);
void                 phosh_shell_remove_global_keyboard_action_entries (PhoshShell *self,
                                                                        GStrv action_names);
gboolean             phosh_shell_is_session_active (PhoshShell *self);
GdkAppLaunchContext *phosh_shell_get_app_launch_context (PhoshShell *self);
PhoshShellStateFlags phosh_shell_get_state (PhoshShell *self);
void                 phosh_shell_set_state (PhoshShell *self, PhoshShellStateFlags state, gboolean enabled);
gboolean             phosh_shell_get_show_splash (PhoshShell *self);
gboolean             phosh_shell_get_docked      (PhoshShell *self);
gboolean             phosh_shell_get_blanked     (PhoshShell *self);
gboolean             phosh_shell_activate_action (PhoshShell *self,
                                                  const char *action,
                                                  GVariant   *parameter);

void                 phosh_shell_set_bg_alpha    (PhoshShell *self, double alpha);
void                 phosh_shell_show_osd (PhoshShell *self,
                                           const char *connector,
                                           const char *icon,
                                           const char *label,
                                           double      level,
                                           double      max_level);

G_END_DECLS
