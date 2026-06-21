/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arunmani@peartree.to>
 */

#include "folder-info.h"

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>


static void
test_phosh_folder_info_new (void)
{
  PhoshFolderInfo *folder_info;
  g_autofree char *path = NULL;

  folder_info = phosh_folder_info_new_from_folder_path ("foo");
  g_object_get (folder_info, "path", &path, NULL);
  g_assert_cmpstr (path, ==, "foo");

  g_assert_true (g_app_info_equal (G_APP_INFO (folder_info), G_APP_INFO (folder_info)));
  g_assert_cmpstr (g_app_info_get_name (G_APP_INFO (folder_info)), ==, "");
  g_assert_false (g_app_info_should_show (G_APP_INFO (folder_info)));
  g_assert_finalize_object (folder_info);
}


static void
test_phosh_folder_info_get_name (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  g_settings_set_string (settings, "name", "X-Phosh-foo.directory");
  g_assert_cmpstr (phosh_folder_info_get_name (folder_info), ==, "Phosh Test Folder");
  g_assert_cmpstr (g_app_info_get_name (G_APP_INFO (folder_info)), ==, "Phosh Test Folder");

  g_settings_set_string (settings, "name", "doesnotexist.directory");
  g_assert_cmpstr (phosh_folder_info_get_name (folder_info), ==, "doesnotexist.directory");

  g_settings_set_string (settings, "name", "broken.directory");
  g_assert_cmpstr (phosh_folder_info_get_name (folder_info), ==, "broken.directory");

  g_settings_set_string (settings, "name", "Bar");
  g_assert_cmpstr (phosh_folder_info_get_name (folder_info), ==, "Bar");
}


static void
test_phosh_folder_info_set_name (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  char *name;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  phosh_folder_info_set_name (folder_info, "Foo");
  name = g_settings_get_string (settings, "name");
  g_assert_cmpstr (name, ==, "Foo");
  g_free (name);

  phosh_folder_info_set_name (folder_info, "Bar");
  name = g_settings_get_string (settings, "name");
  g_assert_cmpstr (name, ==, "Bar");
  g_free (name);
}


static void
test_phosh_folder_info_get_app_infos (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  const char *app_ids[] = {"demo.app.First.desktop", NULL};
  g_autoptr (GListModel) apps = NULL;
  g_autoptr (GAppInfo) got, made;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  g_settings_set_strv (settings, "apps", app_ids);
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  apps = phosh_folder_info_get_app_infos (folder_info);
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  g_object_get (folder_info, "app-infos", &apps, NULL);
  g_assert_cmpint (g_list_model_get_n_items (apps), ==, 1);
  g_assert_true (g_app_info_should_show (G_APP_INFO (folder_info)));

  made = G_APP_INFO (g_desktop_app_info_new (app_ids[0]));
  got = g_list_model_get_item (apps, 0);
  g_assert_true (g_app_info_equal (made, got));
}


static void
test_phosh_folder_info_contains (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  const char *app_ids[] = {"demo.app.First.desktop", NULL};
  g_autoptr (GAppInfo) info_1, info_2;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  g_settings_set_strv (settings, "apps", app_ids);
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  info_1 = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
  g_assert_true (phosh_folder_info_contains (folder_info, info_1));

  info_2 = G_APP_INFO (g_desktop_app_info_new ("demo.app.Second.desktop"));
  g_assert_false (phosh_folder_info_contains (folder_info, info_2));
}


static void
test_phosh_folder_info_refilter (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  const char *app_ids[] = {"demo.app.First.desktop", NULL};
  GListModel *apps;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  g_settings_set_strv (settings, "apps", app_ids);
  g_object_unref (settings);

  settings = g_settings_new ("mobi.phosh.shell");
  folder_info = phosh_folder_info_new_from_folder_path ("foo");
  apps = phosh_folder_info_get_app_infos (folder_info);

  /* Clear all favorites */
  g_settings_set_strv (settings, "favorites", NULL);
  /* Its apps are not favorite, hence non-empty, so should be true */
  g_assert_true (phosh_folder_info_refilter (folder_info, NULL));
  /* No favorite set, so there should be 1 element */
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  /* Set an app as favorite */
  g_settings_set_strv (settings, "favorites", app_ids);
  /* Its apps are favorite, hence empty, so should be false */
  g_assert_false (phosh_folder_info_refilter (folder_info, NULL));
  /* A favorite set, so there should be 0 elements */
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 0);

  /* Search is done for both favorites and non-favorites */
  /* Search for existing app "terminal" (aka `demo.app.First`) */
  g_assert_true (phosh_folder_info_refilter (folder_info, "terminal"));
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);
  /* Search for "doesnotexist", which doesn't exist, hence empty, so should be false */
  g_assert_false (phosh_folder_info_refilter (folder_info, "doesnotexist"));
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 0);
}


