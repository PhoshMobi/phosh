/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "auto-brightness-bucket.h"


static void
test_phosh_auto_brightness_bucket_brightness (void)
{
  g_autoptr (PhoshAutoBrightnessBucket) bucket = phosh_auto_brightness_bucket_new ();
  PhoshAutoBrightness *auto_brightness = PHOSH_AUTO_BRIGHTNESS (bucket);
  double brightness;

  phosh_auto_brightness_add_ambient_level (auto_brightness, 250.0);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 0.55, DBL_EPSILON);

  phosh_auto_brightness_add_ambient_level (auto_brightness, 8000.0);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 1.3, DBL_EPSILON);
  /* More than MAX */
  phosh_auto_brightness_add_ambient_level (auto_brightness, G_MAXINT);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 1.3, DBL_EPSILON);

  phosh_auto_brightness_add_ambient_level (auto_brightness, 0.0);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 0.1, DBL_EPSILON);

  phosh_auto_brightness_add_ambient_level (auto_brightness, 10.0);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 0.1, DBL_EPSILON);

  phosh_auto_brightness_add_ambient_level (auto_brightness, 11.0);
  brightness = phosh_auto_brightness_get_brightness (auto_brightness);
  g_assert_cmpfloat_with_epsilon (brightness, 0.25, DBL_EPSILON);

  g_assert_null (phosh_auto_brightness_get_backlight (auto_brightness));
}


int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/auto-brightness-bucket/brightness",
                   test_phosh_auto_brightness_bucket_brightness);
  return g_test_run ();
}
