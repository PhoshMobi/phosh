/*
 * Copyright (C) 2018 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * based on gvc-channel-bar.h from g-c-c which is
 * Copyright (C) 2008 Red Hat, Inc.
 */

#define G_LOG_DOMAIN "phosh-channel-bar"

#include "phosh-config.h"

#include "channel-bar.h"

#include <glib/gi18n-lib.h>
#include <math.h>
#include <pulse/pulseaudio.h>

#define ADJUSTMENT_MAX_NORMAL PA_VOLUME_NORM
#define ADJUSTMENT_MAX_AMPLIFIED PA_VOLUME_UI_MAX
#define ADJUSTMENT_MAX (self->is_amplified ? ADJUSTMENT_MAX_AMPLIFIED : ADJUSTMENT_MAX_NORMAL)

enum
{
  PROP_0,
  PROP_IS_MUTED,
  PROP_ICON_NAME,
  PROP_IS_AMPLIFIED,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


enum {
  VALUE_CHANGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


struct _PhoshChannelBar {
  GtkBox         parent_instance;

  GtkWidget     *scale_box;
  GtkWidget     *image;
  GtkWidget     *scale;
  GtkAdjustment *adjustment;
  gboolean       is_muted;
  char          *icon_name;
  GtkSizeGroup  *size_group;
  gboolean       click_lock;
  gboolean       is_amplified;
  guint32        base_volume;
};

G_DEFINE_TYPE (PhoshChannelBar, phosh_channel_bar, GTK_TYPE_BOX)


static void
update_image (PhoshChannelBar *self)
{
  g_autoptr (GIcon) gicon = NULL;

  if (self->icon_name) {
    gicon = g_themed_icon_new_with_default_fallbacks (self->icon_name);
    gtk_image_set_from_gicon (GTK_IMAGE (self->image), gicon, -1);
  }

  gtk_widget_set_visible (self->image, self->icon_name != NULL);
}


void
phosh_channel_bar_set_size_group (PhoshChannelBar *self, GtkSizeGroup *group)
{
  g_return_if_fail (PHOSH_IS_CHANNEL_BAR (self));

  self->size_group = group;

  if (self->size_group != NULL)
    gtk_size_group_add_widget (self->size_group, self->scale_box);

  gtk_widget_queue_draw (GTK_WIDGET (self));
}


void
phosh_channel_bar_set_icon_name (PhoshChannelBar *self, const char *name)
{
  g_return_if_fail (PHOSH_IS_CHANNEL_BAR (self));

  if (g_strcmp0 (self->icon_name, name) == 0)
    return;

  g_free (self->icon_name);
  self->icon_name = g_strdup (name);
  update_image (self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}


GtkAdjustment *
phosh_channel_bar_get_adjustment (PhoshChannelBar *self)
{
  g_return_val_if_fail (PHOSH_IS_CHANNEL_BAR (self), NULL);

  return self->adjustment;
}


static gboolean
on_scale_button_press_event (GtkWidget       *widget,
                             GdkEventButton  *event,
                             PhoshChannelBar *self)
{
  self->click_lock = TRUE;

  return FALSE;
}


static gboolean
on_scale_button_release_event (GtkWidget       *widget,
                               GdkEventButton  *event,
                               PhoshChannelBar *self)
{
  double value;

  self->click_lock = FALSE;
  value = gtk_adjustment_get_value (self->adjustment);

  /* this means the adjustment moved away from zero and
   * therefore we should unmute and set the volume. */
  phosh_channel_bar_set_is_muted (self, ((int)value == (int)0.0));

  return FALSE;
}


static void
on_adjustment_value_changed (GtkAdjustment *adjustment, PhoshChannelBar *self)
{
  if (!self->is_muted || self->click_lock)
    g_signal_emit (self, signals[VALUE_CHANGED], 0);
}


void
phosh_channel_bar_set_is_muted (PhoshChannelBar *self, gboolean is_muted)
{
  g_return_if_fail (PHOSH_IS_CHANNEL_BAR (self));

  if (is_muted == self->is_muted)
    return;

  /* Update our internal state before telling the
   * front-end about our changes */
  self->is_muted = is_muted;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_MUTED]);

  if (is_muted)
    gtk_adjustment_set_value (self->adjustment, 0.0);
}


gboolean
phosh_channel_bar_get_is_muted  (PhoshChannelBar *self)
{
  g_return_val_if_fail (PHOSH_IS_CHANNEL_BAR (self), FALSE);
  return self->is_muted;
}


void
phosh_channel_bar_set_is_amplified (PhoshChannelBar *self, gboolean amplified)
{
  g_return_if_fail (PHOSH_IS_CHANNEL_BAR (self));

  if (self->is_amplified == amplified)
    return;

  self->is_amplified = amplified;
  gtk_adjustment_set_upper (self->adjustment, ADJUSTMENT_MAX);
  gtk_scale_clear_marks (GTK_SCALE (self->scale));

  if (amplified) {
    g_autofree char *str = NULL;

    if (G_APPROX_VALUE (self->base_volume, floor (ADJUSTMENT_MAX_NORMAL), DBL_EPSILON)) {
      str = g_strdup_printf ("<small>%s</small>", C_("volume", "100%"));
      gtk_scale_add_mark (GTK_SCALE (self->scale), ADJUSTMENT_MAX_NORMAL,
                          GTK_POS_BOTTOM, str);
    } else {
      str = g_strdup_printf ("<small>%s</small>", C_("volume", "Unamplified"));
      gtk_scale_add_mark (GTK_SCALE (self->scale), self->base_volume,
                          GTK_POS_BOTTOM, str);
      /* Only show 100% if it's higher than the base volume */
      if (self->base_volume < ADJUSTMENT_MAX_NORMAL) {
        str = g_strdup_printf ("<small>%s</small>", C_("volume", "100%"));
        gtk_scale_add_mark (GTK_SCALE (self->scale), ADJUSTMENT_MAX_NORMAL,
                            GTK_POS_BOTTOM, str);
      }
    }

    /* Ideally we would use baseline alignment for all
     * these widgets plus the scale but neither GtkScale
     * nor GtkSwitch support baseline alignment yet. */
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_AMPLIFIED]);
}


