/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-drag-surface"

#include "phosh-config.h"

#include "phosh-enums.h"
#include "drag-surface.h"
#include "layersurface-priv.h"
#include "phosh-marshalers.h"

/**
 * PhoshDragSurface:
 *
 * A drgable layer surface
 *
 * A layer surface that can be dragged in ne direction via gestures.
 *
 * See #PhoshTopPanel for a usage example. Note that you need to
 * update folded/unfolded margins on the #PhoshLayerSurface's
 * `configured` event to adjust it to the proper sizes.
 */

enum {
  PROP_0,
  PROP_LAYER_SHELL_EFFECTS,
  PROP_MARGIN_FOLDED,
  PROP_MARGIN_UNFOLDED,
  PROP_THRESHOLD,
  PROP_DRAG_MODE,
  PROP_DRAG_HANDLE,
  PROP_DRAG_STATE,
  PROP_EXCLUSIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


enum {
  SIGNAL_DRAGGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


typedef struct _PhoshDragSurfacePrivate {
  struct zphoc_layer_shell_effects_v1     *layer_shell_effects;
  struct zphoc_draggable_layer_surface_v1 *drag_surface;

  int                                      margin_folded;
  int                                      margin_unfolded;
  double                                   threshold;
  PhoshDragSurfaceState                    drag_state;
  PhoshDragSurfaceDragMode                 drag_mode;
  guint                                    drag_handle;
  guint                                    exclusive;
} PhoshDragSurfacePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshDragSurface, phosh_drag_surface, PHOSH_TYPE_LAYER_SURFACE)


static void
phosh_drag_surface_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (object);
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);

  switch (property_id) {
  case PROP_LAYER_SHELL_EFFECTS:
    priv->layer_shell_effects = g_value_get_pointer (value);
    break;
  case PROP_MARGIN_FOLDED:
    phosh_drag_surface_set_margin (self, g_value_get_int (value), priv->margin_unfolded);
    break;
  case PROP_MARGIN_UNFOLDED:
    phosh_drag_surface_set_margin (self, priv->margin_folded, g_value_get_int (value));
    break;
  case PROP_THRESHOLD:
    phosh_drag_surface_set_threshold (self, g_value_get_double (value));
    break;
  case PROP_DRAG_MODE:
    phosh_drag_surface_set_drag_mode (self, g_value_get_enum (value));
    break;
  case PROP_DRAG_HANDLE:
    phosh_drag_surface_set_drag_handle (self, g_value_get_uint (value));
    break;
  case PROP_EXCLUSIVE:
    phosh_drag_surface_set_exclusive (self, g_value_get_uint (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_drag_surface_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (object);
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);

  switch (property_id) {
  case PROP_LAYER_SHELL_EFFECTS:
    g_value_set_pointer (value, priv->layer_shell_effects);
    break;
  case PROP_MARGIN_FOLDED:
    g_value_set_int (value, priv->margin_folded);
    break;
  case PROP_MARGIN_UNFOLDED:
    g_value_set_int (value, priv->margin_unfolded);
    break;
  case PROP_THRESHOLD:
    g_value_set_double (value, priv->threshold);
    break;
  case PROP_DRAG_MODE:
    g_value_set_enum (value, priv->drag_mode);
    break;
  case PROP_DRAG_HANDLE:
    g_value_set_uint (value, priv->drag_handle);
    break;
  case PROP_DRAG_STATE:
    g_value_set_enum (value, priv->drag_state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
drag_surface_handle_drag_end (void                                    *data,
                              struct zphoc_draggable_layer_surface_v1 *drag_surface_,
                              uint32_t                                 state)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (data);
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));

  priv = phosh_drag_surface_get_instance_private (self);

  if (state == priv->drag_state)
    return;

  priv->drag_state = state;
  g_debug ("DragSurface %p: state, %d", self, priv->drag_state);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_STATE]);
}


static void
drag_surface_handle_dragged (void                                    *data,
                             struct zphoc_draggable_layer_surface_v1 *drag_surface_,
                             int                                      margin)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (data);
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));

  priv = phosh_drag_surface_get_instance_private (self);

  g_signal_emit (self, signals[SIGNAL_DRAGGED], 0, margin, TRUE);

  if (priv->drag_state == PHOSH_DRAG_SURFACE_STATE_DRAGGED)
    return;

  priv->drag_state = PHOSH_DRAG_SURFACE_STATE_DRAGGED;
  g_debug ("DragSurface %p: state, %d", self, priv->drag_state);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_STATE]);
}


