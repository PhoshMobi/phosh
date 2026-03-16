/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#pragma once

#include "quick-setting.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_SYNCTHING_QUICK_SETTING phosh_syncthing_quick_setting_get_type ()

G_DECLARE_FINAL_TYPE (PhoshSyncthingQuickSetting,
                      phosh_syncthing_quick_setting,
                      PHOSH, SYNCTHING_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
