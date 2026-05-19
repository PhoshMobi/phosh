/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_APP_TRACKER (phosh_app_tracker_get_type ())

#define APP_TRACKER_MAX_INITIAL_TOPLEVEL_TIMEOUT 30 /* s */

G_DECLARE_FINAL_TYPE (PhoshAppTracker, phosh_app_tracker, PHOSH, APP_TRACKER, GObject)

PhoshAppTracker *phosh_app_tracker_new (void);
void phosh_app_tracker_launch_app_info (PhoshAppTracker *self,
                                        GAppInfo        *info);

G_END_DECLS