static void
drag_surface_handle_animated (void                                   *data,
                             struct zphoc_draggable_layer_surface_v1 *drag_surface_,
                             int                                      margin)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (data);
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));

  priv = phosh_drag_surface_get_instance_private (self);

  g_signal_emit (self, signals[SIGNAL_DRAGGED], 0, margin, FALSE);

  if (priv->drag_state == PHOSH_DRAG_SURFACE_STATE_ANIMATED)
    return;

  priv->drag_state = PHOSH_DRAG_SURFACE_STATE_ANIMATED;
  g_debug ("DragSurface %p: state, %d", self, priv->drag_state);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_STATE]);
}


const struct zphoc_draggable_layer_surface_v1_listener drag_surface_listener = {
  .drag_end = drag_surface_handle_drag_end,
  .dragged = drag_surface_handle_dragged,
  .animated = drag_surface_handle_animated,
};


static void
phosh_drag_surface_configured (PhoshLayerSurface *layer_surface)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (layer_surface);
  PhoshLayerSurfaceClass *parent_class = PHOSH_LAYER_SURFACE_CLASS (phosh_drag_surface_parent_class);
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);
  struct zwlr_layer_surface_v1 *wl_layer_surface = phosh_layer_surface_get_layer_surface (layer_surface);

  if (parent_class->configured)
    parent_class->configured (layer_surface);

  if (priv->drag_surface)
    return;

  /* Configure drag surface if not done yet */
  priv->drag_surface = zphoc_layer_shell_effects_v1_get_draggable_layer_surface (priv->layer_shell_effects,
                                                                                 wl_layer_surface);
  zphoc_draggable_layer_surface_v1_add_listener (priv->drag_surface, &drag_surface_listener, self);
}


static enum zphoc_draggable_layer_surface_v1_drag_mode
drag_mode_to_phoc_drag_mode (PhoshDragSurfaceDragMode mode)
{
  switch (mode) {
  case PHOSH_DRAG_SURFACE_DRAG_MODE_FULL:
    return ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_FULL;
    break;
  case PHOSH_DRAG_SURFACE_DRAG_MODE_HANDLE:
    return ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_HANDLE;
    break;
  case PHOSH_DRAG_SURFACE_DRAG_MODE_NONE:
    return ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_NONE;
    break;
  default:
    g_return_val_if_reached (ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_MODE_FULL);
  }
}


static void
on_phosh_drag_surface_mapped (PhoshDragSurface *self, gpointer unused)
{
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);
  enum zphoc_draggable_layer_surface_v1_drag_mode drag_mode;

  g_return_if_fail (priv->drag_surface);

  /* Commit initial state when mapped, we don't update any margins as they depend
     on surface size */
  zphoc_draggable_layer_surface_v1_set_threshold (priv->drag_surface,
                                                  wl_fixed_from_double (priv->threshold));
  drag_mode = drag_mode_to_phoc_drag_mode(priv->drag_mode);
  zphoc_draggable_layer_surface_v1_set_drag_mode (priv->drag_surface,
                                                  drag_mode);
  zphoc_draggable_layer_surface_v1_set_drag_handle (priv->drag_surface,
                                                    priv->drag_handle);
  zphoc_draggable_layer_surface_v1_set_exclusive (priv->drag_surface,
                                                  priv->exclusive);
  phosh_layer_surface_wl_surface_commit (PHOSH_LAYER_SURFACE (self));
}


static void
phosh_drag_surface_constructed (GObject *object)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (object);

  G_OBJECT_CLASS (phosh_drag_surface_parent_class)->constructed (object);

  g_signal_connect (self, "map",
                    G_CALLBACK (on_phosh_drag_surface_mapped),
                    NULL);
}

static void
phosh_drag_surface_dispose (GObject *object)
{
  PhoshDragSurface *self = PHOSH_DRAG_SURFACE (object);
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);

  g_clear_pointer (&priv->drag_surface, zphoc_draggable_layer_surface_v1_destroy);

  G_OBJECT_CLASS (phosh_drag_surface_parent_class)->dispose (object);
}