static void
test_phosh_folder_info_add_app_info (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  const char *app_ids[] = {"demo.app.First.desktop", NULL};
  const char *new_app_ids[] = {"demo.app.First.desktop", "demo.app.Second.desktop", NULL};
  g_auto (GStrv) got_app_ids = NULL;
  GListModel *apps;
  g_autoptr (GAppInfo) first, second, second_got;

  /* Clear all favorites */
  settings = g_settings_new ("mobi.phosh.shell");
  g_settings_set_strv (settings, "favorites", NULL);
  g_assert_finalize_object (settings);

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  g_settings_set_strv (settings, "apps", app_ids);
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  apps = phosh_folder_info_get_app_infos (folder_info);
  first = G_APP_INFO (g_desktop_app_info_new (new_app_ids[0]));
  second = G_APP_INFO (g_desktop_app_info_new (new_app_ids[1]));

  phosh_folder_info_add_app_info (folder_info, second);

  got_app_ids = g_settings_get_strv (settings, "apps");
  g_assert_cmpstrv (got_app_ids, new_app_ids);

  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 2);

  second_got = g_list_model_get_item (apps, 1);
  g_assert_true (g_app_info_equal (second, second_got));
}


static void
test_phosh_folder_info_remove_app_info (void)
{
  g_autoptr (GSettings) settings;
  g_autoptr (PhoshFolderInfo) folder_info;
  const char *app_ids[] = {"demo.app.First.desktop", "demo.app.Second.desktop", NULL};
  const char *new_app_ids[] = {"demo.app.Second.desktop", NULL};
  g_auto (GStrv) got_app_ids = NULL;
  GListModel *apps;
  g_autoptr (GAppInfo) first, second, second_got;

  settings = g_settings_new_with_path ("org.gnome.desktop.app-folders.folder",
                                       "/org/gnome/desktop/app-folders/folders/foo/");
  g_settings_set_strv (settings, "apps", app_ids);
  folder_info = phosh_folder_info_new_from_folder_path ("foo");

  apps = phosh_folder_info_get_app_infos (folder_info);
  first = G_APP_INFO (g_desktop_app_info_new (app_ids[0]));
  second = G_APP_INFO (g_desktop_app_info_new (app_ids[1]));

  /* Folder has one app, so not empty, hence true */
  g_assert_true (phosh_folder_info_remove_app_info (folder_info, first));

  got_app_ids = g_settings_get_strv (settings, "apps");
  g_assert_cmpstrv (got_app_ids, new_app_ids);
  g_strfreev (got_app_ids);

  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 1);

  second_got = g_list_model_get_item (apps, 0);
  g_assert_true (g_app_info_equal (second, second_got));

  /* Folder has no apps now, so empty, hence false */
  g_assert_false (phosh_folder_info_remove_app_info (folder_info, second));
  g_assert_cmpuint (g_list_model_get_n_items (apps), ==, 0);
  got_app_ids = g_settings_get_strv (settings, "apps");
  g_assert_cmpstrv (got_app_ids, (const char *[]) { NULL });
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/folder-info/new", test_phosh_folder_info_new);
  g_test_add_func ("/phosh/folder-info/get_name", test_phosh_folder_info_get_name);
  g_test_add_func ("/phosh/folder-info/set_name", test_phosh_folder_info_set_name);
  g_test_add_func ("/phosh/folder-info/get_app_infos", test_phosh_folder_info_get_app_infos);
  g_test_add_func ("/phosh/folder-info/contains", test_phosh_folder_info_contains);
  g_test_add_func ("/phosh/folder-info/refilter", test_phosh_folder_info_refilter);
  g_test_add_func ("/phosh/folder-info/add_app_info", test_phosh_folder_info_add_app_info);
  g_test_add_func ("/phosh/folder-info/remove_app_info", test_phosh_folder_info_remove_app_info);

  return g_test_run ();
}
