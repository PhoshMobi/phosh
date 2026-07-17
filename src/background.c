/*
 * Copyright (C) 2018-2022 Purism SPC
 *               2023-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Derived in parts from GnomeBG which is
 *
 * Copyright (C) 2000 Eazel, Inc.
 * Copyright (C) 2007-2008 Red Hat, Inc.
 */

#define G_LOG_DOMAIN "phosh-background"

#include "background.h"
#include "background-cache.h"
#include "background-image.h"
#include "background-manager.h"
#include "layersurface-priv.h"
#include "shell-priv.h"
#include "top-panel.h"
#include "util.h"

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>
#include <libgnome-desktop/gnome-bg-slide-show.h>

#include <gio/gio.h>

#include <math.h>
#include <string.h>

#define COLOR_TO_PIXEL(color)     ((((int)(color->red   * 255)) << 24) | \
                                   (((int)(color->green * 255)) << 16) | \
                                   (((int)(color->blue  * 255)) << 8)  | \
                                   (((int)(color->alpha * 255))))
/**
 * PhoshBackground:
 *
 * A [type@LayerSurface] representing the background drawn on a
 * [type@Monitor].
 *
 * The background is updated by [type@BackgroundManager] when needed.
 */

enum {
  PROP_0,
  PROP_PRIMARY,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


struct _PhoshBackground
{
  PhoshLayerSurface        parent;

  /* The cached background image */
  GFile                   *uri;
  PhoshBackgroundImage    *cached_bg_image;
  GCancellable            *cancel_load;
  /* How the background in rendered */
  GDesktopBackgroundStyle  style;
  GdkRGBA                  color;
  GdkPixbuf               *pixbuf;
  gboolean                 needs_update;

  /* The monitor backed by PhoshBackground */
  gboolean                 primary;
  gboolean                 configured;
};


G_DEFINE_TYPE (PhoshBackground, phosh_background, PHOSH_TYPE_LAYER_SURFACE);


void
phosh_background_data_free (PhoshBackgroundData *bd_data)
{
  g_clear_object (&bd_data->uri);
  g_free (bd_data);
}


static void
phosh_background_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  PhoshBackground *self = PHOSH_BACKGROUND (object);

