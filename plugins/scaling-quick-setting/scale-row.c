/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "scale-row.h"

#include <glib/gi18n.h>

#include <math.h>

/**
 * PhoshScaleRow:
 *
 * A widget to display a single monitor scale
 */

enum {
  PROP_0,
  PROP_SCALE,
  PROP_SELECTED,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshScaleRow {
  HdyActionRow      parent;

  GtkRevealer      *revealer;
  double            scale;
  gboolean          selected;
};

G_DEFINE_TYPE (PhoshScaleRow, phosh_scale_row, HDY_TYPE_ACTION_ROW);


static void
phosh_scale_row_set_device (PhoshScaleRow *self, double scale)
{
  g_autofree char *label = NULL;

  self->scale = scale;

  /* Translators: This is scale factor of a monitor in percent */
  label = g_strdup_printf (_("%d%%"), (int)round (scale * 100.0));

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self), label);

  g_object_bind_property (self, "selected",
                          self->revealer, "reveal-child",
                          G_BINDING_SYNC_CREATE);
}


static void
set_selected (PhoshScaleRow *self, gboolean selected)
{
  if (self->selected == selected)
    return;

  self->selected = selected;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED]);
}


static void
phosh_scale_row_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  PhoshScaleRow *self = PHOSH_SCALE_ROW (object);

  switch (property_id) {
  case PROP_SCALE:
    phosh_scale_row_set_device (self, g_value_get_double (value));
    break;
  case PROP_SELECTED:
    set_selected (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_scale_row_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  PhoshScaleRow *self = PHOSH_SCALE_ROW (object);

  switch (property_id) {
  case PROP_SELECTED:
    g_value_set_boolean (value, self->selected);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_scale_row_class_init (PhoshScaleRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_scale_row_get_property;
  object_class->set_property = phosh_scale_row_set_property;

  /**
   * PhoshScaleRow:scale:
   *
   * A monitor scale
   */
  props[PROP_SCALE] =
    g_param_spec_double ("scale", "", "",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshScaleRow:selected:
   *
   * Whether this is the current selected monitor scale
   */
  props[PROP_SELECTED] =
    g_param_spec_boolean ("selected", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "scaling-quick-setting/scale-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshScaleRow, revealer);
}


static void
phosh_scale_row_init (PhoshScaleRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_scale_row_new (double scale, gboolean selected)
{
  return GTK_WIDGET (g_object_new (PHOSH_TYPE_SCALE_ROW,
                                   "scale", scale,
                                   "selected", selected,
                                   NULL));
}


double
phosh_scale_row_get_scale (PhoshScaleRow *self)
{
  g_return_val_if_fail (PHOSH_IS_SCALE_ROW (self), 1.0);

  return self->scale;
}