static void
phosh_drag_surface_class_init (PhoshDragSurfaceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshLayerSurfaceClass *layer_surface_class = PHOSH_LAYER_SURFACE_CLASS (klass);

  object_class->get_property = phosh_drag_surface_get_property;
  object_class->set_property = phosh_drag_surface_set_property;
  object_class->constructed = phosh_drag_surface_constructed;
  object_class->dispose = phosh_drag_surface_dispose;

  layer_surface_class->configured = phosh_drag_surface_configured;

  props[PROP_LAYER_SHELL_EFFECTS] = g_param_spec_pointer ("layer-shell-effects", "", "",
                                                          G_PARAM_READWRITE |
                                                          G_PARAM_CONSTRUCT_ONLY |
                                                          G_PARAM_STATIC_STRINGS);

  props[PROP_MARGIN_FOLDED] = g_param_spec_int ("margin-folded", "", "",
                                                G_MININT, G_MAXINT, 0,
                                                G_PARAM_READWRITE |
                                                G_PARAM_STATIC_STRINGS |
                                                G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_MARGIN_UNFOLDED] = g_param_spec_int ("margin-unfolded", "", "",
                                                  G_MININT, G_MAXINT, 0,
                                                  G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS |
                                                  G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_THRESHOLD] = g_param_spec_double ("threshold", "", "",
                                               G_MINDOUBLE, G_MAXDOUBLE, 1.0,
                                               G_PARAM_READWRITE |
                                               G_PARAM_STATIC_STRINGS |
                                               G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DRAG_MODE] = g_param_spec_enum ("drag-mode", "", "",
                                             PHOSH_TYPE_DRAG_SURFACE_DRAG_MODE,
                                             0,
                                             G_PARAM_READWRITE |
                                             G_PARAM_STATIC_STRINGS |
                                             G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DRAG_HANDLE] = g_param_spec_uint ("drag-handle", "", "",
                                               0, G_MAXUINT, 0,
                                               G_PARAM_READWRITE |
                                               G_PARAM_STATIC_STRINGS |
                                               G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_DRAG_STATE] = g_param_spec_enum ("drag-state", "", "",
                                              PHOSH_TYPE_DRAG_SURFACE_STATE,
                                              PHOSH_DRAG_SURFACE_STATE_FOLDED,
                                              G_PARAM_READABLE |
                                              G_PARAM_STATIC_STRINGS |
                                              G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_EXCLUSIVE] = g_param_spec_uint ("exclusive", "", "",
                                             0, G_MAXUINT, 0,
                                             G_PARAM_READWRITE |
                                             G_PARAM_STATIC_STRINGS |
                                             G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshDragSurface::dragged
   * @self: The drag surface
   * @margin: How far the surface is dragged
   * @user: `TRUE` when the user drags the surface, `FALSE` if the compositor animates it.
   *
   * Invoked when the surface is being dragged
   */
  signals[SIGNAL_DRAGGED] = g_signal_new ("dragged",
                                          G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                          G_STRUCT_OFFSET (PhoshDragSurfaceClass, dragged),
                                          NULL, NULL,
                                          _phosh_marshal_VOID__INT_BOOLEAN,
                                          G_TYPE_NONE,
                                          2,
                                          G_TYPE_INT,
                                          G_TYPE_BOOLEAN);
  g_signal_set_va_marshaller (signals[SIGNAL_DRAGGED],
                              G_TYPE_FROM_CLASS (klass),
                              _phosh_marshal_VOID__INT_BOOLEANv);

}


static void
phosh_drag_surface_init (PhoshDragSurface *self)
{
}


void
phosh_drag_surface_set_margin (PhoshDragSurface *self, int margin_folded, int margin_unfolded)
{
  PhoshDragSurfacePrivate *priv;
  gboolean changed = FALSE;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));

  priv = phosh_drag_surface_get_instance_private (self);

  if (priv->margin_folded != margin_folded) {
    priv->margin_folded = margin_folded;
    changed = TRUE;
  }

  if (priv->margin_unfolded != margin_unfolded) {
    priv->margin_folded = margin_folded;
    changed = TRUE;
  }

  if (changed == FALSE)
    return;

  if (priv->drag_surface)
    zphoc_draggable_layer_surface_v1_set_margins (priv->drag_surface,
                                                  priv->margin_folded,
                                                  priv->margin_unfolded);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_FOLDED]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MARGIN_UNFOLDED]);
}


float
phosh_drag_surface_get_threshold (PhoshDragSurface *self)
{
  PhoshDragSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_DRAG_SURFACE (self), 0);
  priv = phosh_drag_surface_get_instance_private (self);

  return priv->threshold;
}


void
phosh_drag_surface_set_threshold (PhoshDragSurface *self, double threshold)
{
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));

  priv = phosh_drag_surface_get_instance_private (self);

  if (G_APPROX_VALUE (priv->threshold, threshold, FLT_EPSILON))
    return;

  priv->threshold = threshold;
  if (priv->drag_surface)
    zphoc_draggable_layer_surface_v1_set_threshold (priv->drag_surface,
                                                    wl_fixed_from_double (priv->threshold));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_THRESHOLD]);
}


