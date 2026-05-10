/*
 * Copyright (C) 2018-2023 Purism SPC
 *               2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-layer-surface"

#include "phosh-config.h"
#include "layersurface-priv.h"
#include "phosh-wayland.h"
#include "phoc-layer-shell-effects-unstable-v1-client-protocol.h"

#include <gdk/gdkwayland.h>

/**
 * PhoshLayerSurface:
 *
 * A #GtkWindow rendered as a LayerSurface by the compositor
 *
 * #PhoshLayerSurface allows to use a Wayland surface backed by the
 * layer-shell protocol as #GtkWindow. This allows to render e.g. panels and
 * backgrounds using GTK.
 */

enum {
  PROP_0,
  PROP_LAYER_SHELL,
  PROP_WL_OUTPUT,
  PROP_ANCHOR,
  PROP_LAYER,
  PROP_KBD_INTERACTIVITY,
  PROP_EXCLUSIVE_ZONE,
  PROP_MARGIN_TOP,
  PROP_MARGIN_BOTTOM,
  PROP_MARGIN_LEFT,
  PROP_MARGIN_RIGHT,
  PROP_LAYER_WIDTH,
  PROP_LAYER_HEIGHT,
  PROP_CONFIGURED_WIDTH,
  PROP_CONFIGURED_HEIGHT,
  PROP_NAMESPACE,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

enum {
  CONFIGURED,
  N_SIGNALS
};
static guint signals [N_SIGNALS];

typedef struct {
  struct wl_surface            *wl_surface;
  struct zwlr_layer_surface_v1 *layer_surface;
  struct zphoc_alpha_layer_surface_v1 *alpha_surface;
  struct zphoc_stacked_layer_surface_v1 *stacked_surface;

  /* Properties */
  guint                         anchor;
  guint                         layer;
  gboolean                      kbd_interactivity;
  int                           exclusive_zone;
  int                           margin_top, margin_bottom;
  int                           margin_left, margin_right;
  int                           width, height;
  int                           configured_width, configured_height;
  char                         *namespace;
  struct zwlr_layer_shell_v1   *layer_shell;
  struct wl_output             *wl_output;
  /* alpha_layer_surface_v1 */
  double                        alpha;
  /* stacked_layer_surface_v1 */
  PhoshLayerSurface            *stack_target;
  gboolean                      stack_above;
} PhoshLayerSurfacePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshLayerSurface, phosh_layer_surface, GTK_TYPE_WINDOW)


static void
layer_surface_configure (void                         *data,
                         struct zwlr_layer_surface_v1 *surface,
                         uint32_t                      serial,
                         uint32_t                      width,
                         uint32_t                      height)
{
  PhoshLayerSurface *self = data;
  PhoshLayerSurfacePrivate *priv;
  gboolean changed = FALSE;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);
  gtk_window_resize (GTK_WINDOW (self), width, height);
  zwlr_layer_surface_v1_ack_configure (surface, serial);

  if (priv->configured_height != height) {
    priv->configured_height = height;
    changed = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIGURED_HEIGHT]);
  }

  if (priv->configured_width != width) {
    priv->configured_width = width;
    changed = TRUE;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONFIGURED_WIDTH]);
  }

  g_debug ("Configured '%s' (%p) (%dx%d)", priv->namespace, self, width, height);
  if (changed)
    g_signal_emit (self, signals[CONFIGURED], 0);
}


static void
layer_surface_closed (void                         *data,
                      struct zwlr_layer_surface_v1 *surface)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (data);
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  g_return_if_fail (priv->layer_surface == surface);
  g_debug ("Destroying layer surface '%s' (%p)", priv->namespace, self);
  zwlr_layer_surface_v1_destroy (priv->layer_surface);
  priv->layer_surface = NULL;
  gtk_widget_destroy (GTK_WIDGET (self));
}


static struct zwlr_layer_surface_v1_listener layer_surface_listener = {
  .configure = layer_surface_configure,
  .closed = layer_surface_closed,
};


static void
set_alpha (PhoshLayerSurface *self, double alpha)
{
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);

  priv->alpha = alpha;

  if (!priv->alpha_surface)
    return;

  zphoc_alpha_layer_surface_v1_set_alpha (priv->alpha_surface, wl_fixed_from_double (alpha));
  wl_surface_commit (priv->wl_surface);
}


