/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "notifications/notify-feedback.c"


#include <gio/gdesktopappinfo.h>


static void
test_phosh_notify_feedback_screen_wakeup (void)
{
  g_autoptr (PhoshNotification) noti = NULL;
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();
  g_autoptr (PhoshNotificationList) list = phosh_notification_list_new ();
  g_autoptr (PhoshNotifyFeedback) notify_feedback = phosh_notify_feedback_new (list);
  g_autoptr (GSettings) settings = g_settings_new ("sm.puri.phosh.notifications");

  info = G_APP_INFO (g_desktop_app_info_new ("demo.app.Second.desktop"));

  noti = phosh_notification_new (0,
                                 "should-not-be-seen",
                                 info,
                                 "Hey",
                                 "Testing",
                                 NULL,
                                 NULL,
                                 PHOSH_NOTIFICATION_URGENCY_NORMAL,
                                 NULL,
                                 FALSE,
                                 FALSE,
                                 NULL,
                                 NULL,
                                 now);

  g_assert_false (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  /* urgency */
  g_settings_set_flags (settings, "wakeup-screen-triggers",
                        PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_URGENCY);
  g_settings_set_enum (settings, "wakeup-screen-urgency", PHOSH_NOTIFICATION_URGENCY_NORMAL);

  g_assert_true (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  g_settings_set_flags (settings, "wakeup-screen-triggers",
                        PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_URGENCY);
  g_settings_set_enum (settings, "wakeup-screen-urgency", PHOSH_NOTIFICATION_URGENCY_NORMAL);

  /* category */
  g_settings_set_flags (settings, "wakeup-screen-triggers",
                        PHOSH_NOTIFY_SCREEN_WAKEUP_FLAG_CATEGORY);
  g_settings_set_strv (settings, "wakeup-screen-categories",
                       (const char *const[]) { "im", "foo.bar", "baz", NULL });

  g_assert_false (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  /* Exact match without specific */
  phosh_notification_set_category  (noti, "im");
  g_assert_true (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  /* Exact match with specific but config only specifies class */
  phosh_notification_set_category  (noti, "im.received");
  g_assert_true (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  /* No match */
  phosh_notification_set_category  (noti, "doesnotexist");
  g_assert_false (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));

  /* Exact match with specific and config has specific too */
  phosh_notification_set_category  (noti, "foo.bar");
  g_assert_true (phosh_notify_feedback_check_screen_wakeup (notify_feedback, noti));
}


static void
test_phosh_notify_feedback_find_event_inactive (void)
{
  g_assert_cmpstr (find_event_inactive ("doesnotexist"), ==, "notification-missed-generic");
  g_assert_cmpstr (find_event_inactive (NULL), ==, "notification-missed-generic");
  g_assert_cmpstr (find_event_inactive ("call.unanswered"), ==, "phone-missed-call");
  g_assert_cmpstr (find_event_inactive ("im.received"), ==, "message-missed-instant");
}


static void
test_phosh_notify_feedback_find_event_active (void)
{
  gboolean important = FALSE;

  g_assert_cmpstr (find_event_active ("doesnotexist", &important), ==, "notification-new-generic");
  g_assert_false (important);
  g_assert_cmpstr (find_event_active (NULL, &important), ==, "notification-new-generic");
  g_assert_false (important);
  g_assert_cmpstr (find_event_active ("im.received", &important), ==, "message-new-instant");
  g_assert_false (important);
  g_assert_cmpstr (find_event_active ("x-phosh-cellbroadcast.danger.severe", &important), ==,
                   "message-new-cellbroadcast");
  g_assert_true (important);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/notifyfeedback/check-screen-wakeup",
                   test_phosh_notify_feedback_screen_wakeup);
  g_test_add_func ("/phosh/notifyfeedback/find-event-inactive",
                   test_phosh_notify_feedback_find_event_inactive);
  g_test_add_func ("/phosh/notifyfeedback/find-event-active",
                   test_phosh_notify_feedback_find_event_active);

  return g_test_run ();
}
