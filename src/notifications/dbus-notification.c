/*
 * Copyright (C) 2020 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-dbus-notification"

#include "phosh-config.h"

#include "dbus-notification.h"
#include "notify-manager.h"
#include "shell-priv.h"
#include "util.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

/**
 * PhoshDBusNotification:
 *
 * A notification submitted via the DBus notification interface
 *
 * The #PhoshDBusNotification is a notification submitted via the
 * org.freedesktop.Notification interface.
 */

typedef struct _PhoshDBusNotification {
  PhoshNotification parent;

  GCancellable     *cancel;
} PhoshDBusNotification;


G_DEFINE_TYPE (PhoshDBusNotification, phosh_dbus_notification, PHOSH_TYPE_NOTIFICATION)


static void
on_app_activated (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (PhoshDBusNotification) self = user_data;
  g_autoptr (GError) err = NULL;
  GAppInfo *info;
  gboolean success;

  info = phosh_notification_get_app_info (PHOSH_NOTIFICATION (self));
  success = phosh_util_activate_action_finish (res, &err);
  if (!success) {
    g_warning ("Failed to activate %s: %s", g_app_info_get_id (info), err->message);
    return;
  }

  g_debug ("Activated '%s'", g_app_info_get_id (info));
}


static void
phosh_dbus_notification_do_action (PhoshNotification *notification, guint id, const char *action)
{
  PhoshDBusNotification *self = PHOSH_DBUS_NOTIFICATION (notification);
  PhoshNotifyManager *nm = phosh_notify_manager_get_default ();
  GAppInfo *info;

  info = phosh_notification_get_app_info (notification);
  if (info) {
    phosh_util_activate_action (info,
                                NULL,
                                NULL,
                                self->cancel,
                                on_app_activated,
                                g_object_ref (self));
  }
  phosh_dbus_notifications_emit_action_invoked (PHOSH_DBUS_NOTIFICATIONS (nm), id, action);
}


static void
phosh_dbus_notification_finalize (GObject *object)
{
  PhoshDBusNotification *self = PHOSH_DBUS_NOTIFICATION (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  G_OBJECT_CLASS (phosh_dbus_notification_parent_class)->finalize (object);
}


static void
phosh_dbus_notification_class_init (PhoshDBusNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshNotificationClass *notification_class = PHOSH_NOTIFICATION_CLASS (klass);

  object_class->finalize = phosh_dbus_notification_finalize;

  notification_class->do_action = phosh_dbus_notification_do_action;
}


static void
phosh_dbus_notification_init (PhoshDBusNotification *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshDBusNotification *
phosh_dbus_notification_new (guint                     id,
                             const char               *app_name,
                             GAppInfo                 *info,
                             const char               *summary,
                             const char               *body,
                             GIcon                    *icon,
                             GIcon                    *image,
                             PhoshNotificationUrgency  urgency,
                             GStrv                     actions,
                             gboolean                  transient,
                             gboolean                  resident,
                             const char               *category,
                             const char               *profile,
                             GDateTime                *timestamp)
{
  return g_object_new (PHOSH_TYPE_DBUS_NOTIFICATION,
                       "id", id,
                       "summary", summary,
                       "body", body,
                       "app-name", app_name,
                       "app-icon", icon,
                       /* Set info after fallback name and icon */
                       "app-info", info,
                       "image", image,
                       "urgency", urgency,
                       "actions", actions,
                       "transient", transient,
                       "resident", resident,
                       "category", category,
                       "profile", profile,
                       "timestamp", timestamp,
                       NULL);
}
