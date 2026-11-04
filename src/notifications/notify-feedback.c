/*
 * Copyright (C) 2021-2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-notify-feedback"

#include "phosh-config.h"

#include "shell-priv.h"
#include "notify-feedback.h"
#include "notification-source.h"
#include "util.h"

#include <libfeedback.h>

#include <gmobile.h>

/**
 * PhoshNotifyFeedback:
 *
 * Provide feedback on notifications
 *
 * #PhoshNotifyFeedback is the manager object responsible to provide
 * proper feedback on new notifications or when notifications are
 * being closed.
 */

enum {
  PROP_0,
  PROP_NOTIFICATION_LIST,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshNotifyFeedback {
  GObject                      parent;

  LfbEvent                     *active_event;
  LfbEvent                     *inactive_event;
  PhoshNotificationList        *list;

  GSettings                    *settings;
  PhoshNotifyScreenWakeupFlags  wakeup_flags;
  GStrv                         wakeup_categories;
  PhoshNotificationUrgency      wakeup_min_urgency;
};
G_DEFINE_TYPE (PhoshNotifyFeedback, phosh_notify_feedback, G_TYPE_OBJECT)

static void
end_notify_feedback (PhoshNotifyFeedback *self)
{
  g_return_if_fail (lfb_is_initted ());

  if (self->active_event && lfb_event_get_state (self->active_event) == LFB_EVENT_STATE_RUNNING)
    lfb_event_end_feedback_async (self->active_event, NULL, NULL, NULL);

  if (self->inactive_event && lfb_event_get_state (self->inactive_event) == LFB_EVENT_STATE_RUNNING)
    lfb_event_end_feedback_async (self->inactive_event, NULL, NULL, NULL);
}

/**
 * find_event_inactive:
 * @category: The category to look up the event for
 *
 * Look up an event when for a notification category when the device
 * is not in active use.
 *
 * Returns:(nullable): The event name
 */
static const char *
find_event_inactive (const char *category)
{
  const char *ret = "notification-missed-generic";

  if (gm_str_is_null_or_empty (category))
    return ret;

  if (g_str_equal (category, "email.arrived")) {
    ret = "message-missed-email";
  } else if (g_str_equal (category, "im.received")) {
    ret = "message-missed-instant";
  } else if (g_str_equal (category, "x-phosh.sms.received")) {
    ret = "message-missed-sms";
  } else if (g_str_equal (category, "x-gnome.call.unanswered")) {
    ret = "phone-missed-call";
  } else if (g_str_equal (category, "call.unanswered")) {
    ret = "phone-missed-call";
  }

  return ret;
}

/**
 * find_event_active:
 * @category: The category to look up the event for
 * @important:(out): Whether the event is considered important
 *
 * Look up an event when for a notification category when the device
 * is in active use.
 *
 * Returns:(nullable): The event name
 */
static const char *
find_event_active (const char *category, gboolean *important)
{
  const char *ret = "notification-new-generic";

  if (gm_str_is_null_or_empty (category))
    return ret;

  if (g_str_equal (category, "email.arrived"))
    ret = "message-new-email";
  else if (g_str_equal (category, "im.received"))
    ret = "message-new-instant";
  else if (g_str_equal (category, "x-phosh.sms.received"))
    ret = "message-new-sms";
  else if (g_str_equal (category, "x-gnome.call.unanswered"))
    ret = "phone-missed-call";
  else if (g_str_equal (category, "call.ended"))
    ret = "phone-hangup";
  else if (g_str_equal (category, "call.incoming"))
    ret = "phone-incoming-call";
  else if (g_str_equal (category, "call.unanswered"))
    ret = "phone-missed-call";
  else if (g_str_has_prefix (category, "x-phosh-cellbroadcast.")) {
    ret = "message-new-cellbroadcast";
    if (important)
      *important = TRUE;
  }

  return ret;
}


static void
maybe_wakeup_screen (PhoshNotifyFeedback *self, PhoshNotificationSource *source, guint position, guint num)
{
  g_return_if_fail (num > 0);

  if (self->wakeup_flags & PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_ANY) {
    phosh_shell_activate_action (phosh_shell_get_default (), "screensaver.wakeup-screen", NULL);
    return;
  }

  for (int i = 0; i < num; i++) {
    g_autoptr (PhoshNotification) noti = g_list_model_get_item (G_LIST_MODEL (source), position + i);
    gboolean wakeup;

    g_return_if_fail (PHOSH_IS_NOTIFICATION (noti));

    wakeup = phosh_notify_feedback_check_screen_wakeup (self, noti);
    if (wakeup) {
      phosh_shell_activate_action (phosh_shell_get_default (), "screensaver.wakeup-screen", NULL);
      break;
    }
  }
}

/**
 * maybe_trigger_feedback:
 * @self: The notification feedback handler
 * @source: The notification source model
 * @position: Position where notifications were added to the model
 * @num: How many notifications were added to the model
 * @inactive_only: Whether to only look for events used when the device
 *   is considered inactive
 *
 * Look up events matching the categories of newly added notifications
 * and trigger feedback therefore.
 *
 * Returns: `TRUE` when any feedback was triggered
 */
static gboolean
maybe_trigger_feedback (PhoshNotifyFeedback     *self,
                        PhoshNotificationSource *source,
                        guint                    position,
                        guint                    num,
                        gboolean                 inactive_only)
{
  PhoshShell *shell = phosh_shell_get_default ();
  gboolean inactive = phosh_shell_get_blanked (shell) || phosh_shell_get_locked (shell);

  /* Get us the first notification that triggers meaningful feedback */
  for (int i = 0; i < num; i++) {
    g_autoptr (PhoshNotification) noti = g_list_model_get_item (G_LIST_MODEL (source), position + i);
    const char *category, *profile, *sound_file;
    g_autofree char *app_id = NULL;
    GAppInfo *info = NULL;
    gboolean ret = FALSE;

    g_return_val_if_fail (PHOSH_IS_NOTIFICATION (noti), FALSE);
    g_return_val_if_fail (lfb_is_initted (), FALSE);

    info = phosh_notification_get_app_info (noti);
    if (info)
      app_id = phosh_strip_suffix_from_app_id (g_app_info_get_id (info));

    profile = phosh_notification_get_profile (noti);
    if (g_strcmp0 (profile, "none") == 0)
      continue;

    category = phosh_notification_get_category (noti);
    sound_file = phosh_notification_get_sound_file (noti);

    /* The default event */
    if (!inactive_only) {
      const char *name;
      gboolean important = FALSE;

      name = find_event_active (category, &important);
      if (name) {
        g_autoptr (LfbEvent) event = event = lfb_event_new (name);

        lfb_event_set_feedback_profile (event, profile);
        if (sound_file)
          lfb_event_set_sound_file (event, sound_file);

        if (app_id)
          lfb_event_set_app_id (event, app_id);
        g_debug ("Emitting event %s for %s, profile: %s", name, app_id ?: "unknown", profile);
        if (important && app_id)
          lfb_event_set_important (event, TRUE);
        lfb_event_trigger_feedback_async (event, NULL, NULL, NULL);
        /* TODO: we should better track that on the notification */
        g_set_object (&self->active_event, event);
        ret = TRUE;
      }
    }

    /* The event when the device is not in active use (usually long running LED feedback) */
    if (inactive) {
      const char *name;

      name = find_event_inactive (category);
      if (name) {
        g_autoptr (LfbEvent) event = event = lfb_event_new (name);

        lfb_event_set_feedback_profile (event, profile);
        if (app_id)
          lfb_event_set_app_id (event, app_id);
        g_debug ("Emitting event %s for %s, profile: %s", name, app_id ?: "unknown", profile);
        lfb_event_trigger_feedback_async (event, NULL, NULL, NULL);
        /* TODO: we should better track that on the notification */
        g_set_object (&self->inactive_event, event);
        ret = TRUE;
      }
    }

    if (ret)
      return ret;
  }

  return FALSE;
}


static void
on_notification_source_items_changed (PhoshNotifyFeedback *self,
                                      guint                position,
                                      guint                removed,
                                      guint                added,
                                      GListModel          *list)
{
  if (!added)
    return;

  maybe_wakeup_screen (self, PHOSH_NOTIFICATION_SOURCE (list), position, added);

  maybe_trigger_feedback (self, PHOSH_NOTIFICATION_SOURCE (list), position, added, FALSE);
}


static void
on_settings_changed (PhoshNotifyFeedback *self, char *key, GSettings *settings)
{
  self->wakeup_flags = g_settings_get_flags (settings, "wakeup-screen-triggers");
  self->wakeup_min_urgency = g_settings_get_enum (settings, "wakeup-screen-urgency");
  g_strfreev (self->wakeup_categories);
  self->wakeup_categories = g_settings_get_strv (settings, "wakeup-screen-categories");
}


static void
on_notification_list_items_changed (PhoshNotifyFeedback *self,
                                    guint                position,
                                    guint                removed,
                                    guint                added,
                                    GListModel          *list)
{
  g_autoptr (PhoshNotificationSource) first = g_list_model_get_item (list, 0);

  if (!first) {
    g_debug ("Notification list empty, ending feedback");
    end_notify_feedback (self);
  }

  /* No need to worry about removed sources, signals get detached due
   * to g_signal_connect_object () */
  for (int i = position; i < position + added; i++) {
    g_autoptr (PhoshNotificationSource) source = g_list_model_get_item (list, position);
    /* Listen to new notification on the store for feedback triggering */
    g_signal_connect_object (source,
                             "items-changed",
                             G_CALLBACK (on_notification_source_items_changed),
                             self,
                             G_CONNECT_SWAPPED);
    on_notification_source_items_changed (self, 0, 0,
                                         g_list_model_get_n_items (G_LIST_MODEL (source)),
                                         G_LIST_MODEL (source));
  }
}


static void
on_shell_state_changed (PhoshNotifyFeedback *self, GParamSpec *pspec, PhoshShell *shell)
{
  g_return_if_fail (PHOSH_IS_NOTIFY_FEEDBACK (self));

  /* Feedback ongoing, nothing to do */
  if (self->inactive_event && lfb_event_get_state (self->inactive_event) == LFB_EVENT_STATE_RUNNING)
    return;

  if (!phosh_shell_get_blanked (shell) && !phosh_shell_get_locked (shell))
    return;

  /* Ensure we have visual feedback when the device blanks with open notifications */
  for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (self->list)); i++) {
    guint n_items;
    g_autoptr (PhoshNotificationSource) source = g_list_model_get_item (G_LIST_MODEL (self->list), i);

    n_items = g_list_model_get_n_items (G_LIST_MODEL (source));
    if (maybe_trigger_feedback (self, source, 0, n_items, TRUE))
      break;
  }
}


