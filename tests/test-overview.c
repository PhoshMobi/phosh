/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "overview.h"

#include <handy.h>

static void
test_phosh_overview_new(void)
{
  GtkWidget *window = phosh_overview_new ();
  g_assert_nonnull (window);
  gtk_widget_destroy (window);
}


int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);
  hdy_init ();

  g_test_add_func("/phosh/overview/new", test_phosh_overview_new);
  return g_test_run();
}
