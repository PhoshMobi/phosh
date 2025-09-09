/*
 * Copyright (C) 2023-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "monitor/monitor.h"

#include <glib-object.h>
#include "phosh-settings-enums.h"

#pragma once

G_BEGIN_DECLS


#define PHOSH_TYPE_LAYOUT_MANAGER (phosh_layout_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshLayoutManager, phosh_layout_manager, PHOSH, LAYOUT_MANAGER, GObject)

PhoshLayoutManager          *phosh_layout_manager_new (void);
PhoshLayoutClockPosition     phosh_layout_manager_get_clock_pos        (PhoshLayoutManager *self);
guint                        phosh_layout_manager_get_clock_shift      (PhoshLayoutManager *self);
void                         phosh_layout_manager_get_box_shifts       (PhoshLayoutManager *self,
                                                                        guint *network_shift,
                                                                        guint *indicators_shift,
                                                                        guint *settings_shift);
G_END_DECLS
