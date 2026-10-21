/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "lockscreen-manager.h"
#include "calls-manager.h"
#include "user-manager.h"

G_BEGIN_DECLS

PhoshLockscreenManager *phosh_lockscreen_manager_new (PhoshCallsManager *calls_manager,
                                                      PhoshUserManager  *user_manager);

G_END_DECLS