  switch (property_id) {
  case PROP_PRIMARY:
    phosh_background_set_primary (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_background_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PhoshBackground *self = PHOSH_BACKGROUND (object);

  switch (property_id) {
  case PROP_PRIMARY:
    g_value_set_boolean (value, self->primary);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static GdkPixbuf *
pb_scale_to_fit (GdkPixbuf *src, int width, int height, GdkRGBA *color)
{
  int orig_width, orig_height;
  int final_width, final_height;
  int off_x, off_y;
  double ratio_horiz, ratio_vert, ratio;
  GdkPixbuf *bg;

  bg = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
  gdk_pixbuf_fill (bg, COLOR_TO_PIXEL(color));

  orig_width = gdk_pixbuf_get_width (src);
  orig_height = gdk_pixbuf_get_height (src);
  ratio_horiz = (double) width / orig_width;
  ratio_vert = (double) height / orig_height;

  ratio = ratio_horiz > ratio_vert ? ratio_vert : ratio_horiz;
  final_width = ceil (ratio * orig_width);
  final_height = ceil (ratio * orig_height);

  off_x = (width - final_width) / 2;
  off_y = (height - final_height) / 2;
  gdk_pixbuf_composite (src,
                        bg,
                        off_x, off_y, /* dest x,y */
                        final_width,
                        final_height,
                        off_x, off_y, /* offset x, y */
                        ratio,
                        ratio,
                        GDK_INTERP_BILINEAR,
                        255);
  return bg;
}


static GdkPixbuf *
image_background (PhoshBackgroundImage    *image,
                  guint                    width,
                  guint                    height,
                  GDesktopBackgroundStyle  style,
                  GdkRGBA                 *color)
{
  GdkPixbuf *scaled_bg = NULL;;

  if (image == NULL) {
    g_debug ("No image, using 'none' desktop style");
    style = G_DESKTOP_BACKGROUND_STYLE_NONE;
  }

  switch (style) {
  case G_DESKTOP_BACKGROUND_STYLE_NONE:
    /* Nothing to do */
    break;
  case G_DESKTOP_BACKGROUND_STYLE_SCALED:
    scaled_bg = pb_scale_to_fit (phosh_background_image_get_pixbuf (image), width, height, color);
    break;
  case G_DESKTOP_BACKGROUND_STYLE_WALLPAPER:
  case G_DESKTOP_BACKGROUND_STYLE_CENTERED:
  case G_DESKTOP_BACKGROUND_STYLE_STRETCHED:
  case G_DESKTOP_BACKGROUND_STYLE_SPANNED:
    g_warning ("Unimplemented style %d, using zoom", style);
    G_GNUC_FALLTHROUGH;
  case G_DESKTOP_BACKGROUND_STYLE_ZOOM:
  default:
    scaled_bg = phosh_utils_pixbuf_scale_to_min (phosh_background_image_get_pixbuf (image),
                                                 width,
                                                 height);
    break;
  }

  return scaled_bg;
}


static gboolean
phosh_background_draw (GtkWidget *widget, cairo_t *cr)
{
  PhoshBackground *self = PHOSH_BACKGROUND (widget);
  int x = 0, y = 0, width, height;

  g_return_val_if_fail (PHOSH_IS_BACKGROUND (self), GDK_EVENT_PROPAGATE);

  if (!self->configured)
    return GDK_EVENT_PROPAGATE;

  if (self->primary)
    phosh_shell_get_usable_area (phosh_shell_get_default (), &x, &y, NULL, NULL);

  cairo_save (cr);
  if (self->primary) {
    /* Primary background: use CSS color as it's the top- and home-bar's background */
    GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET (self));

    width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
    height = gtk_widget_get_allocated_height (GTK_WIDGET (self));
    gtk_render_background (context, cr, 0, 0, width, height);
  } else {
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb (cr, self->color.red, self->color.green, self->color.blue);
    cairo_paint (cr);
  }

  if (self->pixbuf) {
    double scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));

    cairo_scale (cr, 1.0 / scale, 1.0 / scale);
    gdk_cairo_set_source_pixbuf (cr, self->pixbuf, x / scale, y / scale);

    cairo_paint (cr);
  }

  cairo_restore (cr);

  return GDK_EVENT_PROPAGATE;
}


static void
update_image (PhoshBackground *self)
{
  int width, height, scale;

  if (!self->configured)
    return;

  if (self->primary) {
    phosh_shell_get_usable_area (phosh_shell_get_default (), NULL, NULL, &width, &height);
  } else {
    width = phosh_layer_surface_get_configured_width (PHOSH_LAYER_SURFACE (self));
    height = phosh_layer_surface_get_configured_height (PHOSH_LAYER_SURFACE (self));
  }

  g_return_if_fail (width > 0 && height > 0);

  scale = gtk_widget_get_scale_factor (GTK_WIDGET (self));
  scale = MAX (1, scale);

  g_debug ("Scaling background %p to %dx%d, scale %d", self, width, height, scale);

  g_clear_object (&self->pixbuf);
  self->pixbuf = image_background (self->cached_bg_image, width * scale, height * scale,
                                   self->style, &self->color);

  self->needs_update = FALSE;
  gtk_widget_queue_draw (GTK_WIDGET (self));
}


static void
on_background_cache_fetch_ready (GObject *source_object, GAsyncResult *res, gpointer data)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshBackgroundImage) image = NULL;
  PhoshBackground *self = PHOSH_BACKGROUND (data);
  PhoshBackgroundCache *cache = PHOSH_BACKGROUND_CACHE (source_object);

  image = phosh_background_cache_fetch_finish (cache, res, &err);
  if (!image) {
    phosh_async_error_warn (err, "Failed to load background image");
    return;
  }

  g_assert (PHOSH_IS_BACKGROUND (self));
  g_assert (PHOSH_IS_BACKGROUND_CACHE (cache));

  g_set_object (&self->cached_bg_image, image);
  update_image (self);
}