static void
phosh_layer_surface_set_stacked (PhoshLayerSurface *self, PhoshLayerSurface *target, gboolean above)
{
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);
  PhoshLayerSurfacePrivate *target_priv = phosh_layer_surface_get_instance_private (target);

  if (priv->stack_target)
    g_object_remove_weak_pointer (G_OBJECT (priv->stack_target),
                                  (gpointer *)&priv->stack_target);

  priv->stack_target = target;

  if (priv->stack_target) {
    g_object_add_weak_pointer (G_OBJECT (priv->stack_target),
                               (gpointer *)&priv->stack_target);
  } else {
    /* There's no "remove stacking" so we're done */
    return;
  }

  if (priv->stacked_surface == NULL) {
    g_debug ("Trying to stack an unmapped layer surface '%s'", priv->namespace);
    return;
  }

  if (target_priv->stacked_surface == NULL) {
    g_debug ("Trying to stack above an unmapped layer surface '%s'", target_priv->namespace);
    return;
  }

  if (above)
    zphoc_stacked_layer_surface_v1_stack_above (priv->stacked_surface, target_priv->layer_surface);
  else
    zphoc_stacked_layer_surface_v1_stack_below (priv->stacked_surface, target_priv->layer_surface);

  wl_surface_commit (priv->wl_surface);
}


static void
phosh_layer_surface_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (object);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);
  int width, height;

  switch (property_id) {
  case PROP_LAYER_SHELL:
    priv->layer_shell = g_value_get_pointer (value);
    break;
  case PROP_WL_OUTPUT:
    priv->wl_output = g_value_get_pointer (value);
    break;
  case PROP_ANCHOR:
    priv->anchor = g_value_get_uint (value);
    break;
  case PROP_LAYER:
    phosh_layer_surface_set_layer (self, g_value_get_uint (value));
    break;
  case PROP_KBD_INTERACTIVITY:
    phosh_layer_surface_set_kbd_interactivity (self, g_value_get_boolean (value));
    break;
  case PROP_EXCLUSIVE_ZONE:
    phosh_layer_surface_set_exclusive_zone (self, g_value_get_int (value));
    break;
  case PROP_MARGIN_TOP:
    phosh_layer_surface_set_margins (self,
                                     g_value_get_int (value),
                                     priv->margin_right,
                                     priv->margin_bottom,
                                     priv->margin_left);
    break;
  case PROP_MARGIN_BOTTOM:
    phosh_layer_surface_set_margins (self,
                                     priv->margin_top,
                                     priv->margin_right,
                                     g_value_get_int (value),
                                     priv->margin_left);
    break;
  case PROP_MARGIN_LEFT:
    phosh_layer_surface_set_margins (self,
                                     priv->margin_top,
                                     priv->margin_right,
                                     priv->margin_bottom,
                                     g_value_get_int (value));
    break;
  case PROP_MARGIN_RIGHT:
    phosh_layer_surface_set_margins (self,
                                     priv->margin_top,
                                     g_value_get_int (value),
                                     priv->margin_bottom,
                                     priv->margin_left);
    break;
  case PROP_LAYER_WIDTH:
    width = g_value_get_uint (value);
    phosh_layer_surface_set_size (self, width, priv->height);
    break;
  case PROP_LAYER_HEIGHT:
    height = g_value_get_uint (value);
    phosh_layer_surface_set_size (self, priv->width, height);
    break;
  case PROP_NAMESPACE:
    g_free (priv->namespace);
    priv->namespace = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_layer_surface_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (object);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);

  switch (property_id) {
  case PROP_LAYER_SHELL:
    g_value_set_pointer (value, priv->layer_shell);
    break;
  case PROP_WL_OUTPUT:
    g_value_set_pointer (value, priv->wl_output);
    break;
  case PROP_ANCHOR:
    g_value_set_uint (value, priv->anchor);
    break;
  case PROP_LAYER:
    g_value_set_uint (value, phosh_layer_surface_get_layer (self));
    break;
  case PROP_KBD_INTERACTIVITY:
    g_value_set_boolean (value, priv->kbd_interactivity);
    break;
  case PROP_EXCLUSIVE_ZONE:
    g_value_set_int (value, priv->exclusive_zone);
    break;
  case PROP_MARGIN_TOP:
    g_value_set_int (value, priv->margin_top);
    break;
  case PROP_MARGIN_BOTTOM:
    g_value_set_int (value, priv->margin_bottom);
    break;
  case PROP_MARGIN_LEFT:
    g_value_set_int (value, priv->margin_left);
    break;
  case PROP_MARGIN_RIGHT:
    g_value_set_int (value, priv->margin_right);
    break;
  case PROP_LAYER_WIDTH:
    g_value_set_uint (value, priv->width);
    break;
  case PROP_LAYER_HEIGHT:
    g_value_set_uint (value, priv->height);
    break;
  case PROP_CONFIGURED_WIDTH:
    g_value_set_uint (value, priv->configured_width);
    break;
  case PROP_CONFIGURED_HEIGHT:
    g_value_set_uint (value, priv->configured_height);
    break;
  case PROP_NAMESPACE:
    g_value_set_string (value, priv->namespace);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_layer_surface_realize (GtkWidget *widget)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (widget);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);
  GdkWindow *gdk_window;

  GTK_WIDGET_CLASS (phosh_layer_surface_parent_class)->realize (widget);

  gdk_window = gtk_widget_get_window (GTK_WIDGET (self));
  gdk_wayland_window_set_use_custom_surface (gdk_window);
  priv->wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);

  gtk_window_set_decorated (GTK_WINDOW (self), FALSE);
}


