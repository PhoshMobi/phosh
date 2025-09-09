/*
 * Copyright (C) 2023-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-layout-manager"

#include "phosh-config.h"

#include "layout-manager.h"
#include "monitor/monitor.h"
#include "shell-priv.h"
#include "top-panel.h"

#include <gmobile.h>
#include <gio/gio.h>

#define SHELL_LAYOUT_KEY "shell-layout"

enum {
  LAYOUT_CHANGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

/**
 * PhoshLayoutManager:
 *
 * Provide information on how to layout shell elements.
 *
 * Calculations are done in GTKs coordinates (thus scaled).
 *
 * Since: 0.29.0
 */

struct _PhoshLayoutManager {
  GObject                     parent;

  GmDisplayPanel             *panel;
  GSettings                  *settings;
  PhoshLayoutClockPosition    clock_pos;
  guint                       clock_shift;
  guint                       corner_shift;
  guint                       network_box_shift;
  guint                       indicators_box_shift;

  PhoshMonitor               *builtin;
};
G_DEFINE_TYPE (PhoshLayoutManager, phosh_layout_manager, G_TYPE_OBJECT)

/* Width of an item in the indicator box */
#define BOX_ITEM_WIDTH 24 /* px */

/* The minimum space needed when shifting the indicator area due to a
 * cutout. This is less than the `*_box_rect.width` as the clock will
 * just be shifted by GTK in those cases */
#define CORNER_CUTOUT_SHIFT_THRESHOLD (PHOSH_TOP_BAR_MIN_PADDING + 3 * BOX_ITEM_WIDTH)

/* Space we want reserved for the central clock */
static const GdkRectangle center_clock_rect = {
  .width = 40,
  .height = PHOSH_TOP_BAR_HEIGHT
};

static const GdkRectangle network_box_rect = {
  /* Relevant icons are wifi, bt, 2 * wwan, network status */
  .width = BOX_ITEM_WIDTH * 5,
  .height = PHOSH_TOP_BAR_HEIGHT
};

static const GdkRectangle indicators_box_rect = {
  /* Relevant icons are  battery, vpn, location and language */
  .width = BOX_ITEM_WIDTH * 4 /* max icons */,
  .height = PHOSH_TOP_BAR_HEIGHT
};

/**
 * get_clock_pos:
 * @self: The layout manager
 * @clock_shift:(out): How far to shift the settings clock downwards
 *
 * Update the clock's positions.
 *
 * If the clock can't be in the center due to the built-in display
 * having a notch shift it left or right (whatever causes less overlap
 * with the notch)
 *
 * We assume the panel uses it's native orientation.
 *
 * Returns: The desired clock position
 */
