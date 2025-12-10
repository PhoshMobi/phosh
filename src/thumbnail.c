/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#define G_LOG_DOMAIN "phosh-thumbnail"

#include "thumbnail.h"

/**
 * PhoshThumbnail:
 *
 * An abstract class representing a thumbnail image.
 */

enum {
  PROP_0,
  PROP_READY,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

G_DEFINE_TYPE (PhoshThumbnail, phosh_thumbnail, G_TYPE_OBJECT);


static void
phosh_thumbnail_set_ready (PhoshThumbnail *self, gboolean ready)
{
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_READY]);
}

static void
phosh_thumbnail_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhoshThumbnail *self = PHOSH_THUMBNAIL (object);
  PhoshThumbnailClass *klass = PHOSH_THUMBNAIL_GET_CLASS (self);

  switch (property_id) {
  case PROP_READY:
    klass->set_ready (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_thumbnail_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhoshThumbnail *self = PHOSH_THUMBNAIL (object);

  switch (property_id) {
  case PROP_READY:
    g_value_set_boolean (value, phosh_thumbnail_is_ready (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_thumbnail_class_init (PhoshThumbnailClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_thumbnail_get_property;
  object_class->set_property = phosh_thumbnail_set_property;

  klass->set_ready = phosh_thumbnail_set_ready;

  /**
   * PhoshThumbnail:ready:
   *
   * Whether the image data is ready to be used
   */
  props[PROP_READY] =
    g_param_spec_boolean ("ready", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_thumbnail_init (PhoshThumbnail *self)
{
}


gpointer
phosh_thumbnail_get_image (PhoshThumbnail *self)
{
  PhoshThumbnailClass *klass;

  g_return_val_if_fail (PHOSH_IS_THUMBNAIL (self), NULL);

  klass = PHOSH_THUMBNAIL_GET_CLASS (self);
  g_return_val_if_fail (klass->get_image != NULL, NULL);

  return klass->get_image (self);
}


void
phosh_thumbnail_get_size (PhoshThumbnail *self, guint *width, guint *height, guint *stride)
{
  PhoshThumbnailClass *klass;

  g_return_if_fail (PHOSH_IS_THUMBNAIL (self));

  klass = PHOSH_THUMBNAIL_GET_CLASS (self);
  g_return_if_fail (klass->get_size != NULL);

  return klass->get_size (self, width, height, stride);
}


gboolean
phosh_thumbnail_is_ready (PhoshThumbnail *self)
{
  PhoshThumbnailClass *klass;

  g_return_val_if_fail (PHOSH_IS_THUMBNAIL (self), FALSE);

  klass = PHOSH_THUMBNAIL_GET_CLASS (self);
  g_return_val_if_fail (klass->is_ready != NULL, FALSE);

  return klass->is_ready (self);
}