static void
phosh_layer_surface_map (GtkWidget *widget)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (widget);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);
  PhoshWayland *wl = phosh_wayland_get_default ();
  struct zphoc_layer_shell_effects_v1 *layer_shell_effects;

  GTK_WIDGET_CLASS (phosh_layer_surface_parent_class)->map (widget);

  if (!priv->wl_surface) {
    GdkWindow *gdk_window;

    gdk_window = gtk_widget_get_window (GTK_WIDGET (self));
    gdk_wayland_window_set_use_custom_surface (gdk_window);
    priv->wl_surface = gdk_wayland_window_get_wl_surface (gdk_window);
  }
  g_debug ("Mapped '%s' (%p)", priv->namespace, self);

  priv->layer_surface = zwlr_layer_shell_v1_get_layer_surface (priv->layer_shell,
                                                               priv->wl_surface,
                                                               priv->wl_output,
                                                               priv->layer,
                                                               priv->namespace);
  zwlr_layer_surface_v1_set_exclusive_zone (priv->layer_surface, priv->exclusive_zone);
  zwlr_layer_surface_v1_set_size (priv->layer_surface, priv->width, priv->height);
  zwlr_layer_surface_v1_set_anchor (priv->layer_surface, priv->anchor);
  zwlr_layer_surface_v1_set_margin (priv->layer_surface,
                                    priv->margin_top,
                                    priv->margin_right,
                                    priv->margin_bottom,
                                    priv->margin_left);
  zwlr_layer_surface_v1_set_keyboard_interactivity (priv->layer_surface, priv->kbd_interactivity);
  zwlr_layer_surface_v1_add_listener (priv->layer_surface,
                                      &layer_surface_listener,
                                      self);
  wl_surface_commit (priv->wl_surface);

  /* Process all pending events, otherwise we end up sending ack configure
   * to a not yet configured surface */
  wl_display_roundtrip (gdk_wayland_display_get_wl_display (gdk_display_get_default ()));

  layer_shell_effects = phosh_wayland_get_zphoc_layer_shell_effects_v1 (wl);
  priv->alpha_surface =
    zphoc_layer_shell_effects_v1_get_alpha_layer_surface (layer_shell_effects,
                                                          priv->layer_surface);
  priv->stacked_surface =
    zphoc_layer_shell_effects_v1_get_stacked_layer_surface (layer_shell_effects,
                                                            priv->layer_surface);

  /* Catch up with alpha values set before map */
  if (!G_APPROX_VALUE (priv->alpha, 1.0, FLT_EPSILON))
    set_alpha (self, priv->alpha);

  /* Catch up with stackings set before map */
  if (priv->stacked_surface)
    phosh_layer_surface_set_stacked (self, priv->stack_target, priv->stack_above);
}


