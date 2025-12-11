/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <gtk/gtk.h>
#include "thumbnail.h"

#define PHOSH_TYPE_ACTIVITY (phosh_activity_get_type())

G_DECLARE_FINAL_TYPE (PhoshActivity, phosh_activity, PHOSH, ACTIVITY, GtkEventBox)

GtkWidget  *phosh_activity_new        (const char *app_id);
const char *phosh_activity_get_app_id (PhoshActivity   *self);
void        phosh_activity_set_thumbnail (PhoshActivity *self,
                                          PhoshThumbnail *thumbnail);
void        phosh_activity_get_thumbnail_allocation (PhoshActivity *self,
                                                     GtkAllocation *allocation);
gboolean    phosh_activity_get_has_thumbnail (PhoshActivity *self);
