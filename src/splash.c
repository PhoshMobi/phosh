/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-splash"

#include "phosh-config.h"

#include "animation.h"
#include "layersurface-priv.h"
#include "phosh-wayland.h"
#include "monitor.h"
#include "shell-priv.h"
#include "splash.h"

#define PHOSH_APP_UNKNOWN_ICON "app-icon-unknown"

/**
 * PhoshSplash:
 *
 * A splash screen
 *
 * The #PhoshSplash is a splash screen used to indicate application
 * startup.
 */

enum {
  PROP_0,
  PROP_APP_INFO,
  PROP_PREFER_DARK,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


enum {
  CLOSED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


typedef struct {
  struct zwlr_layer_shell_v1 *layer_shell;

  GAppInfo                   *info;
  GtkWidget                  *box;
  GIcon                      *icon;
  GtkWidget                  *img_app;
  gboolean                    prefer_dark;

  PhoshAnimation             *fadeout;
} PhoshSplashPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshSplash, phosh_splash, PHOSH_TYPE_LAYER_SURFACE);


static void
set_style (PhoshSplash *self, gboolean prefer_dark)
{
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);
  GtkStyleContext    *context;

  priv->prefer_dark = prefer_dark;

  context = gtk_widget_get_style_context (GTK_WIDGET (self));
  if (prefer_dark) {
    gtk_style_context_add_class (context, "dark");
    gtk_style_context_remove_class (context, "light");
  } else {
    gtk_style_context_add_class (context, "light");
    gtk_style_context_remove_class (context, "dark");
  }
}


static void
phosh_splash_set_property (GObject      *obj,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  PhoshSplash *self = PHOSH_SPLASH (obj);
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);

  switch (prop_id) {
  case PROP_APP_INFO:
    g_set_object (&priv->info, g_value_get_object (value));
    break;
  case PROP_PREFER_DARK:
    set_style (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}


static void
phosh_splash_get_property (GObject    *obj,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  PhoshSplash *self = PHOSH_SPLASH (obj);
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);

  switch (prop_id) {
  case PROP_APP_INFO:
    g_value_set_object (value, priv->info);
    break;
  case PROP_PREFER_DARK:
    g_value_set_boolean (value, priv->prefer_dark);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}


static void
phosh_splash_dispose (GObject *obj)
{
  PhoshSplash *self = PHOSH_SPLASH (obj);
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);

  g_clear_pointer (&priv->fadeout, phosh_animation_unref);
  g_clear_object (&priv->info);

  G_OBJECT_CLASS (phosh_splash_parent_class)->dispose (obj);
}


static void
phosh_splash_constructed (GObject *object)
{
  PhoshSplash *self = PHOSH_SPLASH (object);
  PhoshWayland *wl = phosh_wayland_get_default ();
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);
  PhoshMonitor *monitor;

  g_debug ("New splash for %s", g_app_info_get_id (priv->info));
  monitor = phosh_shell_get_primary_monitor (phosh_shell_get_default ());

  g_object_set (PHOSH_LAYER_SURFACE (self),
                "layer-shell", phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                "wl-output", phosh_monitor_get_wl_output (monitor),
                "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                "layer", ZWLR_LAYER_SHELL_V1_LAYER_TOP,
                "kbd-interactivity", TRUE,
                "exclusive-zone", -1,
                "namespace", "phosh splash",
                NULL);

  G_OBJECT_CLASS (phosh_splash_parent_class)->constructed (object);
}


static gboolean
phosh_splash_key_press_event (GtkWidget *self, GdkEventKey *event)
{
  gboolean handled = FALSE;

  g_return_val_if_fail (PHOSH_IS_SPLASH (self), FALSE);

  switch (event->keyval) {
  case GDK_KEY_Escape:
    g_signal_emit (self, signals[CLOSED], 0);
    handled = TRUE;
    break;
  default:
    /* nothing to do */
    break;
  }

  return handled;
}


