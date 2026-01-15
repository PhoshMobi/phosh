/*
 * Copyright (C) 2019 Purism SPC
 *               2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "app-list-model.h"

static void
test_phosh_app_list_model_get_default (void)
{
  PhoshAppListModel *model1 = phosh_app_list_model_get_default ();
  PhoshAppListModel *model2 = phosh_app_list_model_get_default ();

  g_assert_true (PHOSH_IS_APP_LIST_MODEL (model1));
  g_assert_true (model1 == model2);
  g_assert_finalize_object (model1);

  /* test we can do that twice in a row */
  model1 = phosh_app_list_model_get_default ();
  g_assert_true (PHOSH_IS_APP_LIST_MODEL (model1));
  g_assert_finalize_object (model1);
}


typedef struct {
  GMainLoop *loop;
  PhoshAppListModel *model;
  gboolean   changed;
} ItemsChangedContext;


static void
on_items_changed (GListModel *model,
                  guint       position,
                  guint       removed,
                  guint       added,
                  gpointer    user_data)
{
  ItemsChangedContext *context = user_data;

  g_assert_nonnull (context);
  g_assert_true (model == G_LIST_MODEL (context->model));
  g_assert_true (PHOSH_IS_APP_LIST_MODEL (model));
  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), >, 0);
  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, added);
  g_assert_cmpint (removed, ==, 0);
  g_assert_cmpint (position, ==, 0);

  g_assert_nonnull (phosh_app_list_model_lookup_by_startup_wm_class (context->model, "first-app"));

  context->changed = TRUE;
  g_main_loop_quit (context->loop);
}


static void
test_phosh_app_list_model_api (void)
{
  PhoshAppListModel *model = phosh_app_list_model_get_default ();
  g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
  g_autoptr (GAppInfo) info = NULL;
  ItemsChangedContext context = {
    .loop = loop,
    .model = model,
  };

  g_assert_true (PHOSH_IS_APP_LIST_MODEL (model));
  g_assert_true (G_IS_LIST_MODEL (model));
  g_assert_cmpint (g_list_model_get_n_items (G_LIST_MODEL (model)), ==, 0);

  g_assert_null (phosh_app_list_model_lookup_by_startup_wm_class (model, "whatever"));

  g_assert_true (g_list_model_get_item_type (G_LIST_MODEL (model)) == G_TYPE_APP_INFO);
  g_signal_connect (model, "items-changed", G_CALLBACK (on_items_changed), &context);
  g_main_loop_run (loop);
  g_assert_true (context.changed);

  info = g_list_model_get_item (G_LIST_MODEL (model), 0);
  phosh_app_list_model_add_exec (model, "an/exec/with/odd/path1", info);
  g_assert_true (G_IS_APP_INFO (phosh_app_list_model_lookup_by_exec (model, "path1")));
  g_assert_null (phosh_app_list_model_lookup_by_exec (model, "path2"));

  g_assert_finalize_object (model);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/app-list-model/new", test_phosh_app_list_model_get_default);
  g_test_add_func ("/phosh/app-list-model/api", test_phosh_app_list_model_api);

  return g_test_run ();
}
