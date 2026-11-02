/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "quick-setting.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_SCREENSHOT_QUICK_SETTING (phosh_screenshot_quick_setting_get_type ())

G_DECLARE_FINAL_TYPE (PhoshScreenshotQuickSetting,
                      phosh_screenshot_quick_setting,
                      PHOSH, SCREENSHOT_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
