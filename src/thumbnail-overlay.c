/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phosh-thumbnail-overlay"

#include "phosh-config.h"
#include "thumbnail-overlay.h"
#include "layersurface-priv.h"
#include "phosh-wayland.h"
#include "shell.h"
#include "top-panel.h"
#include "toplevel.h"
#include "util.h"

#include <math.h>

/**
 * PhoshThumbnailOverlay:
 *
 * The thumbnail overlay is shown when we transition from a fullscreen view to
 * the [class@Overview].
 *
 * The overlay itself is only used as a depth/position anchor: it's a
 * transparent layer surface that the compositor renders the thumbnail onto
 * (via the `phosh_private_thumbnail` protocol object) instead of the client
 * content.
 */

struct _PhoshThumbnailOverlay {
  PhoshLayerSurface parent;

  struct phosh_private_thumbnail *thumb;

  /* Size of the fitted thumbnail image in output pixels */
  double width;

  double progress;

  struct {
    double scale;
    double y;
  } thumbnail;
};

G_DEFINE_TYPE (PhoshThumbnailOverlay, phosh_thumbnail_overlay, PHOSH_TYPE_LAYER_SURFACE)


static void
update_transform (PhoshThumbnailOverlay *self)
{
  double progress, scale, x, y, alpha;

  if (!self->thumb)
    return;

  progress = CLAMP (self->progress, 0.0, 1.0);
  scale = 1.0 + (self->thumbnail.scale - 1.0) * progress;

  x = (self->width - self->width * scale) / 2;
  y = PHOSH_TOP_BAR_HEIGHT + self->thumbnail.y * progress;
  alpha = CLAMP (1.0 - progress, 0.0, 1.0);

  phosh_private_thumbnail_set_properties (self->thumb,
                                          (int) round (x),
                                          (int) round (y),
                                          wl_fixed_from_double (scale),
                                          wl_fixed_from_double (alpha));
}


static void
attach_toplevel (PhoshThumbnailOverlay *self, PhoshToplevel *toplevel)
{
  PhoshWayland *wl = phosh_wayland_get_default ();
  struct phosh_private *phosh;
  struct zwlr_layer_surface_v1 *layer_surface;

  g_clear_pointer (&self->thumb, phosh_private_thumbnail_destroy);

  if (toplevel == NULL)
    return;

  phosh = phosh_wayland_get_phosh_private (wl);
  layer_surface = phosh_layer_surface_get_layer_surface (PHOSH_LAYER_SURFACE (self));

  self->thumb = phosh_private_add_thumbnail_view (phosh,
                                                  phosh_toplevel_get_handle (toplevel),
                                                  layer_surface);
  update_transform (self);
}


static void
phosh_thumbnail_overlay_dispose (GObject *object)
{
  PhoshThumbnailOverlay *self = PHOSH_THUMBNAIL_OVERLAY (object);

  g_clear_pointer (&self->thumb, phosh_private_thumbnail_destroy);

  G_OBJECT_CLASS (phosh_thumbnail_overlay_parent_class)->dispose (object);
}


static void
phosh_thumbnail_overlay_constructed (GObject *object)
{
  PhoshThumbnailOverlay *self = PHOSH_THUMBNAIL_OVERLAY (object);
  PhoshWayland *wl = phosh_wayland_get_default ();
  PhoshShell *shell = phosh_shell_get_default ();
  int width;

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

  /* The thumbnail is rendered by the compositor at the view's native size.
   * Since we only support maximized views here that equals the usable area,
   * which is also what the thumbnail gets fitted to. */
  phosh_shell_get_usable_area (shell, NULL, NULL, &width, NULL);
  self->width = width;

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

  /* Don't draw any background by default: the compositor renders the content */
  gtk_widget_set_app_paintable (GTK_WIDGET (self), TRUE);
}


PhoshThumbnailOverlay *
phosh_thumbnail_overlay_new (void)
{
  return g_object_new (PHOSH_TYPE_thumbnail_overlay, NULL);
}


void
phosh_thumbnail_overlay_set_toplevel (PhoshThumbnailOverlay *self, PhoshToplevel *toplevel)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel) || toplevel == NULL);

  attach_toplevel (self, toplevel);
}


void
phosh_thumbnail_overlay_set_progress (PhoshThumbnailOverlay *self, double progress)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->progress = progress;
  update_transform (self);
}


void
phosh_thumbnail_overlay_set_thumbnail_scale (PhoshThumbnailOverlay *self, double scale)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->thumbnail.scale = scale;
  update_transform (self);
}


void
phosh_thumbnail_overlay_set_thumbnail_y (PhoshThumbnailOverlay *self, double y)
{
  g_return_if_fail (PHOSH_IS_THUMBNAIL_OVERLAY (self));

  self->thumbnail.y = y;
  update_transform (self);
}