static PhoshLayoutClockPosition
get_clock_pos (PhoshLayoutManager *self, guint *clock_shift)
{
  PhoshLayoutClockPosition clock_pos = PHOSH_LAYOUT_CLOCK_POS_CENTER;
  PhoshShellLayout layout;
  GListModel *cutouts;
  GdkRectangle clock_rect;
  float scale;
  guint shift = 0;

  layout = g_settings_get_enum (self->settings, SHELL_LAYOUT_KEY);
  if (layout != PHOSH_SHELL_LAYOUT_DEVICE)
    return clock_pos;

  if (self->builtin == NULL)
    return clock_pos;

  if (phosh_monitor_get_transform (self->builtin) != PHOSH_MONITOR_TRANSFORM_NORMAL) {
    /* TODO: we can do better here */
    return clock_pos;
  }

  scale = phosh_monitor_get_fractional_scale (self->builtin);
  clock_rect = center_clock_rect;
  clock_rect.x = (self->builtin->width / scale / 2.0) - center_clock_rect.width / 2.0;

  cutouts = gm_display_panel_get_cutouts (self->panel);
  for (int i = 0; i < g_list_model_get_n_items (cutouts); i++) {
    g_autoptr (GmCutout) cutout = g_list_model_get_item (cutouts, i);
    GdkRectangle notch;
    int overlap_left, overlap_right;
    const GmRect *bounds;

    bounds = gm_cutout_get_bounds (cutout);
    notch = (GdkRectangle) {
      .x = floor (bounds->x / scale),
      .y = floor (bounds->y / scale),
      .width = ceil (bounds->width / scale),
      .height = ceil (bounds->height / scale),
    };

    /* Look for top-bar notch */
    if (!gdk_rectangle_intersect (&notch, &clock_rect, NULL))
        continue;
    shift = notch.height + notch.y;

    overlap_left = network_box_rect.width + center_clock_rect.width - notch.x;
    if (overlap_left <= 0) {
      clock_pos = PHOSH_LAYOUT_CLOCK_POS_LEFT;
      break;
    }

    overlap_right = (indicators_box_rect.width + center_clock_rect.width -
                     ((self->builtin->width / scale) - notch.x - notch.width));
    if (overlap_right <= 0) {
      clock_pos = PHOSH_LAYOUT_CLOCK_POS_RIGHT;
      break;
    }

    g_debug ("Notch overlaps left: %d, right: %d", overlap_left, overlap_right);
    g_warning ("No clock placement found to fully avoid notch");
    clock_pos = (overlap_left < overlap_right) ? PHOSH_LAYOUT_CLOCK_POS_LEFT :
      PHOSH_LAYOUT_CLOCK_POS_RIGHT;
    break;
  }

  if (clock_shift)
    *clock_shift = shift;

  g_debug ("Center clock pos: %d, margin: %u", clock_pos, shift);
  return clock_pos;
}

/**
 * get_cutout_shifts:
 * @self: The layout manager
 *
 * Get the shifts for network and indicator box.
 *
 * If the network box can't be left aligned due to the built-in
 * display having a notch shift it to the right. Similar for the
 * indicators but shift them to the left.
 *
 * We assume the panel uses its native orientation.
 */
static void
get_cutout_shifts (PhoshLayoutManager *self, guint *network, guint *indicators)
{
  PhoshShellLayout layout;
  GListModel *cutouts;
  float scale;
  GdkRectangle indicators_box = indicators_box_rect;
  GdkRectangle network_box = network_box_rect;
  guint width;

  layout = g_settings_get_enum (self->settings, SHELL_LAYOUT_KEY);
  if (layout != PHOSH_SHELL_LAYOUT_DEVICE)
    goto out;

  if (self->builtin == NULL)
    goto out;

  if (phosh_monitor_get_transform (self->builtin) != PHOSH_MONITOR_TRANSFORM_NORMAL)
    goto out;

  scale = phosh_monitor_get_fractional_scale (self->builtin);
  width = self->builtin->logical.width;

  network_box.x = PHOSH_TOP_BAR_MIN_PADDING;
  indicators_box.x = width - indicators_box.width - PHOSH_TOP_BAR_MIN_PADDING;

  cutouts = gm_display_panel_get_cutouts (self->panel);
  for (int i = 0; i < g_list_model_get_n_items (cutouts); i++) {
    g_autoptr (GmCutout) cutout = g_list_model_get_item (cutouts, i);
    const GmRect *phys_bounds = gm_cutout_get_bounds (cutout);
    GdkRectangle bounds;
    int available_space;

    bounds = (GdkRectangle) {
      .x = floor (phys_bounds->x / scale),
      .y = floor (phys_bounds->y / scale),
      .width = ceil (phys_bounds->width / scale),
      .height = ceil (phys_bounds->height / scale),
    };

    available_space = ((width - center_clock_rect.width) / 2) - (bounds.x + bounds.width) -
      CORNER_CUTOUT_SHIFT_THRESHOLD;
    if (gdk_rectangle_intersect (&bounds, &network_box, NULL) && available_space > 0)
      *network = bounds.x + bounds.width + PHOSH_TOP_BAR_CUTOUT_MIN_PADDING;

    available_space = ((width - center_clock_rect.width) / 2) - (width - bounds.x) -
      CORNER_CUTOUT_SHIFT_THRESHOLD;
    if (gdk_rectangle_intersect (&bounds, &indicators_box, NULL) && available_space > 0)
      *indicators = (self->builtin->width / scale - bounds.x) + PHOSH_TOP_BAR_CUTOUT_MIN_PADDING;
  }

 out:
  *network = MAX (*network, PHOSH_TOP_BAR_MIN_PADDING);
  *indicators = MAX (*indicators, PHOSH_TOP_BAR_MIN_PADDING);

  g_debug ("Network box: %d, indicators shift: %d", *network, *indicators);
}

