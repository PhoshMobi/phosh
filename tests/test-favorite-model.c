/*
 * Copyright (C) 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include "favorite-list-model.h"

static void
test_phosh_favorite_list_model_get_default(void)
{
  PhoshFavoriteListModel *model1 = phosh_favorite_list_model_get_default ();
  PhoshFavoriteListModel *model2 = phosh_favorite_list_model_get_default ();

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model1));
  g_assert_true (model1 == model2);
  g_object_unref (model1);

  /* test we can do that twice in a row */
  model1 = phosh_favorite_list_model_get_default ();
  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model1));
  g_object_unref (model1);

}


static void
test_phosh_favorite_list_model_get_type (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  g_assert_true (g_list_model_get_item_type (G_LIST_MODEL (model)) == G_TYPE_APP_INFO);
}


static void
test_phosh_favorite_list_model_get_length (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
  g_autoptr (GSettings) settings = NULL;
  const char *items_missing[2] = {"thing-that-wont-exist.desktop", NULL};
  const char *items[2] = {"demo.app.First.desktop", NULL};

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  settings = g_settings_new ("sm.puri.phosh");
  g_settings_set_strv (settings, "favorites", NULL);

  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);

  g_settings_set_strv (settings, "favorites", items_missing);

  /* Should have skipped the non existant item, thus still not items */
  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);

  g_settings_set_strv (settings, "favorites", items);

  /* Should have skipped the non existant item, thus still not items */
  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 1);
}


static void
test_phosh_favorite_list_model_get_item (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
  g_autoptr (GSettings) settings = NULL;
  const char *items[2] = {"demo.app.First.desktop", NULL};
  g_autoptr (GAppInfo) item = NULL;

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  settings = g_settings_new ("sm.puri.phosh");
  g_settings_set_strv (settings, "favorites", NULL);

  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);

  g_assert_null (g_list_model_get_item (G_LIST_MODEL (model), 1));

  g_settings_set_strv (settings, "favorites", items);

  item = g_list_model_get_item (G_LIST_MODEL (model), 0);
  g_assert_nonnull (item);
}


static void
test_phosh_favorite_list_model_add (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GAppInfo) info_desktop = NULL;

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  info = g_app_info_create_from_commandline ("foo",
                                             "com.example.foo",
                                             G_APP_INFO_CREATE_NONE,
                                             NULL);

  settings = g_settings_new ("sm.puri.phosh");
  g_settings_set_strv (settings, "favorites", NULL);

  info_desktop = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
  phosh_favorite_list_model_add_app (model, info_desktop);

  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 1);
}


static void
test_phosh_favorite_list_model_add_dup_invalid (void)
{
  if (g_test_subprocess ()) {
    PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
    g_autoptr (GSettings) settings = NULL;
    g_autoptr (GAppInfo) info_desktop = NULL;

    settings = g_settings_new ("sm.puri.phosh");
    g_settings_set_strv (settings, "favorites", NULL);

    info_desktop = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
    phosh_favorite_list_model_add_app (model, info_desktop);
    /* Boom */
    phosh_favorite_list_model_add_app (model, info_desktop);
  }
  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*: phosh-favorite-list-model-WARNING * "
                             "demo.app.First.desktop is already a favorite\n");
}

static void
test_phosh_favorite_list_model_add_no_id_invalid (void)
{
  if (g_test_subprocess ()) {
    PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
    g_autoptr (GAppInfo) info = g_app_info_create_from_commandline ("foo",
                                                                    "com.example.foo",
                                                                    G_APP_INFO_CREATE_NONE,
                                                                    NULL);
    phosh_favorite_list_model_add_app (model, info);
  }
  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*: phosh-favorite-list-model-CRITICAL * "
                             "Can't add `*`, doesn't have an id\n");
}

static void
test_phosh_favorite_list_model_remove (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GAppInfo) info_desktop = NULL;

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  info = g_app_info_create_from_commandline ("foo",
                                             "com.example.foo",
                                             G_APP_INFO_CREATE_NONE,
                                             NULL);

  settings = g_settings_new ("sm.puri.phosh");
  g_settings_set_strv (settings, "favorites", NULL);

  info_desktop = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));

  /* Add an app so we can remove it */
  phosh_favorite_list_model_add_app (model, info_desktop);

  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 1);

  phosh_favorite_list_model_remove_app (model, info_desktop);

  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);
}


