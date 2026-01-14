/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <handy.h>
#include <gdesktop-enums.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_ACCURACY_ROW (phosh_accuracy_row_get_type ())

G_DECLARE_FINAL_TYPE (PhoshAccuracyRow, phosh_accuracy_row, PHOSH, ACCURACY_ROW, HdyActionRow)

PhoshAccuracyRow *phosh_accuracy_row_new (GDesktopLocationAccuracyLevel level, gboolean selected);
GDesktopLocationAccuracyLevel phosh_accuracy_row_get_level (PhoshAccuracyRow *self);
void              phosh_accuracy_row_set_level (PhoshAccuracyRow *self, GDesktopLocationAccuracyLevel level);
void              phosh_accuracy_row_set_selected (PhoshAccuracyRow *self, gboolean selected);

G_END_DECLS