static void
trigger_update (PhoshBackground *self)
{
  PhoshBackgroundCache *cache = phosh_background_cache_get_default ();
  PhoshBackgroundManager *manager = phosh_shell_get_background_manager (phosh_shell_get_default ());
  g_autoptr (PhoshBackgroundData) bg_data = NULL;

  g_debug ("Updating Background %p", self);
  bg_data = phosh_background_manager_get_data (manager, self);

  self->needs_update = TRUE;
  self->style = bg_data->style;
  self->color = bg_data->color;
  g_set_object (&self->uri, bg_data->uri);

  g_cancellable_cancel (self->cancel_load);
  g_clear_object (&self->cancel_load);
  self->cancel_load = g_cancellable_new ();

  if (self->uri) {
    phosh_background_cache_fetch_async (cache,
                                        self->uri,
                                        self->cancel_load,
                                        on_background_cache_fetch_ready,
                                        self);
  } else {
    g_clear_object (&self->cached_bg_image);
    update_image (self);
  }
}


static void
phosh_background_configured (PhoshLayerSurface *layer_surface)
{
  PhoshBackground *self = PHOSH_BACKGROUND (layer_surface);

  PHOSH_LAYER_SURFACE_CLASS (phosh_background_parent_class)->configured (layer_surface);

  self->configured = TRUE;
  trigger_update (self);
}


static void
phosh_background_finalize (GObject *object)
{
  PhoshBackground *self = PHOSH_BACKGROUND (object);

  g_cancellable_cancel (self->cancel_load);
  g_clear_object (&self->cancel_load);
  g_clear_object (&self->pixbuf);
  g_clear_object (&self->cached_bg_image);

  G_OBJECT_CLASS (phosh_background_parent_class)->finalize (object);
}


static void
phosh_background_class_init (PhoshBackgroundClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  PhoshLayerSurfaceClass *layer_surface_class = PHOSH_LAYER_SURFACE_CLASS (klass);

  object_class->finalize = phosh_background_finalize;
  object_class->set_property = phosh_background_set_property;
  object_class->get_property = phosh_background_get_property;

  widget_class->draw = phosh_background_draw;

  layer_surface_class->configured = phosh_background_configured;

  /**
   * PhoshBackground:primary:
   *
   * Whether this is the background for the primary monitor.
   */
  props[PROP_PRIMARY] =
    g_param_spec_boolean ("primary",
                          "Primary",
                          "Primary monitor",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_CONSTRUCT);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_css_name (widget_class, "phosh-background");
}


static void
phosh_background_init (PhoshBackground *self)
{
}


GtkWidget *
phosh_background_new (gpointer      layer_shell,
                      PhoshMonitor *monitor,
                      gboolean      primary,
                      guint         layer)
{
  return g_object_new (PHOSH_TYPE_BACKGROUND,
                       "layer-shell", layer_shell,
                       "wl-output", monitor->wl_output,
                       "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                       "layer", layer,
                       "kbd-interactivity", FALSE,
                       "exclusive-zone", -1,
                       "namespace", "phosh background",
                       "primary", primary,
                       NULL);
}


void
phosh_background_set_primary (PhoshBackground *self, gboolean primary)
{
  if (self->primary == primary)
    return;

  self->primary = primary;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRIMARY]);

  trigger_update (self);
}

/**
 * phosh_background_needs_update:
 * @self: The background
 *
 * Marks the background's data as dirty, needing an update. This will make the
 * `PhoshBackground` update it's background image.
 */
void
phosh_background_needs_update (PhoshBackground *self)
{
  g_return_if_fail (PHOSH_IS_BACKGROUND (self));

  /* Skip update if layer surface isn't yet configured, it will
   * trigger on layer_surface.configured anyway */
  if (phosh_layer_surface_get_configured_width (PHOSH_LAYER_SURFACE (self)) <= 0 ||
      phosh_layer_surface_get_configured_height (PHOSH_LAYER_SURFACE (self)) <= 0) {
    return;
  }

  trigger_update (self);
}
