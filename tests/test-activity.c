/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "activity.h"

static void
test_phosh_activity_new(void)
{
  PhoshActivity *activity = PHOSH_ACTIVITY (phosh_activity_new ("com.example.foo"));
  g_assert_nonnull (activity);
  g_assert_cmpstr (phosh_activity_get_app_id (activity), ==, "com.example.foo");
  gtk_widget_destroy (GTK_WIDGET (activity));
}


int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func("/phosh/activity/new", test_phosh_activity_new);
  return g_test_run();
}
