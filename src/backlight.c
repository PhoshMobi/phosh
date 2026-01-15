/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-backlight"

#define _GNU_SOURCE

#include "phosh-config.h"

#include "backlight-priv.h"
#include "shell-priv.h"

#include <gtk/gtk.h>

#include <math.h>

/**
 * PhoshBacklight:
 *
 * A `PhoshMonitor`'s backlight. Derived classes implement the actual
 * backlight handling.
 *
 * The backlight's brightness levels are mapped to a logarithmic curve
 * unless the backlight implementation indicates via it's `scale` that
 * it does this internally already.
 */

enum {
  PROP_0,
  PROP_BRIGHTNESS,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct _PhoshBacklightPrivate {
  GObject  parent;

  char    *name;
  PhoshBacklightScale scale;

  gboolean pending;

  /* Brightness levels as exposed by the backlight hardware */
  struct {
    int min;
    int max;
    int target;
  } level;

  /* Brightness as exposed to API users */
  struct {
    double min;
    double max;
    double target;
  } brightness;

  GCancellable *cancel;
} PhoshBacklightPrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (PhoshBacklight, phosh_backlight, G_TYPE_OBJECT)


static void
phosh_backlight_set_curve (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  if (priv->scale == PHOSH_BACKLIGHT_SCALE_NON_LINEAR) {
    priv->brightness.max = priv->level.max;
    priv->brightness.min = priv->level.min;
  } else {
    /* TODO: allow for display backlight specific curves */
    priv->brightness.max = log10 (priv->level.max);
    priv->brightness.min = log10 (priv->level.min);
  }
}


static double
phosh_backlight_level_to_brightness (PhoshBacklight *self, int level)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  double brightness;

  if (priv->scale == PHOSH_BACKLIGHT_SCALE_NON_LINEAR)
    return level;

  brightness = log10 (level);
  return CLAMP (brightness, priv->brightness.min, priv->brightness.max);
}


static int
phosh_backlight_brightness_to_level (PhoshBacklight *self, double brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  int level;

  if (priv->scale == PHOSH_BACKLIGHT_SCALE_NON_LINEAR)
    return round (brightness);

  level = round (exp10 (brightness));
  return CLAMP (level, priv->level.min, priv->level.max);
}


static void
phosh_backlight_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);

  switch (property_id) {
  case PROP_BRIGHTNESS:
    phosh_backlight_set_brightness (self, g_value_get_double (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_backlight_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  switch (property_id) {
  case PROP_BRIGHTNESS:
    g_value_set_double (value, priv->brightness.target);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_backlight_dispose (GObject *object)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_cancellable_cancel (priv->cancel);
  g_clear_object (&priv->cancel);

  g_clear_pointer (&priv->name, g_free);

  G_OBJECT_CLASS (phosh_backlight_parent_class)->dispose (object);
}


static void
phosh_backlight_class_init (PhoshBacklightClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_backlight_get_property;
  object_class->set_property = phosh_backlight_set_property;
  object_class->dispose = phosh_backlight_dispose;

  props[PROP_BRIGHTNESS] =
    g_param_spec_double ("brightness", "", "",
                         0, G_MAXDOUBLE, 0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_backlight_init (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  /* Ensure initial sync */
  priv->level.target = -1;

  priv->cancel = g_cancellable_new ();
}

/**
 * phosh_backlight_backend_update_level:
 * @self: The backlight
 * @brightness: the brightness level
 *
 * This is invoked by the concrete backend implementation when the
 * hardware changed brightness.
 */
void
phosh_backlight_backend_update_level (PhoshBacklight *self, int level)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  int new_level;

  if (priv->level.target == level)
    return;

  new_level = CLAMP (level, priv->level.min, priv->level.max);
  if (level != new_level)
    g_warning ("Trying to set out-of-range brightness level %d on %s", level, priv->name);

  priv->level.target = new_level;
  priv->brightness.target = phosh_backlight_level_to_brightness (self, new_level);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BRIGHTNESS]);
}


void
phosh_backlight_get_range (PhoshBacklight *self, int *min_brightness, int *max_brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_BACKLIGHT (self));

  if (min_brightness)
    *min_brightness = priv->brightness.min;

  if (max_brightness)
    *max_brightness = priv->brightness.max;
}


void
phosh_backlight_set_range (PhoshBacklight *self, int min, int max, PhoshBacklightScale scale)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  gboolean force_non_linear;

  priv->level.min = min;
  priv->level.max = max;
  priv->scale = scale;

  force_non_linear = !!(phosh_shell_get_debug_flags () & PHOSH_SHELL_DEBUG_BACKLIGHT_NON_LINEAR);
  if (force_non_linear)
    priv->scale = PHOSH_BACKLIGHT_SCALE_NON_LINEAR;

  if (priv->scale == PHOSH_BACKLIGHT_SCALE_NON_LINEAR)
    g_debug ("Backlight brightness maps to non-linear brightness curve");
  else
    g_debug ("Backlight brightness maps to linear brightness curve");

  phosh_backlight_set_curve (self);
}


