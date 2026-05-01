/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include <gtk/gtk.h>
#include <app-grid-button.h>

int
main (int argc, char **argv)
{
  GtkWidget *window = NULL;
  GtkWidget *wrap;
  GtkWidget *box = NULL;
  GtkWidget *btn = NULL;
  GtkCssProvider *provider = NULL;
  GFile *file = NULL;
  g_autoptr (GError) err = NULL;
  GDesktopAppInfo *info = NULL;

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

  window = g_object_new (GTK_TYPE_WINDOW,
                         "visible", TRUE,
                         "default-height", 100,
                         "default-width", 360,
                         "height-request", 100,
                         "width-request", 360,
                         "title", "PhoshAppGridButton Demo",
                         NULL);
  g_signal_connect (window, "delete-event", G_CALLBACK (gtk_main_quit), NULL);
  gtk_window_set_deletable (GTK_WINDOW (window), FALSE);

  wrap = g_object_new (GTK_TYPE_BOX,
                       "spacing", 20,
                       "orientation", GTK_ORIENTATION_VERTICAL,
                       "margin-start", 6,
                       "margin-end", 6,
                       "margin-top", 6,
                       "margin-bottom", 6,
                       "halign", GTK_ALIGN_CENTER,
                       "valign", GTK_ALIGN_CENTER,
                       "visible", TRUE,
                       NULL);
  gtk_container_add (GTK_CONTAINER (window), wrap);

  box = g_object_new (GTK_TYPE_BOX,
                      "spacing", 20,
                      "margin-start", 6,
                      "margin-end", 6,
                      "margin-top", 6,
                      "margin-bottom", 6,
                      "halign", GTK_ALIGN_CENTER,
                      "valign", GTK_ALIGN_CENTER,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (wrap), box);

  info = g_desktop_app_info_new ("org.gtk.Demo4.desktop");
  btn = g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                      "app-info", info,
                      "mode", PHOSH_APP_GRID_BUTTON_FAVORITES,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (box), btn);

  info = g_desktop_app_info_new ("org.gtk.IconBrowser4.desktop");
  btn = g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                      "app-info", info,
                      "mode", PHOSH_APP_GRID_BUTTON_FAVORITES,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (box), btn);

  box = g_object_new (GTK_TYPE_BOX,
                      "spacing", 20,
                      "margin-start", 6,
                      "margin-end", 6,
                      "margin-top", 6,
                      "margin-bottom", 6,
                      "halign", GTK_ALIGN_CENTER,
                      "valign", GTK_ALIGN_CENTER,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (wrap), box);

  info = g_desktop_app_info_new ("org.gtk.Demo4.desktop");
  btn = g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                      "app-info", info,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (box), btn);

  info = g_desktop_app_info_new ("org.gtk.IconBrowser4.desktop");
  btn = g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                      "app-info", info,
                      "visible", TRUE,
                      NULL);
  gtk_container_add (GTK_CONTAINER (box), btn);

  gtk_main ();

  return 0;
}