/**
 * get_corner_shift:
 * @self: The layout manager
 *
 * Get the left and right margins to compensate for rounded corners.
 *
 * We assume the panel uses it's native orientation.
 *
 * Returns: The margin in pixels
 */
static guint
get_corner_shift (PhoshLayoutManager *self)
{
  float r, a, b, c, scale;
  PhoshShellLayout layout;
  guint shift = PHOSH_TOP_BAR_MIN_PADDING;

  layout = g_settings_get_enum (self->settings, SHELL_LAYOUT_KEY);
  if (layout != PHOSH_SHELL_LAYOUT_DEVICE)
    goto out;

  scale = phosh_monitor_get_fractional_scale (self->builtin);
  r = c = gm_display_panel_get_border_radius (self->panel) / scale;

  if (G_APPROX_VALUE (r, 0, FLT_EPSILON))
    goto out;

  /*
   * We want to keep the n pixels towards the top screen edge clear
   * n + b = r and m + a = r and c = r
   *
   *    ------------------------
   *    |----+------------------ n
   *    |    |\
   *    |  b | \ c = r
   *    |    |  \
   *    +----+---*
   *    | m    a
   */
  /* Icons usually don't fill the full height so assume 80% */
  b = c - (PHOSH_TOP_BAR_HEIGHT - 0.8 * PHOSH_TOP_BAR_ICON_PIXEL_SIZE) / 2;
  a = floor (sqrt((c * c) - (b * b)));

  shift = MAX (PHOSH_TOP_BAR_MIN_PADDING, ceil (r - a));

 out:
  g_debug ("Corner shift: %u", shift);

  return shift;
}


static void
update_layout (PhoshLayoutManager *self)
{
  PhoshLayoutClockPosition pos;
  guint corner_shift, clock_shift = 0;
  guint network_box_shift = 0, indicators_box_shift = 0;

  corner_shift = get_corner_shift (self);
  get_cutout_shifts (self, &network_box_shift, &indicators_box_shift);
  pos = get_clock_pos (self, &clock_shift);

  indicators_box_shift = MAX (indicators_box_shift, corner_shift);
  network_box_shift = MAX (network_box_shift, corner_shift);

  if (self->corner_shift == corner_shift &&
      self->network_box_shift == network_box_shift &&
      self->indicators_box_shift == indicators_box_shift &&
      self->clock_pos == pos &&
      self->clock_shift == clock_shift) {
    return;
  }

  self->clock_pos = pos;
  self->clock_shift = clock_shift;
  self->corner_shift = corner_shift;
  self->network_box_shift = network_box_shift;
  self->indicators_box_shift = indicators_box_shift;

  g_signal_emit (self, signals[LAYOUT_CHANGED], 0);
}


static void
on_builtin_monitor_configured (PhoshLayoutManager *self, PhoshMonitor *monitor)
{
  g_return_if_fail (PHOSH_IS_LAYOUT_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));

  update_layout (self);
}


