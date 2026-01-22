/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Alexander Mikhaylenko <alexm@gnome.org>
 */
#pragma once

#include <gtk/gtk.h>

#define PHOSH_TYPE_SWIPE_AWAY_BIN (phosh_swipe_away_bin_get_type())

G_DECLARE_FINAL_TYPE (PhoshSwipeAwayBin, phosh_swipe_away_bin, PHOSH, SWIPE_AWAY_BIN, GtkEventBox)

gboolean phosh_swipe_away_bin_get_enabled (PhoshSwipeAwayBin *self);
void     phosh_swipe_away_bin_set_enabled (PhoshSwipeAwayBin *self, gboolean enabled);

gboolean phosh_swipe_away_bin_get_allow_negative (PhoshSwipeAwayBin *self);
void     phosh_swipe_away_bin_set_allow_negative (PhoshSwipeAwayBin *self,
                                                  gboolean           allow_negative);

gboolean phosh_swipe_away_bin_get_reserve_size (PhoshSwipeAwayBin *self);
void     phosh_swipe_away_bin_set_reserve_size (PhoshSwipeAwayBin *self,
                                                gboolean           reserve_size);

void phosh_swipe_away_bin_hide   (PhoshSwipeAwayBin *self);
void phosh_swipe_away_bin_reveal (PhoshSwipeAwayBin *self);
void phosh_swipe_away_bin_remove (PhoshSwipeAwayBin *self);
void phosh_swipe_away_bin_undo   (PhoshSwipeAwayBin *self);
