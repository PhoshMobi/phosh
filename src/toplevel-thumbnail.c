/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#define G_LOG_DOMAIN "phosh-toplevel-thumbnail"

#include "phosh-wayland.h"
#include "shell-priv.h"
#include "toplevel-thumbnail.h"
#include "wl-buffer.h"

#include <fcntl.h>
#include <sys/mman.h>

/**
 * PhoshToplevelThumbnail:
 *
 * Represents an image snapshot of PhoshToplevel obtained via phosh-private and wlr-screencopy Wayland protocols.
 */

enum {
  PROP_0,
  PROP_HANDLE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshToplevelThumbnail {
  PhoshThumbnail parent;

  struct zwlr_screencopy_frame_v1 *handle;
  PhoshWlBuffer *buffer;
  gboolean       ready;
};

G_DEFINE_TYPE (PhoshToplevelThumbnail, phosh_toplevel_thumbnail, PHOSH_TYPE_THUMBNAIL);


static void
phosh_toplevel_thumbnail_set_ready (PhoshThumbnail *thumbnail, gboolean ready)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (thumbnail);

  g_return_if_fail (PHOSH_IS_TOPLEVEL_THUMBNAIL (self));

  self->ready = ready;

  PHOSH_THUMBNAIL_CLASS (phosh_toplevel_thumbnail_parent_class)->set_ready (thumbnail, ready);
}


static void
screencopy_handle_buffer (void                            *data,
                          struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1,
                          uint32_t                         format,
                          uint32_t                         width,
                          uint32_t                         height,
                          uint32_t                         stride)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (data);

  g_debug ("%s: width %d height %d stride %d", __func__, width, height, stride);

  if (stride * height == 0) {
    g_warning ("Got %s with no size!", __func__);
    return;
  }

  self->buffer = phosh_wl_buffer_new (format, width, height, stride);
  zwlr_screencopy_frame_v1_copy (zwlr_screencopy_frame_v1, self->buffer->wl_buffer);
}


static void
screencopy_handle_flags (void                            *data,
                         struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1,
                         uint32_t                         flags)
{
  /* Nothing to do */
}


static void
screencopy_handle_ready (void                            *data,
                         struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1,
                         uint32_t                         tv_sec_hi,
                         uint32_t                         tv_sec_lo,
                         uint32_t                         tv_nsec)
{
  phosh_toplevel_thumbnail_set_ready (PHOSH_THUMBNAIL (data), TRUE);
}

static void
screencopy_handle_failed (void                            *data,
                          struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1)
{
  g_warning ("screencopy failed! %p", data);
}

static void
screencopy_handle_damage (void                            *data,
                          struct zwlr_screencopy_frame_v1 *zwlr_screencopy_frame_v1,
                          uint32_t                         x,
                          uint32_t                         y,
                          uint32_t                         width,
                          uint32_t                         height)
{
}

static const struct zwlr_screencopy_frame_v1_listener zwlr_screencopy_frame_listener = {
  screencopy_handle_buffer,
  screencopy_handle_flags,
  screencopy_handle_ready,
  screencopy_handle_failed,
  screencopy_handle_damage
};


static void *
phosh_toplevel_thumbnail_get_image (PhoshThumbnail *thumbnail)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (thumbnail);

  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_THUMBNAIL (self), NULL);
  return self->buffer->data;
}

static void
phosh_toplevel_thumbnail_get_size (PhoshThumbnail *thumbnail,
                                   guint          *width,
                                   guint          *height,
                                   guint          *stride)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (thumbnail);
  if (width)
    *width = self->buffer->width;
  if (height)
    *height = self->buffer->height;
  if (stride)
    *stride = self->buffer->stride;
}

static gboolean
phosh_toplevel_thumbnail_is_ready (PhoshThumbnail *thumbnail)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (thumbnail);

  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_THUMBNAIL (self), FALSE);

  return self->ready;
}


static void
phosh_toplevel_thumbnail_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (object);

  switch (property_id) {
  case PROP_HANDLE:
    self->handle = g_value_get_pointer (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_toplevel_thumbnail_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (object);

  switch (property_id) {
  case PROP_HANDLE:
    g_value_set_pointer (value, self->handle);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_toplevel_thumbnail_constructed (GObject *object)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (object);
  zwlr_screencopy_frame_v1_add_listener (self->handle, &zwlr_screencopy_frame_listener, self);

  G_OBJECT_CLASS (phosh_toplevel_thumbnail_parent_class)->constructed (object);
}


static void
phosh_toplevel_thumbnail_dispose (GObject *object)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (object);

  g_clear_pointer (&self->handle, zwlr_screencopy_frame_v1_destroy);

  G_OBJECT_CLASS (phosh_toplevel_thumbnail_parent_class)->dispose (object);
}


static void
phosh_toplevel_thumbnail_finalize (GObject *object)
{
  PhoshToplevelThumbnail *self = PHOSH_TOPLEVEL_THUMBNAIL (object);

  g_clear_pointer (&self->buffer, phosh_wl_buffer_destroy);

  G_OBJECT_CLASS (phosh_toplevel_thumbnail_parent_class)->finalize (object);
}


static void
phosh_toplevel_thumbnail_class_init (PhoshToplevelThumbnailClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshThumbnailClass *thumbnail_class = PHOSH_THUMBNAIL_CLASS (klass);

  object_class->set_property = phosh_toplevel_thumbnail_set_property;
  object_class->get_property = phosh_toplevel_thumbnail_get_property;
  object_class->constructed = phosh_toplevel_thumbnail_constructed;
  object_class->dispose = phosh_toplevel_thumbnail_dispose;
  object_class->finalize = phosh_toplevel_thumbnail_finalize;

  thumbnail_class->is_ready = phosh_toplevel_thumbnail_is_ready;
  thumbnail_class->get_image = phosh_toplevel_thumbnail_get_image;
  thumbnail_class->get_size = phosh_toplevel_thumbnail_get_size;
  thumbnail_class->set_ready = phosh_toplevel_thumbnail_set_ready;

  /**
   * PhoshToplevelThumbnail:handle:
   *
   * The zwlr_screencopy_frame_v1 object associated with this
   * thumbnail
   */
  props[PROP_HANDLE] =
    g_param_spec_pointer ("handle", "", "",
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_toplevel_thumbnail_init (PhoshToplevelThumbnail *self)
{
}


static PhoshToplevelThumbnail *
phosh_toplevel_thumbnail_new_from_handle (struct zwlr_screencopy_frame_v1 *handle)
{
  return g_object_new (PHOSH_TYPE_TOPLEVEL_THUMBNAIL, "handle", handle, NULL);
}

PhoshToplevelThumbnail *
phosh_toplevel_thumbnail_new_from_toplevel (PhoshToplevel *toplevel,
                                            guint32        max_width,
                                            guint32        max_height)
{
  struct zwlr_foreign_toplevel_handle_v1 *handle = phosh_toplevel_get_handle (toplevel);
  struct phosh_private *phosh = phosh_wayland_get_phosh_private (phosh_wayland_get_default ());
  struct zwlr_screencopy_frame_v1 *frame;

  if (!phosh || phosh_private_get_version (phosh) < PHOSH_PRIVATE_GET_THUMBNAIL_SINCE_VERSION)
    return NULL;

  g_debug ("Requesting a %dx%d thumbnail for toplevel %p [%s]", max_width, max_height,
           toplevel, phosh_toplevel_get_title (toplevel));

  frame = phosh_private_get_thumbnail (phosh, handle, max_width, max_height);

  return phosh_toplevel_thumbnail_new_from_handle (frame);
}
