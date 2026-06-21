/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "app-grid-button.h"
#include "favorite-list-model.h"
#include "metainfo-cache.h"


static void
test_phosh_app_grid_button_new (void)
{
  PhoshAppGridButtonMode mode;
  GAppInfo *info = g_app_info_create_from_commandline ("foo",
                                                       "com.example.foo",
                                                       G_APP_INFO_CREATE_NONE,
                                                       NULL);
  GAppInfo *got_info;
  GtkWidget *btn;

  btn = phosh_app_grid_button_new (info);
  g_assert_true (PHOSH_IS_APP_GRID_BUTTON (btn));

  mode = phosh_app_grid_button_get_mode (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_LAUNCHER);

  g_assert_true (info == phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));

  g_object_get (btn, "app-info", &got_info, NULL);
  g_assert_true (info == got_info);

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_new_favorite (void)
{
  PhoshAppGridButtonMode mode;
  GAppInfo *info = g_app_info_create_from_commandline ("foo",
                                                       "com.example.foo",
                                                       G_APP_INFO_CREATE_NONE,
                                                       NULL);
  GtkWidget *btn;

  btn = phosh_app_grid_button_new_favorite (info);
  g_assert_true (PHOSH_IS_APP_GRID_BUTTON (btn));

  mode = phosh_app_grid_button_get_mode (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_FAVORITES);

  g_object_get (btn, "mode", &mode, NULL);
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_FAVORITES);

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_set_app_info (void)
{
  g_autoptr (GAppInfo) info1 = g_app_info_create_from_commandline ("foo",
                                                                   "com.example.foo",
                                                                   G_APP_INFO_CREATE_NONE,
                                                                   NULL);
  g_autoptr (GAppInfo) info2 = g_app_info_create_from_commandline ("bar",
                                                                   "com.example.bar",
                                                                   G_APP_INFO_CREATE_NONE,
                                                                   NULL);
  GtkWidget *btn;

  btn = phosh_app_grid_button_new (info1);

  g_assert_true (info1 == phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));

  phosh_app_grid_button_set_app_info (PHOSH_APP_GRID_BUTTON (btn), info2);
  g_assert_true (info2 == phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));
  g_assert_true (gtk_widget_is_sensitive (btn));

  phosh_app_grid_button_set_app_info (PHOSH_APP_GRID_BUTTON (btn), NULL);
  g_assert_true (NULL == phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));
  g_assert_false (gtk_widget_is_sensitive (btn));

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_set_mode (void)
{
  PhoshAppGridButtonMode mode;
  GAppInfo *info = g_app_info_create_from_commandline ("foo",
                                                       "com.example.foo",
                                                       G_APP_INFO_CREATE_NONE,
                                                       NULL);
  GtkWidget *btn;

  btn = phosh_app_grid_button_new (info);
  g_assert_true (PHOSH_IS_APP_GRID_BUTTON (btn));

  phosh_app_grid_button_set_mode (PHOSH_APP_GRID_BUTTON (btn),
                                  PHOSH_APP_GRID_BUTTON_FAVORITES);

  mode = phosh_app_grid_button_get_mode (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_FAVORITES);

  phosh_app_grid_button_set_mode (PHOSH_APP_GRID_BUTTON (btn),
                                  PHOSH_APP_GRID_BUTTON_LAUNCHER);

  mode = phosh_app_grid_button_get_mode (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_LAUNCHER);

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_set_invalid_mode (void)
{

  if (g_test_subprocess ()) {
    GAppInfo *info = g_app_info_create_from_commandline ("foo",
                                                         "com.example.foo",
                                                         G_APP_INFO_CREATE_NONE,
                                                         NULL);
    GtkWidget *btn = phosh_app_grid_button_new (info);

    /* Boom */
    phosh_app_grid_button_set_mode (PHOSH_APP_GRID_BUTTON (btn),
                                    G_MAXINT);
  }
  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*: phosh-app-grid-button-CRITICAL * Invalid mode 2147483647\n");
}


static void
test_phosh_app_grid_button_null_app_info (void)
{
  GtkWidget *btn;

  btn = phosh_app_grid_button_new (NULL);
  g_assert_null (phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));
  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_menu (void)
{
  PhoshFavoriteListModel *list;
  GtkWidget *btn;
  gboolean is_favorite;
  g_autoptr (GAppInfo) info = NULL;
  GActionGroup *actions;

  list = phosh_favorite_list_model_get_default ();

  info = G_APP_INFO (g_desktop_app_info_new ("demo.app.Second.desktop"));

  btn = phosh_app_grid_button_new (info);

  actions = gtk_widget_get_action_group (btn, "app-btn");
  g_action_group_activate_action (actions, "favorite-add", NULL);

  is_favorite = phosh_favorite_list_model_app_is_favorite (list, info);
  g_assert_true (is_favorite);

  g_action_group_activate_action (actions, "favorite-remove", NULL);

  is_favorite = phosh_favorite_list_model_app_is_favorite (list, info);
  g_assert_false (is_favorite);

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_is_favorite (void)
{
  PhoshFavoriteListModel *list;
  PhoshAppGridButtonMode mode;
  gboolean is_favorite;
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GAppInfo) info = NULL;
  GtkWidget *btn;

  list = phosh_favorite_list_model_get_default ();

  info = G_APP_INFO (g_desktop_app_info_new ("demo.app.Second.desktop"));

  /* Clear all favorites */
  settings = g_settings_new ("mobi.phosh.shell");
  g_settings_set_strv (settings, "favorites", NULL);

  btn = phosh_app_grid_button_new (info);
  g_assert_true (PHOSH_IS_APP_GRID_BUTTON (btn));

  mode = phosh_app_grid_button_get_mode (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (mode == PHOSH_APP_GRID_BUTTON_LAUNCHER);

  is_favorite = phosh_app_grid_button_is_favorite (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_false (is_favorite);

  phosh_favorite_list_model_add_app (list, info);

  is_favorite = phosh_app_grid_button_is_favorite (PHOSH_APP_GRID_BUTTON (btn));
  g_assert_true (is_favorite);

  g_object_get (btn, "is-favorite", &is_favorite, NULL);
  g_assert_true (is_favorite);

  g_assert_true (info == phosh_app_grid_button_get_app_info (
                   PHOSH_APP_GRID_BUTTON (btn)));
  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_folder_add_action (void)
{
  g_autoptr (PhoshFolderInfo) folder_info = phosh_folder_info_new_from_folder_path ("foo");
  g_autoptr (GAppInfo) info = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
  g_autoptr (GAppInfo) got_info = NULL;

  GtkWidget *btn = phosh_app_grid_button_new (info);
  GActionGroup *actions = gtk_widget_get_action_group (btn, "app-btn");
  GVariant *param = g_variant_new_string ("foo");
  GListModel *apps = phosh_folder_info_get_app_infos (folder_info);

  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 0);

  g_action_group_activate_action (actions, "folder-add", param);
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  got_info = g_list_model_get_item (apps, 0);
  g_assert_true (g_app_info_equal (info, got_info));

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_folder_new_action (void)
{
  g_autoptr (PhoshFolderInfo) folder_info = NULL;
  g_autoptr (GAppInfo) info = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
  g_autoptr (GAppInfo) got_info = NULL;
  g_autoptr (GSettings) settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  g_auto (GStrv) folders = NULL;

  GtkWidget *btn = phosh_app_grid_button_new (info);
  GActionGroup *actions = gtk_widget_get_action_group (btn, "app-btn");
  GListModel *apps = NULL;

  /* Clear all folders */
  g_settings_set_strv (settings, "folder-children", NULL);

  g_action_group_activate_action (actions, "folder-new", NULL);

  /* Now there must a new folder with our app-id */
  folders = g_settings_get_strv (settings, "folder-children");
  g_assert_true (folders[0] != NULL);

  folder_info = phosh_folder_info_new_from_folder_path (folders[0]);
  g_assert_cmpstr (g_app_info_get_name (info), ==, phosh_folder_info_get_name (folder_info));

  apps = phosh_folder_info_get_app_infos (folder_info);
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  got_info = g_list_model_get_item (apps, 0);
  g_assert_true (g_app_info_equal (info, got_info));

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_folder_remove_action (void)
{
  g_autoptr (PhoshFolderInfo) folder_info = phosh_folder_info_new_from_folder_path ("bar");
  g_autoptr (GAppInfo) info = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));

  GtkWidget *btn = phosh_app_grid_button_new (info);
  GActionGroup *actions = gtk_widget_get_action_group (btn, "app-btn");
  GListModel *apps = phosh_folder_info_get_app_infos (folder_info);

  phosh_app_grid_button_set_folder_info (PHOSH_APP_GRID_BUTTON (btn), folder_info);
  phosh_folder_info_add_app_info (folder_info, info);
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  g_action_group_activate_action (actions, "folder-remove", NULL);
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 0);

  gtk_widget_destroy (btn);
}


static void
test_phosh_app_grid_button_clean_up_empty_folder (void)
{
  const char *folders[] = {"spam", NULL};
  g_auto (GStrv) new_folders = NULL;
  g_autoptr (PhoshFolderInfo) folder_info = phosh_folder_info_new_from_folder_path ("spam");
  g_autoptr (GAppInfo) first_info = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
  g_autoptr (GAppInfo) second_info = G_APP_INFO (g_desktop_app_info_new ("demo.app.Second.desktop"));
  g_autoptr (GSettings) settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  GActionGroup *actions;
  GtkWidget *first_btn = phosh_app_grid_button_new (first_info);
  GtkWidget *second_btn = phosh_app_grid_button_new (second_info);

  g_settings_set_strv (settings, "folder-children", folders);
  phosh_folder_info_add_app_info (folder_info, first_info);
  phosh_folder_info_add_app_info (folder_info, second_info);
  phosh_app_grid_button_set_folder_info (PHOSH_APP_GRID_BUTTON (first_btn), folder_info);
  phosh_app_grid_button_set_folder_info (PHOSH_APP_GRID_BUTTON (second_btn), folder_info);

  actions = gtk_widget_get_action_group (first_btn, "app-btn");
  g_action_group_activate_action (actions, "folder-remove", NULL);
  new_folders = g_settings_get_strv (settings, "folder-children");
  g_assert_cmpstrv (new_folders, folders);
  g_strfreev (new_folders);

  actions = gtk_widget_get_action_group (second_btn, "app-btn");
  g_action_group_activate_action (actions, "folder-remove", NULL);
  new_folders = g_settings_get_strv (settings, "folder-children");
  g_assert_cmpstrv (new_folders, (const char *[]) { NULL });
}


int
main (int   argc,
      char *argv[])
{
  g_autoptr (PhoshMetainfoCache) cache = phosh_metainfo_cache_get_default();

  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func("/phosh/app-grid-button/new", test_phosh_app_grid_button_new);
  g_test_add_func("/phosh/app-grid-button/new_favorite", test_phosh_app_grid_button_new_favorite);
  g_test_add_func("/phosh/app-grid-button/set_app_info", test_phosh_app_grid_button_set_app_info);
  g_test_add_func("/phosh/app-grid-button/set_mode", test_phosh_app_grid_button_set_mode);
  g_test_add_func("/phosh/app-grid-button/set_invalid_mode", test_phosh_app_grid_button_set_invalid_mode);
  g_test_add_func("/phosh/app-grid-button/null_app_info", test_phosh_app_grid_button_null_app_info);
  g_test_add_func("/phosh/app-grid-button/menu", test_phosh_app_grid_button_menu);
  g_test_add_func("/phosh/app-grid-button/is_favorite", test_phosh_app_grid_button_is_favorite);
  g_test_add_func("/phosh/app-grid-button/folder_add_action", test_phosh_app_grid_button_folder_add_action);
  g_test_add_func("/phosh/app-grid-button/folder_new_action", test_phosh_app_grid_button_folder_new_action);
  g_test_add_func("/phosh/app-grid-button/folder_remove_action", test_phosh_app_grid_button_folder_remove_action);
  g_test_add_func("/phosh/app-grid-button/clean_up_empty_folder", test_phosh_app_grid_button_clean_up_empty_folder);

  return g_test_run();
}
