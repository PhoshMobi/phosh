/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>

#define PHOSH_TYPE_THUMBNAIL (phosh_thumbnail_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhoshThumbnail, phosh_thumbnail, PHOSH, THUMBNAIL, GObject)

/**
 * PhoshThumbnailClass:
 * @parent_class: the parent class
 * @get_image: Get the current image data
 * @get_size: get current image size and stride
 */
struct _PhoshThumbnailClass {
  GObjectClass parent_class;
  gpointer     (*get_image) (PhoshThumbnail *self);
  void         (*get_size)  (PhoshThumbnail *self, guint *width, guint *height, guint *stride);
};

gpointer phosh_thumbnail_get_image (PhoshThumbnail *self);
void     phosh_thumbnail_get_size  (PhoshThumbnail *self, guint *width, guint *height,
                                    guint *stride);
gboolean phosh_thumbnail_is_ready  (PhoshThumbnail *self);