static void
phosh_layer_surface_unmap (GtkWidget *widget)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (widget);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);

  g_clear_pointer (&priv->alpha_surface, zphoc_alpha_layer_surface_v1_destroy);
  g_clear_pointer (&priv->stacked_surface, zphoc_stacked_layer_surface_v1_destroy);
  g_clear_pointer (&priv->layer_surface, zwlr_layer_surface_v1_destroy);
  priv->wl_surface = NULL;

  GTK_WIDGET_CLASS (phosh_layer_surface_parent_class)->unmap (widget);
}


static void
phosh_layer_surface_configured_impl (PhoshLayerSurface *layer_surface)
{
  /* Nothing todo here */
}


static void
phosh_layer_surface_dispose (GObject *object)
{
  PhoshLayerSurface *self = PHOSH_LAYER_SURFACE (object);
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);

  phosh_layer_surface_set_stacked (self, NULL, FALSE);
  g_clear_pointer (&priv->alpha_surface, zphoc_alpha_layer_surface_v1_destroy);
  g_clear_pointer (&priv->stacked_surface, zphoc_stacked_layer_surface_v1_destroy);
  g_clear_pointer (&priv->layer_surface, zwlr_layer_surface_v1_destroy);
  g_clear_pointer (&priv->namespace, g_free);

  G_OBJECT_CLASS (phosh_layer_surface_parent_class)->dispose (object);
}


static void
phosh_layer_surface_class_init (PhoshLayerSurfaceClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;
  PhoshLayerSurfaceClass *layer_surface_class = PHOSH_LAYER_SURFACE_CLASS (klass);

  object_class->dispose = phosh_layer_surface_dispose;
  object_class->set_property = phosh_layer_surface_set_property;
  object_class->get_property = phosh_layer_surface_get_property;

  widget_class->realize = phosh_layer_surface_realize;
  widget_class->map = phosh_layer_surface_map;
  widget_class->unmap = phosh_layer_surface_unmap;

  layer_surface_class->configured = phosh_layer_surface_configured_impl;

  props[PROP_LAYER_SHELL] =
    g_param_spec_pointer (
      "layer-shell",
      "Wayland Layer Shell Global",
      "The layer shell wayland global",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_WL_OUTPUT] =
    g_param_spec_pointer (
      "wl-output",
      "Wayland Output",
      "The wl_output associated with this surface",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_ANCHOR] =
    g_param_spec_uint (
      "anchor",
      "Anchor edges",
      "The edges to anchor the surface to",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_LAYER] =
    g_param_spec_uint (
      "layer",
      "Layer",
      "The layer the surface should be attached to",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE |
      G_PARAM_CONSTRUCT |
      G_PARAM_STATIC_STRINGS |
      G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_KBD_INTERACTIVITY] =
    g_param_spec_boolean (
      "kbd-interactivity",
      "Keyboard interactivity",
      "Whether the surface interacts with the keyboard",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_EXCLUSIVE_ZONE] =
    g_param_spec_int (
      "exclusive-zone",
      "Exclusive Zone",
      "Set area that is not occluded with other surfaces",
      -1,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MARGIN_LEFT] =
    g_param_spec_int (
      "margin-left",
      "Left margin",
      "Distance away from the left anchor point",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MARGIN_RIGHT] =
    g_param_spec_int (
      "margin-right",
      "Right margin",
      "Distance away from the right anchor point",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MARGIN_TOP] =
    g_param_spec_int (
      "margin-top",
      "Top margin",
      "Distance away from the top anchor point",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MARGIN_BOTTOM] =
    g_param_spec_int (
      "margin-bottom",
      "Bottom margin",
      "Distance away from the bottom anchor point",
      G_MININT,
      G_MAXINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_LAYER_WIDTH] =
    g_param_spec_uint (
      "width",
      "Width",
      "The width of the layer surface",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_LAYER_HEIGHT] =
    g_param_spec_uint (
      "height",
      "Height",
      "The height of the layer surface",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);


  props[PROP_CONFIGURED_WIDTH] =
    g_param_spec_uint (
      "configured-width",
      "Configured width",
      "The width of the layer surface set by the compositor",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_CONFIGURED_HEIGHT] =
    g_param_spec_uint (
      "configured-height",
      "Configured height",
      "The height of the layer surface set by the compositor",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_NAMESPACE] =
    g_param_spec_string (
      "namespace",
      "Namespace",
      "Namespace of the layer surface",
      "",
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  /**
   * PhoshLayerSurface::configured
   * @self: The #PhoshLayerSurface instance.
   *
   * This signal is emitted once we received the configure event from the
   * compositor.
   */
  signals[CONFIGURED] =
    g_signal_new ("configured",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (PhoshLayerSurfaceClass, configured),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}


static void
phosh_layer_surface_init (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv = phosh_layer_surface_get_instance_private (self);

  priv->alpha = 1.0;
}


GtkWidget *
phosh_layer_surface_new (gpointer layer_shell,
                         gpointer wl_output)
{
  return g_object_new (PHOSH_TYPE_LAYER_SURFACE,
                       "layer-shell", layer_shell,
                       "wl-output", wl_output,
                       NULL);
}


/**
 * phosh_layer_surface_get_surface:
 * @self: The #PhoshLayerSurface
 *
 * Get the layer layer surface or #NULL if the window
 * is not yet realized.
 */
struct zwlr_layer_surface_v1 *
phosh_layer_surface_get_layer_surface (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), NULL);
  priv = phosh_layer_surface_get_instance_private (self);
  return priv->layer_surface;
}