static void
phosh_splash_show (GtkWidget *widget)
{
  PhoshSplash *self = PHOSH_SPLASH (widget);
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);
  GIcon *icon;

  icon = g_app_info_get_icon (priv->info);
  if (G_UNLIKELY (icon == NULL)) {
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->img_app),
                                  PHOSH_APP_UNKNOWN_ICON,
                                  -1);
  } else {
    gtk_image_set_from_gicon (GTK_IMAGE (priv->img_app), icon, -1);
  }

  GTK_WIDGET_CLASS (phosh_splash_parent_class)->show (widget);
}


static void
phosh_splash_class_init (PhoshSplashClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_splash_get_property;
  object_class->set_property = phosh_splash_set_property;
  object_class->constructed = phosh_splash_constructed;
  object_class->dispose = phosh_splash_dispose;
  widget_class->show = phosh_splash_show;
  widget_class->key_press_event = phosh_splash_key_press_event;

  /**
   * PhoshSplash:app-info:
   *
   * The appinfo this splash is for
   */
  props[PROP_APP_INFO] = g_param_spec_object ("app-info", "", "",
                                              G_TYPE_DESKTOP_APP_INFO,
                                              G_PARAM_CONSTRUCT_ONLY |
                                              G_PARAM_READWRITE |
                                              G_PARAM_STATIC_STRINGS);
  /**
   * PhoshSplash:prefer-dark:
   *
   * Whether the splash should prefer dark theming
   */
  props[PROP_PREFER_DARK] = g_param_spec_boolean ("prefer-dark", "", "",
                                                  FALSE,
                                                  G_PARAM_CONSTRUCT_ONLY |
                                                  G_PARAM_READWRITE |
                                                  G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshSplay:closed:
   *
   * The splash should be closed
   */
  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass),
                                  G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                  NULL,
                                  G_TYPE_NONE,
                                  0);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/splash.ui");
  gtk_widget_class_bind_template_child_private (widget_class, PhoshSplash, img_app);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshSplash, box);

  gtk_widget_class_set_css_name (widget_class, "phosh-splash");
}


static void
phosh_splash_init (PhoshSplash *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_splash_new (GDesktopAppInfo *info, gboolean prefer_dark)
{
  return g_object_new (PHOSH_TYPE_SPLASH,
                       "app-info", info,
                       "prefer-dark", prefer_dark,
                       NULL);
}


static void
fadeout_value_cb (double value, gpointer data)
{
  PhoshSplash *self = data;

  phosh_layer_surface_set_alpha (PHOSH_LAYER_SURFACE (self), 1.0 - value);
}


static void
fadeout_done_cb (gpointer data)
{
  PhoshSplash *self = data;
  PhoshSplashPrivate *priv = phosh_splash_get_instance_private (self);

  g_clear_pointer (&priv->fadeout, phosh_animation_unref);

  gtk_widget_destroy (GTK_WIDGET (self));
}


void
phosh_splash_hide (PhoshSplash *self)
{
  PhoshSplashPrivate *priv;

  g_return_if_fail (PHOSH_IS_SPLASH (self));
  priv = phosh_splash_get_instance_private (self);

  priv->fadeout = phosh_animation_new (GTK_WIDGET (self),
                                       0.0,
                                       1.0,
                                       200 * PHOSH_ANIMATION_SLOWDOWN,
                                       PHOSH_ANIMATION_TYPE_EASE_IN_QUINTIC,
                                       fadeout_value_cb,
                                       fadeout_done_cb,
                                       self);
  phosh_animation_start (priv->fadeout);
}


void
phosh_splash_lower (PhoshSplash *self)
{
  g_return_if_fail (PHOSH_IS_SPLASH (self));

  phosh_layer_surface_set_layer (PHOSH_LAYER_SURFACE (self),
                                 ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}


void
phosh_splash_raise (PhoshSplash *self)
{
  g_return_if_fail (PHOSH_IS_SPLASH (self));

  phosh_layer_surface_set_layer (PHOSH_LAYER_SURFACE (self),
                                 ZWLR_LAYER_SHELL_V1_LAYER_TOP);
  gtk_widget_queue_draw (GTK_WIDGET (self));
}
