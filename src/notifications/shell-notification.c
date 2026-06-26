/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-shell-notification"

#include "phosh-config.h"

#include "shell-notification.h"
#include "notify-manager.h"
#include "util.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

/**
 * PhoshShellNotification:
 *
 * A notification submitted via the Shell notification interface
 *
 * The [type@ShellNotification] is a notification that is emitted by
 * the phone shell itself.
 *
 * It allows ot use an action group to handle the notification's
 * actions. This is an alternative to listening to the `actioned`
 * signal and parsing the action-id manually.
 *
 * Since: 0.56.0
 */

enum {
  PROP_0,
  PROP_ACTION_GROUP,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

typedef struct _PhoshShellNotification {
  PhoshNotification parent;

  GActionGroup     *action_group;
} PhoshShellNotification;


G_DEFINE_TYPE (PhoshShellNotification, phosh_shell_notification, PHOSH_TYPE_NOTIFICATION)


static void
phosh_shell_notification_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PhoshShellNotification *self = PHOSH_SHELL_NOTIFICATION (object);

  switch (property_id) {
  case PROP_ACTION_GROUP:
    g_set_object (&self->action_group, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_shell_notification_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshShellNotification *self = PHOSH_SHELL_NOTIFICATION (object);

  switch (property_id) {
  case PROP_ACTION_GROUP:
    g_value_set_object (value, self->action_group);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_shell_notification_do_action (PhoshNotification *notification, guint id, const char *action)
{
  PhoshShellNotification *self = PHOSH_SHELL_NOTIFICATION (notification);

  g_action_group_activate_action (self->action_group, action, NULL);
}


static void
phosh_shell_notification_finalize (GObject *object)
{
  PhoshShellNotification *self = PHOSH_SHELL_NOTIFICATION (object);

  g_clear_object (&self->action_group);

  G_OBJECT_CLASS (phosh_shell_notification_parent_class)->finalize (object);
}


static void
phosh_shell_notification_class_init (PhoshShellNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshNotificationClass *notification_class = PHOSH_NOTIFICATION_CLASS (klass);

  object_class->get_property = phosh_shell_notification_get_property;
  object_class->set_property = phosh_shell_notification_set_property;
  object_class->finalize = phosh_shell_notification_finalize;

  notification_class->do_action = phosh_shell_notification_do_action;

  /**
   * PhoshShellNotification:action-group:
   *
   * The action group that handles the notifications actions
   */
  props[PROP_ACTION_GROUP] =
    g_param_spec_object ("action-group", "", "",
                         G_TYPE_ACTION_GROUP,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_shell_notification_init (PhoshShellNotification *self)
{
}

/**
 * phosh_shell_notification_set_actions:
 * @self: The notification
 * @actions: The actions
 * @action_group: The action group handling the actions
 *
 * Set the actions for this notification. The `actions` are
 * `action-name`, `label` pairs known from DBus notifications and the
 * `action-group` contains the actions that should be triggered when
 * the button with `action-name` is pressed by the user.
 */
void
phosh_shell_notification_set_actions (PhoshShellNotification *self,
                                      GStrv                   actions,
                                      GActionGroup           *action_group)
{
  g_return_if_fail (PHOSH_IS_SHELL_NOTIFICATION (self));
  g_return_if_fail (actions && actions[0]);
  g_return_if_fail (G_IS_ACTION_GROUP (action_group));

  phosh_notification_set_actions (PHOSH_NOTIFICATION (self), actions);
  g_set_object (&self->action_group, action_group);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTION_GROUP]);
}