static void
test_phosh_favorite_list_model_remove_favorite_invalid (void)
{
  if (g_test_subprocess ()) {
    PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
    g_autoptr (GSettings) settings = NULL;
    g_autoptr (GAppInfo) info_desktop = NULL;

    settings = g_settings_new ("sm.puri.phosh");
    g_settings_set_strv (settings, "favorites", NULL);
    info_desktop = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));
    /* Boom */
    phosh_favorite_list_model_remove_app (model, info_desktop);

  }
  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*: phosh-favorite-list-model-WARNING * "
                             "demo.app.First.desktop wasn't a favorite\n");
}


static void
test_phosh_favorite_list_model_remove_no_id_invalid (void)
{
  if (g_test_subprocess ()) {
    PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
    g_autoptr (GAppInfo) info = g_app_info_create_from_commandline ("foo",
                                                                    "com.example.foo",
                                                                    G_APP_INFO_CREATE_NONE,
                                                                    NULL);
    phosh_favorite_list_model_remove_app (model, info);
  }
  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_failed ();
  g_test_trap_assert_stderr ("*: phosh-favorite-list-model-CRITICAL * "
                             "Can't remove `*`, doesn't have an id\n");
}


static void
test_phosh_favorite_list_model_is_favorite (void)
{
  PhoshFavoriteListModel *model = phosh_favorite_list_model_get_default ();
  g_autoptr (GSettings) settings = NULL;
  g_autoptr (GAppInfo) info = NULL;
  g_autoptr (GAppInfo) info_desktop = NULL;

  g_assert_true (PHOSH_IS_FAVORITE_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));

  info = g_app_info_create_from_commandline ("foo",
                                             "com.example.foo",
                                             G_APP_INFO_CREATE_NONE,
                                             NULL);

  settings = g_settings_new ("sm.puri.phosh");
  g_settings_set_strv (settings, "favorites", NULL);

  info_desktop = G_APP_INFO (g_desktop_app_info_new ("demo.app.First.desktop"));

  /* Add an app so we can remove it */
  phosh_favorite_list_model_add_app (model, info_desktop);

  g_assert_true (phosh_favorite_list_model_app_is_favorite (model, info_desktop));

  phosh_favorite_list_model_remove_app (model, info_desktop);

  g_assert_false (phosh_favorite_list_model_app_is_favorite (model, info_desktop));

  g_assert_false (phosh_favorite_list_model_app_is_favorite (model, info));
}


int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/favorites-list-model/new", test_phosh_favorite_list_model_get_default);
  g_test_add_func ("/phosh/favorites-list-model/type", test_phosh_favorite_list_model_get_type);
  g_test_add_func ("/phosh/favorites-list-model/length", test_phosh_favorite_list_model_get_length);
  g_test_add_func ("/phosh/favorites-list-model/get", test_phosh_favorite_list_model_get_item);
  g_test_add_func ("/phosh/favorites-list-model/add", test_phosh_favorite_list_model_add);
  g_test_add_func ("/phosh/favorites-list-model/add_dup_invalid",test_phosh_favorite_list_model_add_dup_invalid);
  g_test_add_func ("/phosh/favorites-list-model/add_no_id_invalid",test_phosh_favorite_list_model_add_no_id_invalid);
  g_test_add_func ("/phosh/favorites-list-model/remove", test_phosh_favorite_list_model_remove);
  g_test_add_func ("/phosh/favorites-list-model/remove_favorite_invalid", test_phosh_favorite_list_model_remove_favorite_invalid);
  g_test_add_func ("/phosh/favorites-list-model/remove_no_id_invalid", test_phosh_favorite_list_model_remove_no_id_invalid);
  g_test_add_func ("/phosh/favorites-list-model/is_favorite", test_phosh_favorite_list_model_is_favorite);

  return g_test_run ();
}
