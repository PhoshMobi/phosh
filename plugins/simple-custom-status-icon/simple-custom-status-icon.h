/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "status-icon.h"

#pragma once

G_BEGIN_DECLS

#define PHOSH_TYPE_SIMPLE_CUSTOM_STATUS_ICON phosh_simple_custom_status_icon_get_type ()

G_DECLARE_FINAL_TYPE (PhoshSimpleCustomStatusIcon,
                      phosh_simple_custom_status_icon,
                      PHOSH, SIMPLE_CUSTOM_STATUS_ICON, PhoshStatusIcon)

G_END_DECLS
