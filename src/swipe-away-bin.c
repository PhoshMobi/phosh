/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Alexander Mikhaylenko <alexm@gnome.org>
 */

#include "phosh-config.h"

#include "animation.h"
#include "swipe-away-bin.h"
#include <handy.h>

enum {
  REMOVED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


enum {
  PROP_0,
  PROP_ENABLED,
  PROP_ALLOW_NEGATIVE,
  PROP_RESERVE_SIZE,
  PROP_ORIENTATION,

  LAST_PROP = PROP_ORIENTATION
};
static GParamSpec *props[LAST_PROP];


struct _PhoshSwipeAwayBin {
  GtkEventBox      parent_instance;

  GtkOrientation   orientation;
  gboolean         allow_negative;
  gboolean         reserve_size;

  double           progress;
  int distance;
  HdySwipeTracker *tracker;
  PhoshAnimation  *animation;

  gboolean         enabled;
};

static void phosh_swipe_away_bin_swipeable_init (HdySwipeableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshSwipeAwayBin, phosh_swipe_away_bin, GTK_TYPE_EVENT_BOX,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_ORIENTABLE, NULL)
                         G_IMPLEMENT_INTERFACE (HDY_TYPE_SWIPEABLE, phosh_swipe_away_bin_swipeable_init))


static void
set_progress (PhoshSwipeAwayBin *self,
              double             progress)
{
  self->progress = progress;

  gtk_widget_set_opacity (GTK_WIDGET (self), hdy_ease_out_cubic (1 - ABS (self->progress)));
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}


static void
animation_value_cb (double             value,
                    PhoshSwipeAwayBin *self)
{
  set_progress (self, value);
}


static gboolean
animation_done_idle_cb (PhoshSwipeAwayBin *self)
{
  g_signal_emit (self, signals[REMOVED], 0);

  return G_SOURCE_REMOVE;
}


static void
animation_done_cb (PhoshSwipeAwayBin *self)
{
  guint id;

  g_clear_pointer (&self->animation, phosh_animation_unref);

  if (ABS (self->progress) < 1)
    return;

  id = g_idle_add ((GSourceFunc) animation_done_idle_cb, self);
  g_source_set_name_by_id (id, "[SwipeAwayBin] idle");
}


static void
animate (PhoshSwipeAwayBin *self,
         gint64             duration,
         double             to,
         PhoshAnimationType type)
{
  self->animation =
    phosh_animation_new (GTK_WIDGET (self),
                         self->progress,
                         to,
                         duration,
                         type,
                         (PhoshAnimationValueCallback) animation_value_cb,
                         (PhoshAnimationDoneCallback) animation_done_cb,
                         self);

  phosh_animation_start (self->animation);
}


static void
begin_swipe_cb (PhoshSwipeAwayBin *self)
{
  if (!self->enabled)
    return;

  if (self->animation)
    phosh_animation_stop (self->animation);
}


static void
update_swipe_cb (PhoshSwipeAwayBin *self,
                 double             progress)
{
  if (!self->enabled)
    return;

  set_progress (self, progress);
}


static void
end_swipe_cb (PhoshSwipeAwayBin *self,
              gint64             duration,
              double             to)
{
  if (!self->enabled)
    return;

  animate (self, duration, to, PHOSH_ANIMATION_TYPE_EASE_OUT_CUBIC);
}


static void
update_orientation (PhoshSwipeAwayBin *self)
{
  gboolean reversed =
    self->orientation == GTK_ORIENTATION_HORIZONTAL &&
    gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

  hdy_swipe_tracker_set_reversed (self->tracker, reversed);

  gtk_widget_queue_allocate (GTK_WIDGET (self));
}


static void
phosh_swipe_away_bin_finalize (GObject *object)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (object);

  g_object_unref (self->tracker);
  g_clear_pointer (&self->animation, phosh_animation_unref);

  G_OBJECT_CLASS (phosh_swipe_away_bin_parent_class)->finalize (object);
}


static void
phosh_swipe_away_bin_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (object);

  switch (prop_id) {
  case PROP_ENABLED:
    g_value_set_boolean (value, phosh_swipe_away_bin_get_enabled (self));
    break;

  case PROP_ALLOW_NEGATIVE:
    g_value_set_boolean (value, phosh_swipe_away_bin_get_allow_negative (self));
    break;

  case PROP_RESERVE_SIZE:
    g_value_set_boolean (value, phosh_swipe_away_bin_get_reserve_size (self));
    break;

  case PROP_ORIENTATION:
    g_value_set_enum (value, self->orientation);
    break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}


