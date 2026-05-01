/*
 * Copyright (C) 2022 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * BUILDDIR $ ./tools/run_tool ./tools/widget-box
 *
 * widget-box is a simple wrapper to run phosh's lockscreen widgets
 */

#include "phosh-config.h"

#include "gtkfilterlistmodel.h"

#include <handy.h>

#include <widget-box.h>
#include <plugin-loader.h>


static void
css_setup (void)
{
  g_autoptr (GtkCssProvider) provider = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &err)) {
    g_warning ("Failed to load CSS file: %s", err->message);
    return;
  }
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


static GStrv
get_plugin_dirs (GStrv plugins)
{
  g_autoptr (GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);

  for (int i = 0; i < g_strv_length (plugins); i++) {
    char *dir = g_strdup_printf (BUILD_DIR "/plugins/%s", plugins[i]);
    g_ptr_array_add (dirs, dir);
  }
  g_ptr_array_add (dirs, NULL);

  return (GStrv) g_ptr_array_steal (dirs, NULL);
}


int
main (int argc, char *argv[])
{
  GtkWidget *win;
  GtkWidget *box;
  g_auto (GStrv) plugins = g_strsplit (PLUGINS, " ", -1);
  g_auto (GStrv) plugin_dirs = NULL;

  gtk_init (&argc, &argv);
  hdy_init ();

  css_setup ();

  /* Needed by the widget box */
  g_type_ensure (GTK_TYPE_FILTER_LIST_MODEL);

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);

  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (win), "Lockscreen widget box");
  gtk_window_set_default_size (GTK_WINDOW (win), 360, 720);
  g_signal_connect (win, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  gtk_widget_set_visible (win, TRUE);

  plugin_dirs = get_plugin_dirs (plugins);
  box = g_object_new (PHOSH_TYPE_WIDGET_BOX, "plugin-dirs", plugin_dirs, NULL);
  phosh_widget_box_set_plugins (PHOSH_WIDGET_BOX (box), plugins);
  gtk_widget_set_visible (box, TRUE);

  gtk_container_add (GTK_CONTAINER (win), box);

  gtk_main ();

  return 0;
}
