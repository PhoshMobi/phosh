/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "manager.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_BATTERY_MANAGER (phosh_battery_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshBatteryManager, phosh_battery_manager, PHOSH, BATTERY_MANAGER, PhoshManager)

PhoshBatteryManager *phosh_battery_manager_new (void);

G_END_DECLS
