/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include "notifications/shell-notification.c"

#include <gio/gdesktopappinfo.h>


static void
do_sth_activated (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  gboolean *actioned = data;

  *actioned = TRUE;
}


static GActionEntry action_entries[] =
{
  { .name = "noti.do-sth", .activate = do_sth_activated },
};

static void
test_phosh_shell_notification_actioned (void)
{
  g_autoptr (PhoshShellNotification) noti = NULL;
  g_autoptr (GSimpleActionGroup) action_group = g_simple_action_group_new ();
  char *actions[] = { "noti.do-sth", "Do something", NULL };
  gboolean actioned = FALSE;

  noti = g_object_new (PHOSH_TYPE_SHELL_NOTIFICATION,
                       "summary", "A summary",
                       "body", "a body",
                       NULL);

  g_action_map_add_action_entries (G_ACTION_MAP (action_group),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   &actioned);
  phosh_shell_notification_set_actions (noti, actions, G_ACTION_GROUP (action_group));

  phosh_notification_do_action (PHOSH_NOTIFICATION (noti), 0, "noti.do-sth");

  g_assert_true (actioned);
}


int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/shell-notification/actioned", test_phosh_shell_notification_actioned);

  return g_test_run ();
}
