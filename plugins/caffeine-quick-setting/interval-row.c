/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Rudra Pratap Singh <rudrasingh900@gmail.com>
 */

#include "interval-row.h"

#include <cui-call.h>

#include <glib/gi18n.h>

/**
 * PhoshIntervalRow:
 *
 * A widget to display timed intervals
 */
enum {
  PROP_0,
  PROP_VALUE,
  PROP_SELECTED,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshIntervalRow {
  HdyActionRow  parent;

  GtkRevealer  *revealer;
  uint          value;
  gboolean      selected;
};

G_DEFINE_TYPE (PhoshIntervalRow, phosh_interval_row, HDY_TYPE_ACTION_ROW);

static void
phosh_interval_row_set_value (PhoshIntervalRow *self, uint value)
{
  self->value = value;

  if (value < PHOSH_INTERVAL_ROW_INFINITY_VALUE) {
    g_autofree char *label = cui_call_format_duration ((double) value);

    hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self), label);
  } else {
    hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self), "∞");
  }

  g_object_bind_property (self, "selected",
                          self->revealer, "reveal-child",
                          G_BINDING_SYNC_CREATE);
}


static void
phosh_interval_row_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshIntervalRow *self = PHOSH_INTERVAL_ROW (object);

  switch (property_id) {
  case PROP_VALUE:
    phosh_interval_row_set_value (self, g_value_get_uint (value));
    break;
  case PROP_SELECTED:
    phosh_interval_row_set_selected (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_interval_row_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshIntervalRow *self = PHOSH_INTERVAL_ROW (object);

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
phosh_interval_row_class_init (PhoshIntervalRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_interval_row_get_property;
  object_class->set_property = phosh_interval_row_set_property;

  /**
   * PhoshIntervalRow:value:
   *
   * The value of interval, in seconds
   */
  props[PROP_VALUE] =
    g_param_spec_uint ("value", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshIntervalRow:selected:
   *
   * Whether this row is selected or not
   */
  props[PROP_SELECTED] =
    g_param_spec_boolean ("selected", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "caffeine-quick-setting/interval-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshIntervalRow, revealer);
}


static void
phosh_interval_row_init (PhoshIntervalRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


PhoshIntervalRow *
phosh_interval_row_new (uint value, gboolean selected)
{
  return g_object_new (PHOSH_TYPE_INTERVAL_ROW,
                       "value", value,
                       "selected", selected,
                       NULL);
}


uint
phosh_interval_row_get_value (PhoshIntervalRow *self)
{
  g_return_val_if_fail (PHOSH_IS_INTERVAL_ROW (self), 0);

  return self->value;
}


void
phosh_interval_row_set_selected (PhoshIntervalRow *self, gboolean selected)
{
  g_return_if_fail (PHOSH_IS_INTERVAL_ROW (self));

  if (self->selected == selected)
    return;

  self->selected = selected;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED]);
}
