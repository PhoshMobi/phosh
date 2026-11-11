/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_CAMERA_MANAGER (phosh_camera_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshCameraManager, phosh_camera_manager, PHOSH, CAMERA_MANAGER, GObject)

PhoshCameraManager *phosh_camera_manager_new        (void);
gboolean            phosh_camera_manager_get_in_use (PhoshCameraManager *self);

G_END_DECLS