static void
phosh_swipe_away_bin_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (object);

  switch (prop_id) {
  case PROP_ENABLED:
    phosh_swipe_away_bin_set_enabled (self, g_value_get_boolean (value));
    break;

  case PROP_ALLOW_NEGATIVE:
    phosh_swipe_away_bin_set_allow_negative (self, g_value_get_boolean (value));
    break;

  case PROP_RESERVE_SIZE:
    phosh_swipe_away_bin_set_reserve_size (self, g_value_get_boolean (value));
    break;

  case PROP_ORIENTATION:
  {
    GtkOrientation orientation = g_value_get_enum (value);
    if (orientation != self->orientation) {
      self->orientation = orientation;
      update_orientation (self);
      g_object_notify (G_OBJECT (self), "orientation");
    }
  }
  break;

  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}


static void
phosh_swipe_away_bin_size_allocate (GtkWidget     *widget,
                                    GtkAllocation *alloc)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (widget);
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));
  GtkAllocation child_alloc;

  GTK_WIDGET_CLASS (phosh_swipe_away_bin_parent_class)->size_allocate (widget, alloc);

  if (!child || !gtk_widget_get_visible (child))
    return;

  child_alloc.y = alloc->y;
  child_alloc.x = alloc->x;
  child_alloc.width = alloc->width;
  child_alloc.height = alloc->height;

  if (self->orientation == GTK_ORIENTATION_HORIZONTAL) {
    if (self->reserve_size) {
      self->distance = alloc->width / 3;

      child_alloc.width = self->distance;
      child_alloc.x += self->distance;
    } else {
      self->distance = alloc->width;
    }

    if (gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
      child_alloc.x += (int) (self->progress * self->distance);
    else
      child_alloc.x -= (int) (self->progress * self->distance);

  } else {
    if (self->reserve_size) {
      self->distance = alloc->height / 3;

      child_alloc.height = self->distance;
      child_alloc.y += self->distance;
    } else {
      self->distance = alloc->height;
    }

    child_alloc.y -= (int) (self->progress * self->distance);
  }


  gtk_widget_size_allocate (child, &child_alloc);
}


static void
phosh_swipe_away_bin_get_preferred_width (GtkWidget *widget,
                                          gint      *minimum,
                                          gint      *natural)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (widget);

  GTK_WIDGET_CLASS (phosh_swipe_away_bin_parent_class)->get_preferred_width (widget, minimum, natural);

  if (self->reserve_size &&
      self->orientation == GTK_ORIENTATION_HORIZONTAL) {
    if (minimum)
      *minimum *= 3;

    if (natural)
      *natural *= 3;
  }
}


static void
phosh_swipe_away_bin_get_preferred_height (GtkWidget *widget,
                                           gint      *minimum,
                                           gint      *natural)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (widget);

  GTK_WIDGET_CLASS (phosh_swipe_away_bin_parent_class)->get_preferred_height (widget, minimum, natural);

  if (self->reserve_size &&
      self->orientation == GTK_ORIENTATION_VERTICAL) {
    if (minimum)
      *minimum *= 3;

    if (natural)
      *natural *= 3;
  }
}


static void
phosh_swipe_away_bin_direction_changed (GtkWidget        *widget,
                                        GtkTextDirection  previous_direction)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (widget);

  update_orientation (self);
}


static void
phosh_swipe_away_bin_class_init (PhoshSwipeAwayBinClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_swipe_away_bin_finalize;
  object_class->get_property = phosh_swipe_away_bin_get_property;
  object_class->set_property = phosh_swipe_away_bin_set_property;
  widget_class->size_allocate = phosh_swipe_away_bin_size_allocate;
  widget_class->get_preferred_width = phosh_swipe_away_bin_get_preferred_width;
  widget_class->get_preferred_height = phosh_swipe_away_bin_get_preferred_height;
  widget_class->direction_changed = phosh_swipe_away_bin_direction_changed;

  /**
   * PhoshSwipeAwayBin:enabled:
   *
   * Whether the widget reacts to swipes
   */
  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_ALLOW_NEGATIVE] =
    g_param_spec_boolean ("allow-negative",
                          "Allow Negative",
                          "Use [-1:1] progress range instead of [0:1]",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  props[PROP_RESERVE_SIZE] =
    g_param_spec_boolean ("reserve-size",
                          "Reserve Size",
                          "Allocate larger size than the child so that the child is never clipped when animating",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  g_object_class_override_property (object_class,
                                    PROP_ORIENTATION,
                                    "orientation");

  signals[REMOVED] =
    g_signal_new ("removed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
}


static void
phosh_swipe_away_bin_init (PhoshSwipeAwayBin *self)
{
  self->enabled = TRUE;
  self->tracker = hdy_swipe_tracker_new (HDY_SWIPEABLE (self));

  g_object_bind_property (self, "orientation",
                          self->tracker, "orientation",
                          G_BINDING_SYNC_CREATE);
  hdy_swipe_tracker_set_allow_mouse_drag (self->tracker, TRUE);
  update_orientation (self);

  g_signal_connect_object (self->tracker, "begin-swipe",
                           G_CALLBACK (begin_swipe_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->tracker, "update-swipe",
                           G_CALLBACK (update_swipe_cb), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->tracker, "end-swipe",
                           G_CALLBACK (end_swipe_cb), self,
                           G_CONNECT_SWAPPED);
}


static HdySwipeTracker *
phosh_swipe_away_bin_get_swipe_tracker (HdySwipeable *swipeable)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (swipeable);

  return self->tracker;
}


static double
phosh_swipe_away_bin_get_distance (HdySwipeable *swipeable)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (swipeable);

  return self->distance;
}


static double
phosh_swipe_away_bin_get_cancel_progress (HdySwipeable *swipeable)
{
  return 0;
}


static double
phosh_swipe_away_bin_get_progress (HdySwipeable *swipeable)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (swipeable);

  return self->progress;
}


