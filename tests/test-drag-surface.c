/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "testlib-compositor.h"

#include "drag-surface.h"
#include <gdk/gdkwayland.h>

#include <glib.h>


static void
test_drag_surface_g_object_new (PhoshTestCompositorFixture *fixture, gconstpointer unused)
{
  g_autofree char *namespace = g_strdup_printf ("phosh test %s", __func__);
  PhoshMonitor *monitor = phosh_test_get_monitor (fixture->state);
  GtkWidget *surface = g_object_new (PHOSH_TYPE_DRAG_SURFACE,
                                     "layer-shell", phosh_wayland_get_zwlr_layer_shell_v1 (
                                       fixture->state->wl),
                                     "layer-shell-effects",
                                     phosh_wayland_get_zphoc_layer_shell_effects_v1 (
                                       fixture->state->wl),
                                     "wl-output", monitor->wl_output,
                                     "layer", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                     "kbd-interactivity", FALSE,
                                     "exclusive", 32,
                                     "namespace", namespace,
                                     "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                                     "margin-folded", 0,
                                     "margin-unfolded", 200,
                                     "drag-handle", 10,
                                     "drag-mode", PHOSH_DRAG_SURFACE_DRAG_MODE_HANDLE,
                                     "threshold", 0.1,
                                     NULL);

  g_assert_true (PHOSH_IS_DRAG_SURFACE (surface));
  gtk_widget_set_visible (surface, TRUE);

  g_assert_cmpint (phosh_drag_surface_get_drag_handle (PHOSH_DRAG_SURFACE (surface)), ==, 10);
  g_assert_cmpint (phosh_drag_surface_get_drag_mode (PHOSH_DRAG_SURFACE (surface)), ==,
                   PHOSH_DRAG_SURFACE_DRAG_MODE_HANDLE);
  g_assert_cmpfloat_with_epsilon (phosh_drag_surface_get_threshold (PHOSH_DRAG_SURFACE (surface)),
                                  0.1,
                                  DBL_EPSILON);

  g_assert_true (gtk_widget_get_visible (surface));
  g_assert_true (gtk_widget_get_mapped (surface));
  gtk_widget_set_visible (surface, FALSE);
  g_assert_false (gtk_widget_get_visible (surface));
  g_assert_false (gtk_widget_get_mapped (surface));
  gtk_widget_destroy (surface);
}


static void
test_drag_surface_set_state (PhoshTestCompositorFixture *fixture, gconstpointer unused)
{
  g_autofree char *namespace = g_strdup_printf ("phosh test %s", __func__);
  PhoshMonitor *monitor = phosh_test_get_monitor (fixture->state);
  GtkWidget *surface = g_object_new (PHOSH_TYPE_DRAG_SURFACE,
                                     "layer-shell", phosh_wayland_get_zwlr_layer_shell_v1 (
                                       fixture->state->wl),
                                     "layer-shell-effects",
                                     phosh_wayland_get_zphoc_layer_shell_effects_v1 (
                                       fixture->state->wl),
                                     "wl-output", monitor->wl_output,
                                     "layer", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                                     "kbd-interactivity", FALSE,
                                     "exclusive", 32,
                                     "namespace", namespace,
                                     "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                                     NULL);

  g_assert_true (PHOSH_IS_DRAG_SURFACE (surface));
  gtk_widget_set_visible (surface, TRUE);

  g_assert_cmpint (phosh_drag_surface_get_drag_state (PHOSH_DRAG_SURFACE (surface)),
                   ==, PHOSH_DRAG_SURFACE_STATE_FOLDED);
  phosh_drag_surface_set_drag_state (PHOSH_DRAG_SURFACE (surface), PHOSH_DRAG_SURFACE_STATE_UNFOLDED);
  g_assert_cmpint (phosh_drag_surface_get_drag_state (PHOSH_DRAG_SURFACE (surface)),
                   ==, PHOSH_DRAG_SURFACE_STATE_FOLDED);

  gtk_widget_set_visible (surface, FALSE);
  gtk_widget_destroy (surface);
}


int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  PHOSH_COMPOSITOR_TEST_ADD ("/phosh/drag-surface/g_object_new", test_drag_surface_g_object_new);
  PHOSH_COMPOSITOR_TEST_ADD ("/phosh/drag-surface/set_state", test_drag_surface_set_state);

  return g_test_run ();
}
