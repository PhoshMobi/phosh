/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "status-icon.h"

#pragma once

G_BEGIN_DECLS

#define PHOSH_TYPE_CAMERA_STATUS_ICON phosh_camera_status_icon_get_type ()

G_DECLARE_FINAL_TYPE (PhoshCameraStatusIcon,
                      phosh_camera_status_icon,
                      PHOSH, CAMERA_STATUS_ICON, PhoshStatusIcon)

G_END_DECLS
