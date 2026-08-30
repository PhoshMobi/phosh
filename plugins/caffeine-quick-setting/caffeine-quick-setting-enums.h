/*
 * Copyright (C) 2024 The Phosh Developers
 *               2025-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum _PhoshCaffeineInhibitModeFlags {
  PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_IDLE = 1 << 0,
  PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_SUSPEND = 1 << 1,
} PhoshCaffeineInhibitModeFlags;

G_END_DECLS
