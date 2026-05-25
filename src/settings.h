/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>
#include "wifi-status-page.h"

#define PHOSH_TYPE_SETTINGS (phosh_settings_get_type())

G_DECLARE_FINAL_TYPE (PhoshSettings, phosh_settings, PHOSH, SETTINGS, GtkBin)

GtkWidget * phosh_settings_new (void);
gint        phosh_settings_get_drag_handle_offset (PhoshSettings *self);
void        phosh_settings_hide_details (PhoshSettings *self);
void        phosh_settings_set_two_column (PhoshSettings *self, gboolean enabled);
