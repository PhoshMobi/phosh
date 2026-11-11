/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "camera-manager.h"
#include "launcher-entry-manager.h"
#include "monitor-manager.h"
#include "mpris-manager.h"
#include "session-manager.h"
#include "shell.h"
#include "wifi-manager.h"
#include "wwan/phosh-wwan-iface.h"

G_BEGIN_DECLS

/* Created by the shell on startup */
PhoshLauncherEntryManager *phosh_shell_get_launcher_entry_manager (PhoshShell *self);
PhoshMonitorManager       *phosh_shell_get_monitor_manager (PhoshShell *self);
PhoshMprisManager         *phosh_shell_get_mpris_manager   (PhoshShell *self);
PhoshSessionManager       *phosh_shell_get_session_manager (PhoshShell *self);
/* Created on the fly */
PhoshCameraManager        *phosh_shell_get_camera_manager  (PhoshShell *self);
PhoshWifiManager          *phosh_shell_get_wifi_manager    (PhoshShell *self);
PhoshWWan                 *phosh_shell_get_wwan            (PhoshShell *self);

PhoshMonitor              *phosh_shell_get_primary_monitor (PhoshShell *self);
PhoshMonitor              *phosh_shell_get_builtin_monitor (PhoshShell *self);

G_END_DECLS
