/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Stubs so we don't need to run the shell */

#include <glib.h>
#include "phosh-wayland.h"
#include "connectivity-manager.h"
#include "shell-priv.h"

static PhoshToplevelManager *toplevel_manager = NULL;
static PhoshBackgroundManager *background_manager = NULL;
static GObject *shell = NULL;
static GObject *wifi_manager = NULL;

PhoshShell *
phosh_shell_get_default (void)
{
  if (shell == NULL)
    shell = g_object_new (G_TYPE_OBJECT, NULL);
  return (PhoshShell*)(shell);
}

void
phosh_shell_get_usable_area (PhoshShell *self, int *x, int *y, int *width, int *height)
{
  if (x)
    *x = 16;
  if (y)
    *y = 32;
  if (width)
    *width = 64;
  if (height)
    *height = 128;
  return;
}

PhoshMonitor *
phosh_shell_get_primary_monitor (PhoshShell *self)
{
  return NULL;
}

PhoshToplevelManager*
phosh_shell_get_toplevel_manager (PhoshShell *self)
{
  if (toplevel_manager == NULL)
    toplevel_manager = g_object_new (PHOSH_TYPE_TOPLEVEL_MANAGER, NULL);

  return toplevel_manager;
}

GdkAppLaunchContext*
phosh_shell_get_app_launch_context (PhoshShell *self)
{
  return gdk_display_get_app_launch_context (gdk_display_get_default ());
}

void
phosh_shell_set_state (PhoshShell *self, PhoshShellStateFlags state, gboolean set)
{
}

PhoshShellStateFlags
phosh_shell_get_state (PhoshShell *self)
{
  return 0;
}

gboolean
phosh_shell_get_locked (PhoshShell *self)
{
  return FALSE;
}

gboolean
phosh_shell_get_docked (PhoshShell *self)
{
  return FALSE;
}

PhoshAppTracker *
phosh_shell_get_app_tracker (PhoshShell *self)
{
  return NULL;
}


PhoshMprisManager *
phosh_shell_get_mpris_manager (PhoshShell *self)
{
  return NULL;
}


PhoshWifiManager *
phosh_shell_get_wifi_manager (PhoshShell *self)
{
  if (wifi_manager == NULL)
    wifi_manager = g_object_new (G_TYPE_OBJECT, NULL);
  return (PhoshWifiManager*)(wifi_manager);
}


PhoshConnectivityManager *
phosh_shell_get_connectivity_manager (PhoshShell *self)
{
  return phosh_connectivity_manager_new ();
}


PhoshLockscreenManager *
phosh_shell_get_lockscreen_manager (PhoshShell *self)
{
  return NULL;
}


PhoshBackgroundManager *
phosh_shell_get_background_manager (PhoshShell *self)
{
  if (background_manager == NULL)
    background_manager = g_object_new (PHOSH_TYPE_BACKGROUND_MANAGER, NULL);

  return background_manager;
}


PhoshLauncherEntryManager *
phosh_shell_get_launcher_entry_manager (PhoshShell *self)
{
  return NULL;
}


PhoshSplashManager *
phosh_shell_get_splash_manager (PhoshShell *self)
{
  return NULL;
}


gboolean
phosh_shell_get_show_splash (PhoshShell *self)
{
  return FALSE;
}


gboolean
phosh_shell_get_blanked (PhoshShell *self)
{
  return FALSE;
}

gboolean
phosh_shell_activate_action (PhoshShell *self, const char *action, GVariant *parameter)
{
  return TRUE;
}

PhoshShellDebugFlags
phosh_shell_get_debug_flags (void)
{
  return 0;
}