PhoshDragSurfaceState
phosh_drag_surface_get_drag_state (PhoshDragSurface *self)
{
  PhoshDragSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_DRAG_SURFACE (self), 0);
  priv = phosh_drag_surface_get_instance_private (self);

  return priv->drag_state;
}


void
phosh_drag_surface_set_drag_state (PhoshDragSurface     *self,
                                   PhoshDragSurfaceState state)
{
  PhoshDragSurfacePrivate *priv;
  enum zphoc_draggable_layer_surface_v1_drag_end_state drag_state;

  g_return_if_fail (state >= PHOSH_DRAG_SURFACE_STATE_FOLDED &&
                    state <= PHOSH_DRAG_SURFACE_STATE_UNFOLDED);
  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));
  priv = phosh_drag_surface_get_instance_private (self);

  if (priv->drag_state == state)
    return;

  switch (state) {
  case PHOSH_DRAG_SURFACE_STATE_FOLDED:
    drag_state = ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_FOLDED;
    break;
  case PHOSH_DRAG_SURFACE_STATE_UNFOLDED:
    drag_state = ZPHOC_DRAGGABLE_LAYER_SURFACE_V1_DRAG_END_STATE_UNFOLDED;
    break;
  case PHOSH_DRAG_SURFACE_STATE_ANIMATED:
  case PHOSH_DRAG_SURFACE_STATE_DRAGGED:
  default:
    g_return_if_reached ();
  }

  if (priv->drag_surface)
    zphoc_draggable_layer_surface_v1_set_state (priv->drag_surface, drag_state);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_STATE]);
}


void
phosh_drag_surface_set_exclusive (PhoshDragSurface *self, guint exclusive)

{
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));
  priv = phosh_drag_surface_get_instance_private (self);

  if (priv->exclusive == exclusive)
    return;

  priv->exclusive = exclusive;
  if (priv->drag_surface)
      zphoc_draggable_layer_surface_v1_set_exclusive (priv->drag_surface, exclusive);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_EXCLUSIVE]);
}


PhoshDragSurfaceDragMode
phosh_drag_surface_get_drag_mode (PhoshDragSurface *self)
{
  PhoshDragSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_DRAG_SURFACE (self), 0);
  priv = phosh_drag_surface_get_instance_private (self);

  return priv->drag_mode;
}


void
phosh_drag_surface_set_drag_mode (PhoshDragSurface *self, PhoshDragSurfaceDragMode mode)

{
  PhoshDragSurfacePrivate *priv;
  enum zphoc_draggable_layer_surface_v1_drag_mode drag_mode;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));
  priv = phosh_drag_surface_get_instance_private (self);

  if (priv->drag_mode == mode)
    return;
  priv->drag_mode = mode;

  if (priv->drag_surface) {
    drag_mode = drag_mode_to_phoc_drag_mode(priv->drag_mode);
    zphoc_draggable_layer_surface_v1_set_drag_mode (priv->drag_surface, drag_mode);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_MODE]);
}


void
phosh_drag_surface_set_drag_handle (PhoshDragSurface *self, guint handle)

{
  PhoshDragSurfacePrivate *priv;

  g_return_if_fail (PHOSH_IS_DRAG_SURFACE (self));
  priv = phosh_drag_surface_get_instance_private (self);

  if (priv->drag_handle == handle)
    return;
  priv->drag_handle = handle;

  if (priv->drag_surface)
    zphoc_draggable_layer_surface_v1_set_drag_handle (priv->drag_surface, handle);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_HANDLE]);
}


guint
phosh_drag_surface_get_drag_handle (PhoshDragSurface *self)
{
  PhoshDragSurfacePrivate *priv;

  g_return_val_if_fail (PHOSH_IS_DRAG_SURFACE (self), 0);
  priv = phosh_drag_surface_get_instance_private (self);

  return priv->drag_handle;
}

/**
 * phosh_drag_surface_is_moving:
 * @self: The drag surface
 *
 * Whether the surface is currently moving, either by the user or animated by the compositor.x
 *
 * Returns: `TRUE` if the suface is moving, otherwise `FALSE`.
 */
gboolean
phosh_drag_surface_is_moving (PhoshDragSurface *self)
{
  PhoshDragSurfacePrivate *priv = phosh_drag_surface_get_instance_private (self);
  gboolean moving;

  g_return_val_if_fail (PHOSH_IS_DRAG_SURFACE (self), FALSE);

  moving = (priv->drag_state == PHOSH_DRAG_SURFACE_STATE_DRAGGED ||
            priv->drag_state == PHOSH_DRAG_SURFACE_STATE_ANIMATED);

  return moving;
}
