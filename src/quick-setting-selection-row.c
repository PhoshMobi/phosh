/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "quick-setting-selection-row.h"

/**
 * PhoshQuickSettingSelectionRow:
 *
 * A reusable action row with an optional leading icon and a selection
 * indicator. It is intended for mutually exclusive choices in quick-setting
 * status pages.
 */

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_SELECTED,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshQuickSettingSelectionRow {
  HdyActionRow parent;

  GtkImage    *icon;
  GtkRevealer *revealer;
  char        *icon_name;
  gboolean     selected;
};

G_DEFINE_TYPE (PhoshQuickSettingSelectionRow,
               phosh_quick_setting_selection_row,
               HDY_TYPE_ACTION_ROW)


static void
phosh_quick_setting_selection_row_set_property (GObject      *object,
                                                guint         property_id,
                                                const GValue *value,
                                                GParamSpec   *pspec)
{
  PhoshQuickSettingSelectionRow *self = PHOSH_QUICK_SETTING_SELECTION_ROW (object);

  switch (property_id) {
  case PROP_ICON_NAME:
    phosh_quick_setting_selection_row_set_icon_name (self, g_value_get_string (value));
    break;
  case PROP_SELECTED:
    phosh_quick_setting_selection_row_set_selected (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_quick_setting_selection_row_get_property (GObject    *object,
                                                guint       property_id,
                                                GValue     *value,
                                                GParamSpec *pspec)
{
  PhoshQuickSettingSelectionRow *self = PHOSH_QUICK_SETTING_SELECTION_ROW (object);

  switch (property_id) {
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
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
phosh_quick_setting_selection_row_finalize (GObject *object)
{
  PhoshQuickSettingSelectionRow *self = PHOSH_QUICK_SETTING_SELECTION_ROW (object);

  g_clear_pointer (&self->icon_name, g_free);

  G_OBJECT_CLASS (phosh_quick_setting_selection_row_parent_class)->finalize (object);
}


static void
phosh_quick_setting_selection_row_class_init (PhoshQuickSettingSelectionRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_quick_setting_selection_row_set_property;
  object_class->get_property = phosh_quick_setting_selection_row_get_property;
  object_class->finalize = phosh_quick_setting_selection_row_finalize;

  /**
   * PhoshQuickSettingSelectionRow:icon-name:
   *
   * The optional icon shown before the row title.
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshQuickSettingSelectionRow:selected:
   *
   * Whether this row is selected.
   */
  props[PROP_SELECTED] =
    g_param_spec_boolean ("selected", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/quick-setting-selection-row.ui");
  gtk_widget_class_bind_template_child (widget_class,
                                        PhoshQuickSettingSelectionRow,
                                        icon);
  gtk_widget_class_bind_template_child (widget_class,
                                        PhoshQuickSettingSelectionRow,
                                        revealer);
  gtk_widget_class_set_accessible_role (widget_class, ATK_ROLE_RADIO_BUTTON);
}


static void
phosh_quick_setting_selection_row_init (PhoshQuickSettingSelectionRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* Only show the icon once we have one */
  gtk_widget_set_visible (GTK_WIDGET (self->icon), FALSE);
}


PhoshQuickSettingSelectionRow *
phosh_quick_setting_selection_row_new (const char *title,
                                       const char *icon_name,
                                       gboolean    selected)
{
  return g_object_new (PHOSH_TYPE_QUICK_SETTING_SELECTION_ROW,
                       "title", title,
                       "icon-name", icon_name,
                       "selected", selected,
                       NULL);
}


void
phosh_quick_setting_selection_row_set_icon_name (PhoshQuickSettingSelectionRow *self,
                                                 const char                    *icon_name)
{
  g_return_if_fail (PHOSH_IS_QUICK_SETTING_SELECTION_ROW (self));

  if (!g_set_str (&self->icon_name, icon_name))
    return;

  gtk_image_set_from_icon_name (self->icon, self->icon_name, GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_visible (GTK_WIDGET (self->icon), self->icon_name != NULL);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}


const char *
phosh_quick_setting_selection_row_get_icon_name (PhoshQuickSettingSelectionRow *self)
{
  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING_SELECTION_ROW (self), NULL);

  return self->icon_name;
}


void
phosh_quick_setting_selection_row_set_selected (PhoshQuickSettingSelectionRow *self,
                                                gboolean                       selected)
{
  g_return_if_fail (PHOSH_IS_QUICK_SETTING_SELECTION_ROW (self));

  selected = !!selected;
  if (self->selected == selected)
    return;

  self->selected = selected;
  gtk_revealer_set_reveal_child (self->revealer, selected);
  atk_object_notify_state_change (gtk_widget_get_accessible (GTK_WIDGET (self)),
                                  ATK_STATE_CHECKED,
                                  selected);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SELECTED]);
}


gboolean
phosh_quick_setting_selection_row_get_selected (PhoshQuickSettingSelectionRow *self)
{
  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING_SELECTION_ROW (self), FALSE);

  return self->selected;
}
