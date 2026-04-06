/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <gtk/gtk.h>

#define PHOSH_TYPE_TOPLEVEL (phosh_toplevel_get_type())

G_DECLARE_FINAL_TYPE (PhoshToplevel, phosh_toplevel, PHOSH, TOPLEVEL, GObject)

PhoshToplevel *phosh_toplevel_new_from_handle (struct zwlr_foreign_toplevel_handle_v1 *handle);
const char *phosh_toplevel_get_title (PhoshToplevel *self);
const char *phosh_toplevel_get_app_id (PhoshToplevel *self);
struct zwlr_foreign_toplevel_handle_v1 *phosh_toplevel_get_handle (PhoshToplevel *self);
struct zwlr_foreign_toplevel_handle_v1 *phosh_toplevel_get_parent_handle (PhoshToplevel *self);
gboolean phosh_toplevel_is_configured (PhoshToplevel *self);
gboolean phosh_toplevel_is_activated (PhoshToplevel *self);
gboolean phosh_toplevel_is_maximized (PhoshToplevel *self);
gboolean phosh_toplevel_is_fullscreen (PhoshToplevel *self);
void phosh_toplevel_activate (PhoshToplevel *self, struct wl_seat *seat);
void phosh_toplevel_close (PhoshToplevel *self);
void phosh_toplevel_fullscreen (PhoshToplevel *self, gboolean fullscreen);
