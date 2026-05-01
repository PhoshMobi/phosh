/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include <gtk/gtk.h>
#include <activity.h>

int
main (int argc, char **argv)
{
  GtkWidget *window = NULL;
  GtkWidget *scrolled = NULL;
  GtkWidget *box = NULL;
  GtkWidget *activity = NULL;
  GtkCssProvider *provider = NULL;
  GFile *file = NULL;
  GError *error = NULL;

  gtk_init (&argc, &argv);

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &error)) {
    g_warning ("Failed to load CSS file: %s", error->message);
    g_clear_error (&error);
    return 1;
  }
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  window = g_object_new (GTK_TYPE_WINDOW,
                         "visible", TRUE,
                         "default-height", 640,
                         "default-width", 360,
                         "height-request", 640,
                         "width-request", 360,
                         "title", "PhoshApp Demo",
                         NULL);
  g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  scrolled = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                           "vscrollbar-policy", GTK_POLICY_NEVER,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled);

  box = g_object_new (GTK_TYPE_BOX,
                      "spacing", 18,
                      "margin-start", 24,
                      "margin-end", 24,
                      "margin-top", 10,
                      "margin-bottom", 10,
                      "halign", GTK_ALIGN_CENTER,
                      "valign", GTK_ALIGN_FILL,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (scrolled), box);

  activity = g_object_new (PHOSH_TYPE_ACTIVITY,
                           "app-id", "org.gnome.Calculator",
                           "title", "1 + 1 = 2",
                           "win-width", 360,
                           "win-height", 640,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (box), activity);

  activity = g_object_new (PHOSH_TYPE_ACTIVITY,
                           "app-id", "org.gnome.Nautilus",
                           "title", "Home",
                           "win-width", 640,
                           "win-height", 360,
                           "visible", TRUE,
                           NULL);
  gtk_container_add (GTK_CONTAINER (box), activity);

  gtk_main ();

  return 0;
}
