/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_STATUS_ICONS_BOX phosh_status_icons_box_get_type ()
G_DECLARE_FINAL_TYPE (PhoshStatusIconsBox, phosh_status_icons_box, PHOSH, STATUS_ICONS_BOX,
                      GtkContainer)

GtkWidget *phosh_status_icons_box_new (guint spacing);
void phosh_status_icons_box_set_spacing (PhoshStatusIconsBox *self, guint spacing);
guint phosh_status_icons_box_get_spacing (PhoshStatusIconsBox *self);
void phosh_status_icons_box_set_align (PhoshStatusIconsBox *self, GtkAlign align);
GtkAlign phosh_status_icons_box_get_align (PhoshStatusIconsBox *self);
void phosh_status_icons_box_append (PhoshStatusIconsBox *self, GtkWidget *child);
void phosh_status_icons_box_remove (PhoshStatusIconsBox *self, GtkWidget *child);
