/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "camera-manager.h"

static void
test_phosh_camera_manager_new (void)
{
  g_autoptr (PhoshCameraManager) manager = NULL;
  gboolean in_use = TRUE;

  manager = phosh_camera_manager_new ();
  g_assert_true (PHOSH_IS_CAMERA_MANAGER (manager));

  /* Nothing is dispatched without a main loop so we must never see a running camera */
  g_assert_false (phosh_camera_manager_get_in_use (manager));
  g_object_get (manager, "in-use", &in_use, NULL);
  g_assert_false (in_use);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/camera-manager/new", test_phosh_camera_manager_new);

  return g_test_run ();
}