void
phosh_channel_bar_set_base_volume (PhoshChannelBar *self, pa_volume_t base_volume)
{
  g_return_if_fail (PHOSH_IS_CHANNEL_BAR (self));

  if (base_volume == 0) {
    self->base_volume = ADJUSTMENT_MAX_NORMAL;
    return;
  }

  /* Note that you need to call _is_amplified() afterwards to update the marks */
  self->base_volume = base_volume;
}


static void
phosh_channel_bar_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PhoshChannelBar *self = PHOSH_CHANNEL_BAR (object);

  switch (prop_id) {
  case PROP_IS_MUTED:
    phosh_channel_bar_set_is_muted (self, g_value_get_boolean (value));
    break;
  case PROP_ICON_NAME:
    phosh_channel_bar_set_icon_name (self, g_value_get_string (value));
    break;
  case PROP_IS_AMPLIFIED:
    phosh_channel_bar_set_is_amplified (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}


static void
phosh_channel_bar_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PhoshChannelBar *self = PHOSH_CHANNEL_BAR (object);

  switch (prop_id) {
  case PROP_IS_MUTED:
    g_value_set_boolean (value, self->is_muted);
    break;
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  case PROP_IS_AMPLIFIED:
    g_value_set_boolean (value, self->is_amplified);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}


static void
phosh_channel_bar_finalize (GObject *object)
{
  PhoshChannelBar *self = PHOSH_CHANNEL_BAR (object);

  g_free (self->icon_name);

  G_OBJECT_CLASS (phosh_channel_bar_parent_class)->finalize (object);
}


static void
phosh_channel_bar_class_init (PhoshChannelBarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_channel_bar_finalize;
  object_class->set_property = phosh_channel_bar_set_property;
  object_class->get_property = phosh_channel_bar_get_property;

  /**
   * PhoshChannelBar:is-muted:
   *
   * Whether the stream is muted
   */
  props[PROP_IS_MUTED] =
    g_param_spec_boolean ("is-muted", "", "",
                          FALSE,
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  /**
   * PhoshChannelBar:icon-name:
   *
   * The name of icon to display for this stream
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_EXPLICIT_NOTIFY |
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  /**
   * PhoshChannelBar:is-amplified:
   *
   * Whether the stream is digitally amplified
   */
  props[PROP_IS_AMPLIFIED] =
    g_param_spec_boolean ("is-amplified", "", "",
                          FALSE,
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  signals[VALUE_CHANGED] = g_signal_new ("value-changed",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0, NULL, NULL, NULL,
                                         G_TYPE_NONE,
                                         0);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/channel-bar.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshChannelBar, adjustment);
  gtk_widget_class_bind_template_child (widget_class, PhoshChannelBar, scale_box);
  gtk_widget_class_bind_template_child (widget_class, PhoshChannelBar, image);
  gtk_widget_class_bind_template_child (widget_class, PhoshChannelBar, scale);
  gtk_widget_class_bind_template_callback (widget_class, on_adjustment_value_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_scale_button_press_event);
  gtk_widget_class_bind_template_callback (widget_class, on_scale_button_release_event);

  gtk_widget_class_set_css_name (widget_class, "phosh-channel-bar");
}


static void
phosh_channel_bar_init (PhoshChannelBar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->base_volume = ADJUSTMENT_MAX_NORMAL;
  gtk_adjustment_set_upper (self->adjustment, ADJUSTMENT_MAX_NORMAL);
  gtk_adjustment_set_step_increment (self->adjustment, ADJUSTMENT_MAX_NORMAL / 100.0);
  gtk_adjustment_set_page_increment (self->adjustment, ADJUSTMENT_MAX_NORMAL / 10.0);

  gtk_widget_add_events (self->scale, GDK_SCROLL_MASK);
}


GtkWidget *
phosh_channel_bar_new (void)
{
  return g_object_new (PHOSH_TYPE_CHANNEL_BAR,
                       "icon-name", "audio-speakers-symbolic",
                       NULL);
}


double
phosh_channel_bar_get_volume (PhoshChannelBar *self)
{
  g_return_val_if_fail (PHOSH_IS_CHANNEL_BAR (self), 0.0);

  return gtk_adjustment_get_value (self->adjustment);
}
