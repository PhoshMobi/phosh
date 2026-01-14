/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Gotam Gorabh <gautamy672@gmail.com>
 */

#include "accuracy-row.h"
#include "location-quick-setting.h"
#include "quick-setting.h"
#include "status-icon.h"

#include <glib/gi18n.h>

#define LOCATION_SETTINGS "org.gnome.system.location"
#define ENABLED_KEY "enabled"
#define MAX_ACCURACY_LEVEL_KEY "max-accuracy-level"

/**
 * PhoshLocationQuickSetting:
 *
 * A quick setting to toggle location services on/off.
 *
 * The status page exposes only three predefined `GDesktopLocationAccuracyLevel`
 * valuesas selectable rows: COUNTRY (0), CITY (1), and EXACT (4).
 *
 * The other levels (NEIGHBORHOOD (2) and STREET (3)) are not offered to the user
 * to choose because street, neighborhood and exact might not have usefull differences
 * in accuracy, but it appear if they were previously set via GSettings.
 *
 * A dedicated "custom_row" (initially hidden) is dynamically shown when the
 * stored max-accuracy-level is NEIGHBORHOOD or STREET. This ensures the status page
 * accurately reflects the current GSettings value.
 */
struct _PhoshLocationQuickSetting {
  PhoshQuickSetting        parent;

  GSettings               *settings;
  PhoshStatusPage         *status_page;
  PhoshStatusIcon         *info;
  
  GtkStack                *stack;
  GtkListBox              *listbox;
  GtkListBoxRow           *cur_row;
  PhoshAccuracyRow        *custom_row;
};

G_DEFINE_TYPE (PhoshLocationQuickSetting, phosh_location_quick_setting, PHOSH_TYPE_QUICK_SETTING);

static void
on_clicked (PhoshLocationQuickSetting *self)
{
  gboolean enabled = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));

  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), !enabled);
}


static void
on_enable_clicked (PhoshLocationQuickSetting *self)
{
  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), TRUE);
}


static gboolean
transform_to_icon_name (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  gboolean enabled = g_value_get_boolean (from_value);
  const char *icon_name;

  icon_name = enabled ? "location-services-active-symbolic" : "location-services-disabled-symbolic";
  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static gboolean
transform_to_label (GBinding     *binding,
                    const GValue *from_value,
                    GValue       *to_value,
                    gpointer      user_data)
{
  gboolean enabled = g_value_get_boolean (from_value);
  const char *label;

  label = enabled ? _("Location On") : _("Location Off");
  g_value_set_string (to_value, label);
  return TRUE;
}

static void
phosh_location_quick_setting_finalize (GObject *object)
{
  PhoshLocationQuickSetting *self = PHOSH_LOCATION_QUICK_SETTING (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_location_quick_setting_parent_class)->finalize (object);
}


static void
on_accuracy_row_activated (GtkListBox                *listbox,
                           GtkListBoxRow             *row,
                           PhoshLocationQuickSetting *self)
{
  if (self->cur_row != row) {
    GDesktopLocationAccuracyLevel level = phosh_accuracy_row_get_level (PHOSH_ACCURACY_ROW (row));
    g_settings_set_enum (self->settings, MAX_ACCURACY_LEVEL_KEY, level);
  }

  g_signal_emit_by_name (self->status_page, "done", TRUE);
}


static int
sort_accuracy_rows (GtkListBoxRow *row1,
                    GtkListBoxRow *row2,
                    gpointer       user_data)
{
  GDesktopLocationAccuracyLevel level1 = phosh_accuracy_row_get_level (PHOSH_ACCURACY_ROW (row1));
  GDesktopLocationAccuracyLevel level2 = phosh_accuracy_row_get_level (PHOSH_ACCURACY_ROW (row2));

  return level1 - level2;
}


static void
phosh_location_quick_setting_class_init (PhoshLocationQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_location_quick_setting_finalize;

  g_type_ensure (PHOSH_TYPE_ACCURACY_ROW);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/location-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, info);

  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, status_page);
  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, listbox);
  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, stack);
  gtk_widget_class_bind_template_child (widget_class, PhoshLocationQuickSetting, custom_row);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_enable_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_accuracy_row_activated);
}


static void
set_selected_accuracy_row (PhoshLocationQuickSetting *self, GtkListBoxRow *row)
{
  if (self->cur_row)
    phosh_accuracy_row_set_selected (PHOSH_ACCURACY_ROW (self->cur_row), FALSE);

  self->cur_row = row;
  if (row)
    phosh_accuracy_row_set_selected (PHOSH_ACCURACY_ROW (self->cur_row), TRUE);
}


static void
update_stack_page_cb (PhoshLocationQuickSetting *self)
{
  gboolean active = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));

  gtk_stack_set_visible_child_name (self->stack, active ? "listbox" : "empty-state");
}


static void
on_max_accuracy_changed (PhoshLocationQuickSetting *self)
{
  GDesktopLocationAccuracyLevel max_accuracy;
  g_autoptr (GList) children = NULL;

  max_accuracy = g_settings_get_enum (self->settings, MAX_ACCURACY_LEVEL_KEY);

  if (max_accuracy == G_DESKTOP_LOCATION_ACCURACY_LEVEL_NEIGHBORHOOD ||
      max_accuracy == G_DESKTOP_LOCATION_ACCURACY_LEVEL_STREET) {

    phosh_accuracy_row_set_level (self->custom_row, max_accuracy);
    gtk_widget_set_visible (GTK_WIDGET (self->custom_row), TRUE);
    set_selected_accuracy_row (self, GTK_LIST_BOX_ROW (self->custom_row));
    /* Visibility changes can affect listbox ordering, so
     * force the listbox to re-evaluate its sort function. */
    gtk_list_box_invalidate_sort (self->listbox);
    return;
  }

  gtk_widget_set_visible (GTK_WIDGET (self->custom_row), FALSE);

  children = gtk_container_get_children (GTK_CONTAINER (self->listbox));
  for (GList *child = children; child; child = child->next) {
    PhoshAccuracyRow *row = PHOSH_ACCURACY_ROW (child->data);
    if (phosh_accuracy_row_get_level (row) == max_accuracy) {
      set_selected_accuracy_row (self, GTK_LIST_BOX_ROW (row));
      return;
    }
  }

  set_selected_accuracy_row (self, NULL);
}


static void
phosh_location_quick_setting_init (PhoshLocationQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/location-quick-setting/icons");

  gtk_list_box_set_sort_func (self->listbox, sort_accuracy_rows, NULL, NULL);

  self->settings = g_settings_new (LOCATION_SETTINGS);

  g_settings_bind (self->settings, "enabled",
                   self, "active",
                   G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  g_signal_connect_object (self,
                           "notify::active",
                           G_CALLBACK (update_stack_page_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->settings,
                           "changed::" MAX_ACCURACY_LEVEL_KEY,
                           G_CALLBACK (on_max_accuracy_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_object_bind_property_full (self, "active",
                               self->info, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_to_icon_name,
                               NULL, NULL, NULL);

  g_object_bind_property_full (self, "active",
                               self->info, "info",
                               G_BINDING_SYNC_CREATE,
                               transform_to_label,
                               NULL, NULL, NULL);

  update_stack_page_cb (self);
  on_max_accuracy_changed (self);
}
