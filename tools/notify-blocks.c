/*
 * Copyright © 2020 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 *
 * Static notification blocks for experimenting with styles
 *
 * For testing the notification server (rather than widget style) see
 * notify-server-standalone
 */

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <notifications/notification-frame.h>
#include <notifications/notification.h>


static void
empty (PhoshNotificationFrame *self, GtkWidget *box)
{
  gtk_container_remove (GTK_CONTAINER (box), GTK_WIDGET (self));
}


int
main (int argc, char **argv)
{
  GtkWidget *window = NULL;
  GtkWidget *scrolled = NULL;
  GtkWidget *box = NULL;
  GtkWidget *frame = NULL;
  GDesktopAppInfo *app = NULL;
  GStrv actions = (char *[]) { "ok", "Okay", NULL };
  GIcon *image = NULL;
  PhoshNotification *notification = NULL;
  GtkCssProvider *provider = NULL;
  GFile *file = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (GDateTime) now = g_date_time_new_now_local ();

  gtk_init (&argc, &argv);

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &err)) {
    g_warning ("Failed to load CSS file: %s", err->message);
    return 1;
  }
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);

  window = g_object_new (GTK_TYPE_WINDOW,
                         "visible", TRUE,
                         "default-height", 640,
                         "default-width", 360,
                         "height-request", 640,
                         "width-request", 360,
                         "title", "PhoshNotificationFrame Demo",
                         NULL);
  g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  scrolled = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "vscrollbar-policy", GTK_POLICY_NEVER,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled);

  box = g_object_new (GTK_TYPE_BOX,
                      "margin-start", 6,
                      "margin-top", 6,
                      "margin-end", 6,
                      "margin-bottom", 6,
                      "orientation", GTK_ORIENTATION_VERTICAL,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (scrolled), box);

  app = g_desktop_app_info_new ("org.gnome.Calculator.desktop");
  notification = phosh_notification_new (0,
                                         "Not Shown",
                                         G_APP_INFO (app),
                                         "2 + 2",
                                         "= 4",
                                         NULL,
                                         NULL,
                                         PHOSH_NOTIFICATION_URGENCY_NORMAL,
                                         actions,
                                         FALSE,
                                         FALSE,
                                         NULL,
                                         NULL,
                                         now);
  frame = phosh_notification_frame_new (TRUE, NULL);
  phosh_notification_frame_bind_notification (PHOSH_NOTIFICATION_FRAME (frame),
                                              notification);
  g_signal_connect (frame, "empty", G_CALLBACK (empty), box);
  gtk_container_add (GTK_CONTAINER (box), frame);

  image = g_themed_icon_new ("org.gnome.Software");
  notification = phosh_notification_new (1,
                                         "Some App",
                                         NULL,
                                         "2 + 2",
                                         "= 4",
                                         NULL,
                                         image,
                                         PHOSH_NOTIFICATION_URGENCY_NORMAL,
                                         NULL,
                                         FALSE,
                                         FALSE,
                                         NULL,
                                         NULL,
                                         now);
  frame = phosh_notification_frame_new (TRUE, NULL);
  phosh_notification_frame_bind_notification (PHOSH_NOTIFICATION_FRAME (frame),
                                              notification);
  g_signal_connect (frame, "empty", G_CALLBACK (empty), box);
  gtk_container_add (GTK_CONTAINER (box), frame);

  gtk_main ();

  return 0;
}