static double *
phosh_swipe_away_bin_get_snap_points (HdySwipeable *swipeable,
                                      int          *n_snap_points)
{
  PhoshSwipeAwayBin *self = PHOSH_SWIPE_AWAY_BIN (swipeable);
  int n = self->allow_negative ? 3 : 2;
  double *points;

  points = g_new0 (double, n);

  if (self->allow_negative) {
    points[0] = -1;
    points[2] = 1;
  } else {
    points[1] = 1;
  }

  if (n_snap_points)
    *n_snap_points = n;

  return points;
}


static void
phosh_swipe_away_bin_switch_child (HdySwipeable *swipeable,
                                   guint         index,
                                   gint64        duration)
{
}


static void
phosh_swipe_away_bin_swipeable_init (HdySwipeableInterface *iface)
{
  iface->get_swipe_tracker = phosh_swipe_away_bin_get_swipe_tracker;
  iface->get_distance = phosh_swipe_away_bin_get_distance;
  iface->get_cancel_progress = phosh_swipe_away_bin_get_cancel_progress;
  iface->get_progress = phosh_swipe_away_bin_get_progress;
  iface->get_snap_points = phosh_swipe_away_bin_get_snap_points;
  iface->switch_child = phosh_swipe_away_bin_switch_child;
}


gboolean
phosh_swipe_away_bin_get_enabled (PhoshSwipeAwayBin *self)
{
  g_return_val_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self), FALSE);

  return self->enabled;
}


void
phosh_swipe_away_bin_set_enabled (PhoshSwipeAwayBin *self,
                                  gboolean           enabled)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  enabled = !!enabled;
  if (enabled == self->enabled)
    return;

  self->enabled = enabled;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);
}


gboolean
phosh_swipe_away_bin_get_allow_negative (PhoshSwipeAwayBin *self)
{
  g_return_val_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self), FALSE);

  return self->allow_negative;
}


void
phosh_swipe_away_bin_set_allow_negative (PhoshSwipeAwayBin *self,
                                         gboolean           allow_negative)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  allow_negative = !!allow_negative;

  if (allow_negative == self->allow_negative)
    return;

  self->allow_negative = allow_negative;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ALLOW_NEGATIVE]);
}


gboolean
phosh_swipe_away_bin_get_reserve_size (PhoshSwipeAwayBin *self)
{
  g_return_val_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self), FALSE);

  return self->reserve_size;
}


void
phosh_swipe_away_bin_set_reserve_size (PhoshSwipeAwayBin *self,
                                       gboolean           reserve_size)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  reserve_size = !!reserve_size;

  if (reserve_size == self->reserve_size)
    return;

  self->reserve_size = reserve_size;

  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_RESERVE_SIZE]);
}


void
phosh_swipe_away_bin_hide (PhoshSwipeAwayBin *self)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  if (self->animation)
    phosh_animation_stop (self->animation);

  set_progress (self, 1);
}


void
phosh_swipe_away_bin_reveal (PhoshSwipeAwayBin *self)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  if (self->animation)
    phosh_animation_stop (self->animation);

  animate (self, 200, 0, PHOSH_ANIMATION_TYPE_EASE_OUT_CUBIC);
}


void
phosh_swipe_away_bin_remove (PhoshSwipeAwayBin *self)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  if (self->animation)
    phosh_animation_stop (self->animation);

  animate (self, 200, 1, PHOSH_ANIMATION_TYPE_EASE_OUT_CUBIC);
}


void
phosh_swipe_away_bin_undo (PhoshSwipeAwayBin *self)
{
  g_return_if_fail (PHOSH_IS_SWIPE_AWAY_BIN (self));

  if (self->animation)
    phosh_animation_stop (self->animation);

  animate (self, 600, 0, PHOSH_ANIMATION_TYPE_EASE_OUT_BOUNCE);
}