/**
 * phosh_layer_surface_get_wl_surface:
 * @self: The #PhoshLayerSurface
 *
 * Get the layer wayland surface or #NULL if the window
 * is not yet realized.
 */
struct wl_surface *
phosh_layer_surface_get_wl_surface (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), NULL);
  priv = phosh_layer_surface_get_instance_private (self);
  return priv->wl_surface;
}


/**
 * phosh_layer_surface_set_size:
 * @self: The #PhoshLayerSurface
 * @width: the height in pixels
 * @height: the width in pixels
 *
 * Set the size of a layer surface. A value of '-1' indicates 'use old value'
 */
void
phosh_layer_surface_set_size (PhoshLayerSurface *self, int width, int height)
{
  PhoshLayerSurfacePrivate *priv;
  int old_width, old_height;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->height == height && priv->width == width)
    return;

  old_width = priv->width;
  old_height = priv->height;

  if (width != -1)
    priv->width = width;

  if (height != -1)
    priv->height = height;

  if (gtk_widget_get_mapped (GTK_WIDGET (self))) {
    zwlr_layer_surface_v1_set_size (priv->layer_surface, priv->width, priv->height);
  }

  if (priv->height != old_height)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LAYER_HEIGHT]);

  if (priv->width != old_width)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LAYER_WIDTH]);
}


/**
 * phosh_layer_surface_set_margins:
 * @self: The #PhoshLayerSurface
 * @top: the top margin in pixels
 * @right: the right margin in pixels
 * @bottom: the bottom margin in pixels
 * @left: the left margin in pixels
 *
 * Set anchor margins of a layer surface.
 */
void
phosh_layer_surface_set_margins (PhoshLayerSurface *self, int top, int right, int bottom, int left)
{
  PhoshLayerSurfacePrivate *priv;
  int old_top, old_bottom, old_left, old_right;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  old_top = priv->margin_top;
  old_left = priv->margin_left;
  old_right = priv->margin_right;
  old_bottom = priv->margin_bottom;

  if (old_top == top && old_left == left && old_right == right && old_bottom == bottom)
    return;

  priv->margin_top = top;
  priv->margin_left = left;
  priv->margin_right = right;
  priv->margin_bottom = bottom;

  if (priv->layer_surface)
    zwlr_layer_surface_v1_set_margin (priv->layer_surface, top, right, bottom, left);

  if (old_top != top)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_TOP]);
  if (old_bottom != bottom)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_BOTTOM]);
  if (old_left != left)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_LEFT]);
  if (old_right != right)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_RIGHT]);
}


/**
 * phosh_layer_surface_set_exclusive_zone:
 * @self: The #PhoshLayerSurface
 * @zone: Size of the exclusive zone.
 *
 * Set exclusive zone of a layer surface.
 */
void
phosh_layer_surface_set_exclusive_zone (PhoshLayerSurface *self, int zone)
{
  PhoshLayerSurfacePrivate *priv;
  int old_zone;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  old_zone = priv->exclusive_zone;

  if (old_zone == zone)
    return;

  priv->exclusive_zone = zone;

  if (priv->layer_surface)
    zwlr_layer_surface_v1_set_exclusive_zone (priv->layer_surface, zone);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_EXCLUSIVE_ZONE]);
}