int
phosh_backlight_get_brightness (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), -1);

  return priv->brightness.target;
}


static void
on_level_set (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshBacklight *self = PHOSH_BACKLIGHT (source_object);
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  g_autoptr (GError) err = NULL;
  int level;

  priv->pending = FALSE;

  level = PHOSH_BACKLIGHT_GET_CLASS (self)->set_level_finish (self, res, &err);
  if (level < 0) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    g_warning ("Setting backlight on %s failed: %s", priv->name, err->message);
    return;
  }

  /* Brightness got updated from the system and we tried to set it at
   * the same time. Let's try to set the brightness the system was
   * setting to make sure we're in the correct state. */
  if (priv->level.target != level) {
    priv->pending = TRUE;

    PHOSH_BACKLIGHT_GET_CLASS (self)->set_level (self,
                                                 priv->level.target,
                                                 priv->cancel,
                                                 on_level_set,
                                                 NULL);
  }
}


void
phosh_backlight_set_brightness (PhoshBacklight *self, double brightness)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  double new_brightness;
  int new_level;

  g_return_if_fail (PHOSH_IS_BACKLIGHT (self));

  new_brightness = CLAMP (brightness, priv->brightness.min, priv->brightness.max);
  if (!G_APPROX_VALUE (brightness, new_brightness, FLT_EPSILON))
    g_warning ("Trying to set out-of-range brightness %.2f on %s", brightness, priv->name);

  new_level = phosh_backlight_brightness_to_level (self, brightness);
  if (priv->level.target == new_level)
    return;

  g_debug ("Setting target brightness to %d", new_level);
  priv->brightness.target = new_brightness;
  priv->level.target = new_level;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BRIGHTNESS]);

  if (!priv->pending) {
    priv->pending = TRUE;

    PHOSH_BACKLIGHT_GET_CLASS (self)->set_level (self,
                                                 priv->level.target,
                                                 priv->cancel,
                                                 on_level_set,
                                                 NULL);
  }
}

/**
 * phosh_backlight_set_relative:
 * @self: The backlight
 * @val: The relative brightness
 *
 * The relative brightness value between `[0.0, 1.0]` is applied
 * linearly between the backlight's min and max brightness.
 */
void
phosh_backlight_set_relative (PhoshBacklight *self, double val)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);
  double brightness;

  g_return_if_fail (PHOSH_IS_BACKLIGHT (self));
  g_return_if_fail (val >= 0.0 && val <= 1.0);

  brightness = priv->brightness.min + ((priv->brightness.max - priv->brightness.min) * val);
  phosh_backlight_set_brightness (self, brightness);
}

/**
 * phosh_backlight_get_relative:
 * @self: The backlight
 * @val: The relative brightness
 *
 * Get the relative brightness value between `[0.0, 1.0]`.
 */
double
phosh_backlight_get_relative (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  return 1.0 * (priv->brightness.target - priv->brightness.min) /
    (priv->brightness.max - priv->brightness.min);
}


const char *
phosh_backlight_get_name (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), NULL);

  return priv->name;
}


void
phosh_backlight_set_name (PhoshBacklight *self, const char *name)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_set_str (&priv->name, name);
}


PhoshBacklightScale
phosh_backlight_get_scale (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), PHOSH_BACKLIGHT_SCALE_UNKNOWN);

  return priv->scale;
}


int
phosh_backlight_get_levels (PhoshBacklight *self)
{
  PhoshBacklightPrivate *priv = phosh_backlight_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_BACKLIGHT (self), 1);

  return 1 + priv->level.max - priv->level.min;
}
