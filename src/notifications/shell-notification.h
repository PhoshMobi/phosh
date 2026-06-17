/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <notifications/notification.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_SHELL_NOTIFICATION     phosh_shell_notification_get_type ()

G_DECLARE_FINAL_TYPE (PhoshShellNotification, phosh_shell_notification,
                      PHOSH, SHELL_NOTIFICATION, PhoshNotification)

void phosh_shell_notification_set_actions (PhoshShellNotification *self,
                                           GStrv                   actions,
                                           GActionGroup           *action_group);

G_END_DECLS
