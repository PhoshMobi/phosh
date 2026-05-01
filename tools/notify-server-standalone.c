/*
 * Copyright © 2020 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 *
 * A "real" notification daemon for testing the notification list
 *
 * NOTE: Remember to close this, otherwise you'll miss things
 *
 * If you just want to play around with styles, see notify-blocks
 */

#include <gtk/gtk.h>
#include <notifications/notify-manager.h>
#include <notifications/notification-frame.h>


static GtkWidget *
create (gpointer item, gpointer data)
{
  GtkWidget *row = NULL;
  GtkWidget *frame = NULL;

  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "activatable", FALSE,
                      "visible", TRUE,
                      NULL);

  frame = phosh_notification_frame_new (TRUE, NULL);
  phosh_notification_frame_bind_model (PHOSH_NOTIFICATION_FRAME (frame), item);

  gtk_widget_set_visible (frame, TRUE);

  gtk_container_add (GTK_CONTAINER (row), frame);

  return row;
}


int
main (int argc, char **argv)
{
  GtkWidget *window = NULL;
  GtkWidget *scrolled = NULL;
  GtkWidget *box = NULL;
  PhoshNotifyManager *manager = NULL;
  GtkCssProvider *provider = NULL;
  GFile *file = NULL;
  g_autoptr (GError) err = NULL;

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
                         "title", "PhoshNotification Demo",
                         NULL);
  g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  scrolled = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                           "hscrollbar-policy", GTK_POLICY_NEVER,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled);

  box = g_object_new (GTK_TYPE_LIST_BOX,
                      "selection-mode", GTK_SELECTION_NONE,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (scrolled), box);

  manager = phosh_notify_manager_get_default ();
  gtk_list_box_bind_model (GTK_LIST_BOX (box),
                           G_LIST_MODEL (phosh_notify_manager_get_list (manager)),
                           create,
                           NULL,
                           NULL);

  gtk_main ();

  return 0;
}
