/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BUILDDIR $ ./run_tool ./tools/app-grid-standalone
 *
 * PhoshAppGrid in a simple wrapper
 */

#include <app-grid.h>

static void
css_setup (void)
{
  GtkCssProvider *provider;
  GFile *file;
  g_autoptr (GError) err = NULL;

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &err)) {
    g_warning ("Failed to load CSS file: %s", err->message);
    g_object_unref (file);
    return;
  }
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref (file);
}

int
main (int argc, char *argv[])
{
  GtkWidget *win;
  GtkWidget *widget;

  gtk_init (&argc, &argv);

  css_setup ();

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);

  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (win, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  gtk_widget_set_visible (win, TRUE);

  widget = g_object_new (PHOSH_TYPE_APP_GRID, NULL);

  gtk_widget_set_visible (widget, TRUE);

  gtk_container_add (GTK_CONTAINER (win), widget);

  gtk_main ();

  return 0;
}
