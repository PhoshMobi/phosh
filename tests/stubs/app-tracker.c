/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "app-tracker.h"

struct _PhoshAppTracker {
  GObject parent;
};

G_DEFINE_TYPE (PhoshAppTracker, phosh_app_tracker, G_TYPE_OBJECT);

static void
phosh_app_tracker_class_init (PhoshAppTrackerClass *klass)
{
}


static void
phosh_app_tracker_init (PhoshAppTracker *self)
{
}

gboolean
phosh_app_tracker_launch_app_info (PhoshAppTracker *self,
                                   GAppInfo        *info)
{
  return TRUE;
}
