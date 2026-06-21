/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include <glib.h>
#include <gio/gio.h>


static void
action_test (GSimpleAction *action,
             GVariant      *parameter,
             gpointer       data)
{
  g_autoptr (GNotification) noti = NULL;
  GApplication *app = data;

  noti = g_notification_new ("Okay");

  g_application_send_notification (app, "test", noti);

  g_application_release (app);
}


static GActionEntry entries[] = {
  { .name = "test", .activate = action_test },
};


static void
activate (GApplication *app)
{
  /* Do nothing */
}


static void
startup (GApplication *app)
{
  g_autoptr (GNotification) noti = NULL;
  g_autoptr (GIcon) icon = NULL;

  icon = g_themed_icon_new ("start-here");

  noti = g_notification_new ("Hello, World!");
  g_notification_set_body (noti, "A <i>whole</i> <b>world</b> of fun :-)");
  g_notification_set_icon (noti, icon);

  g_notification_add_button (noti, "Test", "app.test");
  g_notification_add_button (noti, "Me", "app.test");

  g_application_send_notification (app, "test", noti);
}

int
main (int argc, char **argv)
{
  GApplication *app;

  app = g_application_new ("mobi.phosh.Shell.NotifyTest", G_APPLICATION_DEFAULT_FLAGS);
  g_action_map_add_action_entries (G_ACTION_MAP (app), entries, 1, app);

  g_application_hold (app);

  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  g_signal_connect (app, "startup", G_CALLBACK (startup), NULL);

  return g_application_run (G_APPLICATION (app), argc, argv);
}
