/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Gotam Gorabh <gautamy672@gmail.com>
 */

#include "accuracy-row.h"

#include <glib/gi18n.h>

/**
 * PhoshAccuracyRow:
 *
 * A row representing a location accuracy level.
 */
enum {
  PROP_0,
  PROP_LEVEL,
  PROP_SELECTED,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshAccuracyRow {
  HdyActionRow parent;

  GtkRevealer *revealer;

  GDesktopLocationAccuracyLevel level;
  gboolean     selected;
};

G_DEFINE_TYPE (PhoshAccuracyRow, phosh_accuracy_row, HDY_TYPE_ACTION_ROW);


static const char *
phosh_accuracy_row_level_to_label (GDesktopLocationAccuracyLevel level)
{
  switch (level) {
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_COUNTRY:
    /* Translators: Location precision level - country-wide */
    return _("Country");
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_CITY:
    /* Translators: Location precision level - city-wide */
    return _("City");
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_NEIGHBORHOOD:
    /* Translators: Location precision level - neighborhood */
    return _("Neighborhood");
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_STREET:
    /* Translators: Location precision level - street */
    return _("Street");
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_EXACT:
    /* Translators: Location precision level - exact position */
    return _("Exact");
  default:
    g_warn_if_reached ();
    return NULL;
  }
}


static const char *
accuracy_level_to_icon_name (GDesktopLocationAccuracyLevel level)
{
  switch (level) {
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_COUNTRY:
    return "earth-symbolic";
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_CITY:
    return "city-symbolic";
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_NEIGHBORHOOD:
    return "shop-symbolic";
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_STREET:
    return "traffic-lights-symbolic";
  case G_DESKTOP_LOCATION_ACCURACY_LEVEL_EXACT:
    return "pin-symbolic";
  default:
    g_warn_if_reached ();
    return NULL;
  }
}


static void
phosh_accuracy_row_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshAccuracyRow *self = PHOSH_ACCURACY_ROW (object);

  switch (property_id) {
  case PROP_LEVEL:
    phosh_accuracy_row_set_level (self, g_value_get_uint (value));
    break;
  case PROP_SELECTED:
    phosh_accuracy_row_set_selected (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_accuracy_row_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshAccuracyRow *self = PHOSH_ACCURACY_ROW (object);

  switch (property_id) {
  case PROP_LEVEL:
    g_value_set_uint (value, self->level);
    break;
  case PROP_SELECTED:
    g_value_set_boolean (value, self->selected);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_accuracy_row_class_init (PhoshAccuracyRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_accuracy_row_set_property;
  object_class->get_property = phosh_accuracy_row_get_property;

  /**
   * PhoshAccuracyRow:level:
   *
   * The accuracy level represented by this row.
   */
  props[PROP_LEVEL] =
    g_param_spec_uint ("level", "", "",
                       0, G_MAXUINT, G_DESKTOP_LOCATION_ACCURACY_LEVEL_COUNTRY,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshAccuracyRow:selected:
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
                                               "location-quick-setting/accuracy-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshAccuracyRow, revealer);
}


static void
phosh_accuracy_row_init (PhoshAccuracyRow *self)
{
  /* Ensure initial sync */
  self->level = (GDesktopLocationAccuracyLevel) G_MAXUINT;

  gtk_widget_init_template (GTK_WIDGET (self));
}


PhoshAccuracyRow *
phosh_accuracy_row_new (GDesktopLocationAccuracyLevel level, gboolean selected)
{
  return g_object_new (PHOSH_TYPE_ACCURACY_ROW,
                       "level", level,
                       "selected", selected,
                       NULL);
}


GDesktopLocationAccuracyLevel
phosh_accuracy_row_get_level (PhoshAccuracyRow *self)
{
  g_return_val_if_fail (PHOSH_IS_ACCURACY_ROW (self), G_DESKTOP_LOCATION_ACCURACY_LEVEL_COUNTRY);

  return self->level;
}


void
phosh_accuracy_row_set_level (PhoshAccuracyRow *self, GDesktopLocationAccuracyLevel level)
{
  g_return_if_fail (PHOSH_IS_ACCURACY_ROW (self));

  if (self->level == level || level == G_MAXUINT)
    return;

  self->level = level;

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self),
                                 phosh_accuracy_row_level_to_label (level));
  hdy_action_row_set_icon_name (HDY_ACTION_ROW (self),
                                accuracy_level_to_icon_name (level));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LEVEL]);
}


void
phosh_accuracy_row_set_selected (PhoshAccuracyRow *self,
                                 gboolean          selected)
{
  g_return_if_fail (PHOSH_IS_ACCURACY_ROW (self));

  if (self->selected == selected)
    return;

  self->selected = selected;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED]);
}