static void
on_builtin_monitor_changed (PhoshLayoutManager *self,
                            GParamSpec         *pspec,
                            PhoshShell         *shell)
{
  PhoshMonitor *monitor;

  g_return_if_fail (PHOSH_IS_LAYOUT_MANAGER (self));
  g_return_if_fail (PHOSH_IS_SHELL (shell));
  g_return_if_fail (GM_IS_DISPLAY_PANEL (self->panel));

  monitor = phosh_shell_get_builtin_monitor (shell);
  if (self->builtin == monitor)
    return;

  self->builtin = monitor;
  if (monitor == NULL)
    return;

  g_signal_connect_object (monitor,
                           "configured",
                           G_CALLBACK (on_builtin_monitor_configured),
                           self,
                           G_CONNECT_SWAPPED);
  if (phosh_monitor_is_configured (monitor))
      on_builtin_monitor_configured (self, monitor);
}


static void
phosh_layout_manager_finalize (GObject *object)
{
  PhoshLayoutManager *self = PHOSH_LAYOUT_MANAGER(object);

  self->builtin = NULL;
  g_clear_object (&self->settings);
  g_clear_object (&self->panel);

  G_OBJECT_CLASS (phosh_layout_manager_parent_class)->finalize (object);
}


static void
phosh_layout_manager_class_init (PhoshLayoutManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_layout_manager_finalize;

  signals[LAYOUT_CHANGED] = g_signal_new ("layout-changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL,
                                          NULL, G_TYPE_NONE, 0);
}


static void
phosh_layout_manager_init (PhoshLayoutManager *self)
{
  g_autoptr (GError) err = NULL;
  g_auto (GStrv) compatibles = NULL;
  PhoshShell *shell = phosh_shell_get_default ();
  g_autoptr (GmDeviceInfo) info = NULL;

  self->network_box_shift = PHOSH_TOP_BAR_MIN_PADDING;
  self->indicators_box_shift = PHOSH_TOP_BAR_MIN_PADDING;
  self->corner_shift = PHOSH_TOP_BAR_MIN_PADDING;

  self->settings = g_settings_new ("sm.puri.phosh");

  compatibles = gm_device_tree_get_compatibles (NULL, &err);
  if (compatibles != NULL) {
    info = gm_device_info_new ((const char * const *)compatibles);
    g_set_object (&self->panel, gm_device_info_get_display_panel (info));
  }

  if (self->panel) {
    g_signal_connect_swapped (shell, "notify::builtin-monitor",
                              G_CALLBACK (on_builtin_monitor_changed),
                              self);
    on_builtin_monitor_changed (self, NULL, shell);
  }
}


PhoshLayoutManager *
phosh_layout_manager_new (void)
{
  return PHOSH_LAYOUT_MANAGER (g_object_new (PHOSH_TYPE_LAYOUT_MANAGER, NULL));
}

/**
 * phosh_layout_manager_get_clock_pos:
 * @self: The layout manager
 *
 * The top bar's clock position.
 *
 * Returns: The position
 */
PhoshLayoutClockPosition
phosh_layout_manager_get_clock_pos (PhoshLayoutManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LAYOUT_MANAGER (self), PHOSH_LAYOUT_CLOCK_POS_CENTER);

  return self->clock_pos;
}

/**
 * phosh_layout_manager_get_clock_shift:
 * @self: The layout manager
 *
 * The settings clock position
 *
 * Returns: How far the settings clock is shifted down
 */
guint
phosh_layout_manager_get_clock_shift (PhoshLayoutManager *self)
{
  g_return_val_if_fail (PHOSH_IS_LAYOUT_MANAGER (self), 0);

  return self->clock_shift;
}

/**
 * phosh_layout_manager_get_box_shifts:
 * @self: The layout manager
 * @network_shift: (out): The shift of the network box
 * @indicators_shift: (out): The shift of the indicators box
 *
 * Gets the amount of pixels UI elements should be moved to towards the
 * center because of rounded corners or notches.
 */
void
phosh_layout_manager_get_box_shifts (PhoshLayoutManager *self,
                                     guint              *network_shift,
                                     guint              *indicators_shift)
{
  g_return_if_fail (PHOSH_IS_LAYOUT_MANAGER (self));
  g_return_if_fail (network_shift && indicators_shift);

  *network_shift = self->network_box_shift;
  *indicators_shift = self->indicators_box_shift;
}
