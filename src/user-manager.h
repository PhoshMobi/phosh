/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_USER_MANAGER (phosh_user_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshUserManager, phosh_user_manager, PHOSH, USER_MANAGER, GObject)

PhoshUserManager *phosh_user_manager_new (const char *username);

G_END_DECLS