/**
 * phosh_layer_surface_set_keyboard_interactivity:
 * @self: The #PhoshLayerSurface
 * @interactivity: %TRUE if the #PhoshLayerSurface should receive keyboard input.
 *
 * Set keyboard ineractivity a layer surface.
 */
void
phosh_layer_surface_set_kbd_interactivity (PhoshLayerSurface *self, gboolean interactivity)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->kbd_interactivity == interactivity)
    return;

  priv->kbd_interactivity = interactivity;

  if (priv->layer_surface)
    zwlr_layer_surface_v1_set_keyboard_interactivity (priv->layer_surface, interactivity);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_KBD_INTERACTIVITY]);
}

/**
 * phosh_layer_surface_get_layer:
 * @self: The #PhoshLayerSurface
 *
 * Gets the surfaces current layer.
 *
 * Returns: The layer
 */
guint32
phosh_layer_surface_get_layer (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), 0);
  priv = phosh_layer_surface_get_instance_private (self);

  return priv->layer;
}

/**
 * phosh_layer_surface_set_layer:
 * @self: The #PhoshLayerSurface
 * @layer: The layer.
 *
 * Sets the layer a layer-surface belongs to `layer`.
 */
void
phosh_layer_surface_set_layer (PhoshLayerSurface *self, guint32 layer)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->layer == layer)
    return;

  priv->layer = layer;

  if (priv->layer_surface)
    zwlr_layer_surface_v1_set_layer (priv->layer_surface, layer);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LAYER]);
}

/**
 * phosh_layer_surface_get_wl_output:
 * @self: The #PhoshLayerSurface
 *
 * Gets the output.
 *
 * Returns:(nullable)(transfer none): The output
 */
gpointer
phosh_layer_surface_get_wl_output (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), 0);
  priv = phosh_layer_surface_get_instance_private (self);

  return priv->wl_output;
}

/**
 * phosh_layer_surface_wl_surface_commit:
 * @self: The #PhoshLayerSurface
 *
 * Forces a commit of layer surface's state.
 */
void
phosh_layer_surface_wl_surface_commit (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->wl_surface)
    wl_surface_commit (priv->wl_surface);
}


void
phosh_layer_surface_get_margins (PhoshLayerSurface *self, int *top, int *right, int *bottom, int *left)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  if (top)
    *top = priv->margin_top;

  if (right)
    *right = priv->margin_right;

  if (bottom)
    *bottom = priv->margin_bottom;

  if (left)
    *left = priv->margin_left;
}


int
phosh_layer_surface_get_configured_width (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), 0);
  priv = phosh_layer_surface_get_instance_private (self);

  return priv->configured_width;
}

int
phosh_layer_surface_get_configured_height (PhoshLayerSurface *self)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LAYER_SURFACE (self), 0);
  priv = phosh_layer_surface_get_instance_private (self);

  return priv->configured_height;
}


void
phosh_layer_surface_set_alpha (PhoshLayerSurface *self, double alpha)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  priv = phosh_layer_surface_get_instance_private (self);

  g_return_if_fail (alpha >= 0.0 && alpha <= 1.0);

  if (G_APPROX_VALUE (priv->alpha, alpha, FLT_EPSILON))
    return;

  set_alpha (self, alpha);
}

/**
 * phosh_layer_surface_set_stacked_above:
 * @self: The surface to be stacked
 * @target: The target surface
 *
 * Stacks the surface `self` directly above `target`.
 */
void
phosh_layer_surface_set_stacked_above (PhoshLayerSurface *self, PhoshLayerSurface *target)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (target));

  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->stack_target == target && priv->stack_above == TRUE)
    return;

  phosh_layer_surface_set_stacked (self, target, TRUE);
}

/**
 * phosh_layer_surface_set_stacked_below:
 * @self: The surface to be stacked
 * @target: The target surface
 *
 * Stacks the surface `self` directly below `target`.
 */
void
phosh_layer_surface_set_stacked_below (PhoshLayerSurface *self, PhoshLayerSurface *target)
{
  PhoshLayerSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (self));
  g_return_if_fail (PHOSH_IS_LAYER_SURFACE (target));

  priv = phosh_layer_surface_get_instance_private (self);

  if (priv->stack_target == target && priv->stack_above == FALSE)
    return;

  phosh_layer_surface_set_stacked (self, target, FALSE);
}
