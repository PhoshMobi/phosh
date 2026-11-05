/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "quick-setting.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_SSH_QUICK_SETTING phosh_ssh_quick_setting_get_type ()

G_DECLARE_FINAL_TYPE (PhoshSshQuickSetting,
                      phosh_ssh_quick_setting,
                      PHOSH, SSH_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
