/*
 * Copyright (C) 2018 Purism SPC
 *               2025-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "activity.h"
#include "app-grid.h"
#include "toplevel.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_OVERVIEW (phosh_overview_get_type ())

G_DECLARE_FINAL_TYPE (PhoshOverview, phosh_overview, PHOSH, OVERVIEW, GtkBox)


GtkWidget *phosh_overview_new (void);
void       phosh_overview_refresh (PhoshOverview *self);
void       phosh_overview_reset (PhoshOverview *self);
void       phosh_overview_focus_app_search (PhoshOverview *self);
gboolean   phosh_overview_has_running_activities (PhoshOverview *self);
gboolean   phosh_overview_handle_search (PhoshOverview *self, GdkEvent *event);
PhoshAppGrid *phosh_overview_get_app_grid (PhoshOverview *self);
void         phosh_overview_set_active_activity_opacity (PhoshOverview *self, double progress);
PhoshToplevel *phosh_overview_get_focused_toplevel (PhoshOverview *self);
PhoshToplevel *phosh_overview_get_toplevel_from_activity (PhoshOverview *self,
                                                          PhoshActivity *activity);
gboolean       phosh_overview_get_focused_activity_rect (PhoshOverview *self,
                                                         GtkAllocation *alloc,
                                                         int           *y_off);

G_END_DECLS
