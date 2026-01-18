/*
 * Copyright (C) 2018 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * based on Phosh-channel-bar.h from g-c-c which is
 * Copyright (C) 2008 Red Hat, Inc.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_CHANNEL_BAR (phosh_channel_bar_get_type ())
G_DECLARE_FINAL_TYPE (PhoshChannelBar, phosh_channel_bar, PHOSH, CHANNEL_BAR, GtkBox)

GtkWidget *    phosh_channel_bar_new                 (void);

void           phosh_channel_bar_set_name            (PhoshChannelBar *bar, const char *name);
void           phosh_channel_bar_set_icon_name       (PhoshChannelBar *bar, const char *icon_name);
void           phosh_channel_bar_set_low_icon_name   (PhoshChannelBar *bar, const char *icon_name);
void           phosh_channel_bar_set_high_icon_name  (PhoshChannelBar *bar, const char *icon_name);

void           phosh_channel_bar_set_orientation     (PhoshChannelBar *bar,
                                                      GtkOrientation   orientation);
GtkOrientation phosh_channel_bar_get_orientation     (PhoshChannelBar *bar);

GtkAdjustment *phosh_channel_bar_get_adjustment      (PhoshChannelBar *bar);

gboolean       phosh_channel_bar_get_is_muted        (PhoshChannelBar *bar);
void           phosh_channel_bar_set_is_muted        (PhoshChannelBar *bar, gboolean is_muted);
gboolean       phosh_channel_bar_get_show_mute       (PhoshChannelBar *bar);
void           phosh_channel_bar_set_show_mute       (PhoshChannelBar *bar, gboolean show_mute);
void           phosh_channel_bar_set_size_group      (PhoshChannelBar *bar, GtkSizeGroup *group);
void           phosh_channel_bar_set_is_amplified    (PhoshChannelBar *bar, gboolean amplified);
void           phosh_channel_bar_set_base_volume     (PhoshChannelBar *bar, guint32 base_volume);
gboolean       phosh_channel_bar_get_ellipsize       (PhoshChannelBar *bar);
void           phosh_channel_bar_set_ellipsize       (PhoshChannelBar *bar, gboolean ellipsized);

double         phosh_channel_bar_get_volume          (PhoshChannelBar *self);

G_END_DECLS
