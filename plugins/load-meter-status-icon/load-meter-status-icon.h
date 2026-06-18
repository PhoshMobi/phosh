/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: johnwa <johnwa@loopgain.net>
 */

#include "status-icon.h"

#pragma once

G_BEGIN_DECLS

#define PHOSH_TYPE_LOAD_METER_STATUS_ICON phosh_load_meter_status_icon_get_type ()

G_DECLARE_FINAL_TYPE (PhoshLoadMeterStatusIcon,
                      phosh_load_meter_status_icon,
                      PHOSH, LOAD_METER_STATUS_ICON, PhoshStatusIcon)

G_END_DECLS