static void
phosh_notify_feedback_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PhoshNotifyFeedback *self = PHOSH_NOTIFY_FEEDBACK (object);

  switch (property_id) {
  case PROP_NOTIFICATION_LIST:
    self->list = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_notify_feedback_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PhoshNotifyFeedback *self = PHOSH_NOTIFY_FEEDBACK (object);

  switch (property_id) {
  case PROP_NOTIFICATION_LIST:
    g_value_set_object (value, self->list);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_notify_feedback_constructed (GObject *object)
{
  PhoshNotifyFeedback *self = PHOSH_NOTIFY_FEEDBACK (object);

  G_OBJECT_CLASS (phosh_notify_feedback_parent_class)->constructed (object);

  g_signal_connect_swapped (self->list,
                            "items-changed",
                            G_CALLBACK (on_notification_list_items_changed),
                            self);

  g_signal_connect_object (phosh_shell_get_default (),
                           "notify::shell-state",
                           G_CALLBACK (on_shell_state_changed),
                           self,
                           G_CONNECT_SWAPPED);
}


static void
phosh_notify_feedback_dispose (GObject *object)
{
  PhoshNotifyFeedback *self = PHOSH_NOTIFY_FEEDBACK (object);

  g_clear_object (&self->settings);

  if (self->inactive_event && self->active_event) {
    end_notify_feedback (self);
    g_clear_object (&self->active_event);
    g_clear_object (&self->inactive_event);
  }

  g_signal_handlers_disconnect_by_data (self->list, self);
  g_clear_object (&self->list);

  g_clear_pointer (&self->wakeup_categories, g_strfreev);

  G_OBJECT_CLASS (phosh_notify_feedback_parent_class)->dispose (object);
}


static void
phosh_notify_feedback_class_init (PhoshNotifyFeedbackClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_notify_feedback_get_property;
  object_class->set_property = phosh_notify_feedback_set_property;
  object_class->constructed = phosh_notify_feedback_constructed;
  object_class->dispose = phosh_notify_feedback_dispose;

  /**
   * PhoshNotifyFeedback:notification-list
   *
   * The list of notifications that drives the feedback emission
   */
  props[PROP_NOTIFICATION_LIST] =
    g_param_spec_object ("notification-list",
                         "",
                         "",
                         PHOSH_TYPE_NOTIFICATION_LIST,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_notify_feedback_init (PhoshNotifyFeedback *self)
{
  self->settings = g_settings_new ("mobi.phosh.shell.notifications");

  g_signal_connect_swapped (self->settings, "changed", G_CALLBACK (on_settings_changed), self);
  on_settings_changed (self, NULL, self->settings);
}


PhoshNotifyFeedback *
phosh_notify_feedback_new (PhoshNotificationList *list)
{
  return PHOSH_NOTIFY_FEEDBACK (g_object_new (PHOSH_TYPE_NOTIFY_FEEDBACK,
                                              "notification-list", list,
                                              NULL));
}


/**
 * phosh_notify_feedback_check_screen_wakeup:
 * @self: The notification feedback manager
 * @notification: The notification to check
 *
 * Checks if the given notification should trigger screeen wakeup
 *
 * Returns: %TRUE if the notification should trigger the screen wakeup.
 */
gboolean
phosh_notify_feedback_check_screen_wakeup (PhoshNotifyFeedback *self,
                                           PhoshNotification   *notification)
{
  const char *category;
  PhoshNotificationUrgency urgency;

  g_return_val_if_fail (PHOSH_IS_NOTIFICATION (notification), FALSE);

  urgency = phosh_notification_get_urgency (notification);
  if (self->wakeup_flags & PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_URGENCY &&
        urgency >= self->wakeup_min_urgency) {
    return TRUE;
  }

  category = phosh_notification_get_category (notification);
  if (!gm_str_is_null_or_empty (category) &&
      self->wakeup_flags & PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_CATEGORY) {
    /* exact match of setting to notification's category (e.g. `im` == `im` or `im.foo == `im.foo` */
    if (g_strv_contains ((const char * const *)self->wakeup_categories, category))
      return TRUE;

    /* setting (`im`) matches the class of the notification (`im.received`) */
    for (int j = 0; j < g_strv_length (self->wakeup_categories); j++) {
      if (strchr (self->wakeup_categories[j], '.'))
        continue;

      if (g_str_has_prefix (category, self->wakeup_categories[j]) &&
          category [strlen (self->wakeup_categories[j])] == '.') {
        return TRUE;
      }
    }
  }
  return FALSE;
}
