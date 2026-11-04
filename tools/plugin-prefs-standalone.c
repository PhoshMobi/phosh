/*
 * Copyright (C) 2022-2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * BUILDDIR $ ./tools/run_tool ./tools/plugin-prefs
 *
 * plugin-prefs-standalone: A simple wrapper to look at plugin preferences
 */

#include "phosh-config.h"
#include "phosh-plugin.h"

#include <adwaita.h>

#include <plugin-loader.h>

#include <glib/gi18n.h>
#include <glib-unix.h>


typedef struct _PluginInfo {
  const char *plugins;
  const char *entrypoint;
} PluginInfo;

static const PluginInfo plugin_infos[] = {
  { LOCKSCREEN_PLUGINS, PHOSH_PLUGIN_EXTENSION_POINT_LOCKSCREEN_WIDGET_PREFS },
  { QUICK_SETTING_PLUGINS, PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET_PREFS },
};

static const PluginInfo *plugin_info;


static GStrv
get_plugin_prefs_dirs (const char *const *plugins)
{
  g_autoptr (GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);

  for (int i = 0; plugins[i] != NULL; i++) {
    char *dir = g_strdup_printf (BUILD_DIR "/plugins/%s/prefs", plugins[i]);
    g_ptr_array_add (dirs, dir);
  }
  g_ptr_array_add (dirs, NULL);

  return (GStrv) g_ptr_array_steal (dirs, NULL);
}


static void
on_activated (GSimpleAction *action,
              GVariant      *parameter,
              gpointer       data)
{
  GtkApplication *app = GTK_APPLICATION (data);
  PhoshPluginLoader *loader;
  const char *name;
  AdwDialog *plugin_prefs;
  GtkWindow *parent;

  name = g_variant_get_string (parameter, NULL);
  g_debug ("Loading plugin '%s'", name);

  loader = g_object_get_data (G_OBJECT (app), "loader");
  plugin_prefs = ADW_DIALOG (phosh_plugin_loader_load_plugin (loader, name));

  parent = gtk_application_get_active_window (app);

  adw_dialog_present (plugin_prefs, GTK_WIDGET (parent));
}


static void
on_app_activated (GtkApplication *app)
{
  g_auto (GStrv) prefs_dirs = NULL;
  PhoshPluginLoader *loader;
  AdwApplicationWindow *window;
  GtkWidget *flowbox;
  GtkWidget *prefs;
  g_auto (GStrv) all_plugins = g_strsplit (plugin_info->plugins, " ", -1);

  flowbox = g_object_new (GTK_TYPE_FLOW_BOX,
                          "valign", GTK_ALIGN_CENTER,
                          NULL);
  prefs_dirs = get_plugin_prefs_dirs ((const char * const*)all_plugins);
  loader = phosh_plugin_loader_new (prefs_dirs, plugin_info->entrypoint);
  g_object_set_data_full (G_OBJECT (app), "loader", loader, g_object_unref);

  for (int i = 0; all_plugins[i] != NULL; i++) {
    const char *plugin = all_plugins[i];
    g_autofree char *name = g_strdup_printf ("%s-prefs", plugin);
    g_autofree char *action_name = NULL;
    g_autofree char *shortcut = NULL;
    GtkWidget *button;
    char *accels[] = { NULL, NULL };

    prefs = phosh_plugin_loader_load_plugin (loader, name);
    if (prefs == NULL)
      continue;

    /* The plugins don't know that we're using a prefix for the installed data */
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);

    button = g_object_new (GTK_TYPE_BUTTON,
                           "label", name,
                           "margin-start", 6,
                           "margin-end", 6,
                           "margin-top", 6,
                           "margin-bottom", 6,
                           NULL);
    action_name = g_strdup_printf ("app.show-prefs::%s", name);
    gtk_actionable_set_detailed_action_name (GTK_ACTIONABLE (button), action_name);
    gtk_flow_box_append (GTK_FLOW_BOX (flowbox), button);

    /* Set up shortcut, beware of collisions */
    shortcut = g_strdup_printf ("<ctrl>%c", name[0]);
    accels[0] = shortcut;
    gtk_application_set_accels_for_action (app, action_name, (const char *const *)accels);
  }

  window = g_object_new (GTK_TYPE_APPLICATION_WINDOW,
                         "application", app,
                         "title", "Plugin Preferences",
                         "child", flowbox,
                         NULL);

  gtk_window_present (GTK_WINDOW (window));
}


static gboolean
on_sigterm (gpointer user_data)
{
  GApplication *app = user_data;

  g_debug ("Got SIGTERM, quitting");
  g_application_quit (app);

  return G_SOURCE_REMOVE;
}


static GActionEntry entries[] =
{
  { .name = "show-prefs", .parameter_type = "s", .activate = on_activated },
};


int
main (int argc, char *argv[])
{
  g_autoptr (AdwApplication) app = NULL;
  g_autoptr (GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  gboolean quick_settings = FALSE, lock_screen = FALSE;
  const GOptionEntry options [] = {
    {"lock-screen", 'l', 0, G_OPTION_ARG_NONE, &lock_screen,
     "Load quick setting plugin prefs", NULL},
    {"quick-settings", 'q', 0, G_OPTION_ARG_NONE, &quick_settings,
     "Load quick setting plugin prefs", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };

  textdomain (GETTEXT_PACKAGE);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);

  opt_context = g_option_context_new ("- Test plugin prefs");
  g_option_context_add_main_entries (opt_context, options, NULL);
  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_warning ("%s", err->message);
    return 1;
  }

  if (lock_screen && quick_settings) {
    g_warning ("Can show either lock screen or quick setting plugins");
    return 1;
  }

  plugin_info = &plugin_infos[quick_settings ? 1 : 0];

  app = g_object_new (ADW_TYPE_APPLICATION,
                      "application-id", "mobi.phosh.Shell.PluginPrefsStandalone",
                      NULL);
  g_signal_connect (app, "activate", G_CALLBACK (on_app_activated), NULL);
  g_action_map_add_action_entries (G_ACTION_MAP (app),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   app);

  g_unix_signal_add (SIGTERM, on_sigterm, app);
  g_application_run (G_APPLICATION (app), argc, argv);

  return EXIT_SUCCESS;
}
