/*
 * Copyright (C) 2024 The Phosh Developers
 *               2025-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include "quick-setting.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_CAFFEINE_QUICK_SETTING phosh_caffeine_quick_setting_get_type ()

G_DECLARE_FINAL_TYPE (PhoshCaffeineQuickSetting,
                      phosh_caffeine_quick_setting,
                      PHOSH, CAFFEINE_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
