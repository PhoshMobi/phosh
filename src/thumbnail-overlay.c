/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phosh-thumbnail-overlay"

#include "phosh-config.h"
#include "thumbnail-overlay.h"
#include "top-panel.h"
#include "util.h"

/**
 * PhoshThumbnailOverlay:
 *
 * The thumbnail overlay is shown when we transition from a fullscreen view to
 * the [class@Overview].
 */

struct _PhoshThumbnailOverlay {
  PhoshLayerSurface parent;

  cairo_surface_t  *image;
  double progress;

  struct {
    PhoshThumbnail *thumbnail;
    double          scale;
    double          y;
  } thumbnail;
};

G_DEFINE_TYPE (PhoshThumbnailOverlay, phosh_thumbnail_overlay, PHOSH_TYPE_LAYER_SURFACE)


static gboolean
on_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  PhoshThumbnailOverlay *self = PHOSH_THUMBNAIL_OVERLAY (widget);
  double width, img_w;
  double scale, target_scale, progress, x, y;

  if (!self->image)
    return FALSE;

  width = gtk_widget_get_allocated_width (widget);
  img_w = cairo_image_surface_get_width (self->image);
  progress = CLAMP (self->progress, 0.0, 1.0);

  target_scale = self->thumbnail.scale;
  scale = 1.0 + (target_scale - 1.0) * progress;

  x = (width - img_w * scale) / 2;
  y = PHOSH_TOP_BAR_HEIGHT + self->thumbnail.y * progress;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 0, 0, 0, CLAMP (1.0 - progress, 0.0, 1.0));
  cairo_paint (cr);
  cairo_translate (cr, x, y);
  cairo_scale (cr, scale, scale);
  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_surface (cr, self->image, 0, 0);
  cairo_paint (cr);
  cairo_restore (cr);

  return FALSE;
}


static void
on_screenshot_ready (PhoshThumbnailOverlay *self, GParamSpec *pspec, PhoshThumbnail *thumbnail)
{
  guint w, h, stride;
  g_autoptr (cairo_surface_t) surface = NULL;
  gpointer data;

  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));
  g_return_if_fail (phosh_thumbnail_is_ready (thumbnail));
  g_return_if_fail (self->thumbnail.thumbnail == thumbnail);

  g_debug ("Screenshot %p ready", thumbnail);
  data = phosh_thumbnail_get_image (thumbnail);
  if (!data)
    return;

  phosh_thumbnail_get_size (thumbnail, &w, &h, &stride);
  surface = cairo_image_surface_create_for_data (data,
                                                 CAIRO_FORMAT_ARGB32,
                                                 w, h, stride);
  if (surface == NULL) {
    g_warning ("Failed to create cairo surface from thumbnail data");
    return;
  }

  g_clear_pointer (&self->image, cairo_surface_destroy);
  self->image = cairo_surface_reference (surface);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}


static void
phosh_thumbnail_overlay_dispose (GObject *object)
{
  PhoshThumbnailOverlay *self = PHOSH_THUMBNAIL_OVERLAY (object);

  g_clear_pointer (&self->image, cairo_surface_destroy);
  g_clear_object (&self->thumbnail.thumbnail);

  G_OBJECT_CLASS (phosh_thumbnail_overlay_parent_class)->dispose (object);
}


static void
phosh_thumbnail_overlay_constructed (GObject *object)
{
  PhoshThumbnailOverlay *self = PHOSH_THUMBNAIL_OVERLAY (object);
  PhoshWayland *wl = phosh_wayland_get_default ();

  g_object_set (PHOSH_LAYER_SURFACE (self),
                "layer-shell", phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                "layer", ZWLR_LAYER_SHELL_V1_LAYER_TOP,
                "kbd-interactivity", FALSE,
                "exclusive-zone", -1,
                "namespace", "phosh screenshot overview",
                NULL);

  G_OBJECT_CLASS (phosh_thumbnail_overlay_parent_class)->constructed (object);
}


static void
phosh_thumbnail_overlay_class_init (PhoshThumbnailOverlayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = phosh_thumbnail_overlay_constructed;
  object_class->dispose = phosh_thumbnail_overlay_dispose;

  gtk_widget_class_set_css_name (widget_class, "phosh-thumbnail-overlay");
}


static void
phosh_thumbnail_overlay_init (PhoshThumbnailOverlay *self)
{
  self->progress = 1.0;

  /* Don't draw any background by default */
  gtk_widget_set_app_paintable (GTK_WIDGET (self), TRUE);
  g_signal_connect (self, "draw", G_CALLBACK (on_draw), NULL);
}


PhoshThumbnailOverlay *
phosh_thumbnail_overlay_new (void)
{
  return g_object_new (PHOSH_TYPE_thumbnail_overlay, NULL);
}


void
phosh_thumbnail_overlay_set_thumbnail (PhoshThumbnailOverlay *self, PhoshThumbnail *thumbnail)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));
  g_return_if_fail (PHOSH_IS_THUMBNAIL (thumbnail) || thumbnail == NULL);

  g_set_object (&self->thumbnail.thumbnail, thumbnail);

  if (thumbnail == NULL)
    return;

  /* TODO: need to ref toplevel to ensure it stays alive */
  g_signal_connect_object (self->thumbnail.thumbnail,
                           "notify::ready",
                           G_CALLBACK (on_screenshot_ready),
                           self,
                           G_CONNECT_SWAPPED);
}


void
phosh_thumbnail_overlay_set_progress (PhoshThumbnailOverlay *self, double progress)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->progress = progress;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}


void
phosh_thumbnail_overlay_set_thumbnail_scale (PhoshThumbnailOverlay *self, double scale)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->thumbnail.scale = scale;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}


void
phosh_thumbnail_overlay_set_thumbnail_y (PhoshThumbnailOverlay *self, double y)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->thumbnail.y = y;

  gtk_widget_queue_draw (GTK_WIDGET (self));
}
