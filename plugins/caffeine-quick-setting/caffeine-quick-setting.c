/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "caffeine-quick-setting.h"
#include "interval-row.h"
#include "plugin-shell.h"
#include "status-page.h"

#include <cui-call.h>

#include <glib/gi18n.h>

#define CAFFEINE_QUICK_SETTING_SCHEMA "mobi.phosh.plugins.caffeine-quick-setting"
#define CAFFEINE_INTERVALS_KEY        "intervals"
#define CAFFEINE_SELECTED_KEY         "selected-index"

#define UPDATE_INTERVAL    1 /* seconds */
#define CAFFEINE_ON_ICON   "cafe-hot-symbolic"
#define CAFFEINE_OFF_ICON  "cafe-cold-symbolic"

/**
 * PhoshCaffeineQuickSetting:
 *
 * A quick setting to keep the session from going idle
 */

enum {
  PROP_0,
  PROP_INHIBITED,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

struct _PhoshCaffeineQuickSetting {
  PhoshQuickSetting        parent;

  PhoshStatusPage         *status_page;
  PhoshStatusIcon         *info;
  guint                    cookie;

  GtkStack                *stack;
  GtkListBox              *listbox;
  GtkListBoxRow           *cur_row;
  GSettings               *settings;

  uint                     remaining;
  uint                     update_id;
};

G_DEFINE_TYPE (PhoshCaffeineQuickSetting, phosh_caffeine_quick_setting, PHOSH_TYPE_QUICK_SETTING);

static void
phosh_caffeine_quick_setting_inhibit (PhoshCaffeineQuickSetting *self, gboolean inhibit)
{
  PhoshSessionManager *manager = phosh_shell_get_session_manager (phosh_shell_get_default ());

  if (inhibit  == !!self->cookie)
    return;

  if (inhibit) {
    self->cookie = phosh_session_manager_inhibit (manager,
                                                  PHOSH_SESSION_INHIBIT_IDLE |
                                                  PHOSH_SESSION_INHIBIT_SUSPEND,
    /* Translators: Phosh prevents the session from going idle because the caffeine quick setting is toggled */
                                                  _("Phosh on caffeine"));
  } else {
    phosh_session_manager_uninhibit (manager, self->cookie);
    self->cookie = 0;
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_INHIBITED]);
}


static void
phosh_caffeine_quick_setting_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  PhoshCaffeineQuickSetting *self = PHOSH_CAFFEINE_QUICK_SETTING (object);

  switch (property_id) {
    case PROP_INHIBITED:
      phosh_caffeine_quick_setting_inhibit (self, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
phosh_caffeine_quick_setting_get_property (GObject *object,
                                           guint property_id,
                                           GValue *value,
                                           GParamSpec *pspec)
{
  PhoshCaffeineQuickSetting *self = PHOSH_CAFFEINE_QUICK_SETTING (object);

  switch (property_id) {
    case PROP_INHIBITED:
      g_value_set_boolean (value, !!self->cookie);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


static void
phosh_caffeine_quick_setting_clear_timer (PhoshCaffeineQuickSetting *self)
{
  self->remaining = 0;

  g_clear_handle_id (&self->update_id, g_source_remove);

  phosh_caffeine_quick_setting_inhibit (self, FALSE);
}


static gboolean
on_update_expired (gpointer user_data)
{
  PhoshCaffeineQuickSetting *self = PHOSH_CAFFEINE_QUICK_SETTING (user_data);

  g_return_val_if_fail (self->remaining >= UPDATE_INTERVAL, FALSE);
  self->remaining -= UPDATE_INTERVAL;
  if (self->remaining > 0) {
    g_autofree char *label = cui_call_format_duration ((double) self->remaining);
    phosh_status_icon_set_info (self->info, label);

    return G_SOURCE_CONTINUE;
  }

  phosh_caffeine_quick_setting_clear_timer (self);

  return G_SOURCE_REMOVE;
}


static void
on_clicked (PhoshCaffeineQuickSetting *self)
{
  uint value;

  if (self->cur_row)
    value = phosh_interval_row_get_value (PHOSH_INTERVAL_ROW (self->cur_row));
  else
    value = PHOSH_INTERVAL_ROW_INFINITY_VALUE; /* No interval, default to infinity */

  if (value >= PHOSH_INTERVAL_ROW_INFINITY_VALUE) {
    phosh_caffeine_quick_setting_inhibit (self, !self->cookie);

    return;
  }

  /* Clear timer if on */
  if (self->update_id) {
    phosh_caffeine_quick_setting_clear_timer (self);

    return;
  }

  self->remaining = value;
  self->update_id = g_timeout_add_seconds (UPDATE_INTERVAL, on_update_expired, self);
  phosh_caffeine_quick_setting_inhibit (self, TRUE);
}


static void
phosh_caffeine_quick_setting_set_selected_row (PhoshCaffeineQuickSetting *self,
                                               GtkListBoxRow             *row)
{
  if (self->cur_row)
    phosh_interval_row_set_selected (PHOSH_INTERVAL_ROW (self->cur_row), FALSE);

  self->cur_row = row;
  phosh_interval_row_set_selected (PHOSH_INTERVAL_ROW (self->cur_row), TRUE);
}


static void
on_interval_row_activated (GtkListBox                *listbox,
                           GtkListBoxRow             *row,
                           PhoshCaffeineQuickSetting *self)
{
  uint selected_idx = 0;
  g_autoptr (GList) children = NULL;

  if (self->cur_row != row) {
    phosh_caffeine_quick_setting_clear_timer (self);

    children = gtk_container_get_children (GTK_CONTAINER (self->listbox));
    for (GList *child = children; child; child = child->next) {
      if (child->data == row)
        break;

      selected_idx++;
    }

    g_settings_set_uint (self->settings, CAFFEINE_SELECTED_KEY, selected_idx);
    /* Setting selected-index will update row selection, but
     * do it here too to start the timer */
    phosh_caffeine_quick_setting_set_selected_row (self, row);
    on_clicked (self);
  }

  g_signal_emit_by_name (self->status_page, "done", TRUE);
}


static gboolean
transform_to_icon_name (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  gboolean inhibited = g_value_get_boolean (from_value);
  const char *icon_name;

  icon_name = inhibited ? CAFFEINE_ON_ICON : CAFFEINE_OFF_ICON;
  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static gboolean
transform_to_label (GBinding     *binding,
                    const GValue *from_value,
                    GValue       *to_value,
                    gpointer      user_data)
{
  PhoshCaffeineQuickSetting *self = PHOSH_CAFFEINE_QUICK_SETTING (user_data);
  gboolean inhibited = g_value_get_boolean (from_value);

  if (!inhibited) {
    g_value_set_string (to_value, C_("caffeine-disabled", "Off"));
  } else if (self->update_id) {
    g_autofree char *label = cui_call_format_duration ((uint) self->remaining);
    g_value_set_string (to_value, label);
  } else {
    g_value_set_string (to_value, C_("caffeine-enabled", "On"));
  }

  return TRUE;
}


static void
phosh_caffeine_quick_setting_finalize (GObject *gobject)
{
  PhoshCaffeineQuickSetting *self = PHOSH_CAFFEINE_QUICK_SETTING (gobject);

  if (self->cookie)
    phosh_caffeine_quick_setting_inhibit (self, FALSE);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_caffeine_quick_setting_parent_class)->finalize (gobject);
}


static void
phosh_caffeine_quick_setting_class_init (PhoshCaffeineQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_caffeine_quick_setting_finalize;
  object_class->set_property = phosh_caffeine_quick_setting_set_property;
  object_class->get_property = phosh_caffeine_quick_setting_get_property;

  props[PROP_INHIBITED] =
    g_param_spec_boolean ("inhibited", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY |G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/caffeine-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSetting, info);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSetting, status_page);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSetting, listbox);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSetting, stack);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_interval_row_activated);
}


static int
sort_rows_func (GtkListBoxRow *r1, GtkListBoxRow *r2, gpointer user_data)
{
  uint v1 = phosh_interval_row_get_value (PHOSH_INTERVAL_ROW (r1));
  uint v2 = phosh_interval_row_get_value (PHOSH_INTERVAL_ROW (r2));

  /* Overflow is possible as values are unsigned,
   * so we compare them manually. */
  if (v1 == v2)
    return 0;

  return v1 < v2 ? -1 : 1;
}


static void
on_selected_index_changed (PhoshCaffeineQuickSetting *self)
{
  uint selected_idx = g_settings_get_uint (self->settings, CAFFEINE_SELECTED_KEY);
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->listbox));
  uint len = g_list_length (children);

  phosh_caffeine_quick_setting_clear_timer (self);

  /* Possible that we don't have any intervals */
  if (len) {
    GtkListBoxRow *row;

    /* If index is out of bounds, select the last in the list */
    if (selected_idx >= len) {
      g_debug ("Invalid interval index, defaulting to last interval");

      selected_idx = len - 1;
    }

    row = gtk_list_box_get_row_at_index (self->listbox, selected_idx);
    phosh_caffeine_quick_setting_set_selected_row (self, row);
  }
}


static void
on_intervals_changed (PhoshCaffeineQuickSetting *self)
{
  uint interval;
  GVariantIter iter;
  PhoshIntervalRow* row = NULL;
  g_autoptr (GVariant) intervals = NULL;
  g_autoptr (GList) children = gtk_container_get_children (GTK_CONTAINER (self->listbox));

  g_debug ("Intervals changed, reconfiguring listbox...");

  self->cur_row = NULL;

  for (GList *child = children; child; child = child->next)
    gtk_container_remove (GTK_CONTAINER (self->listbox), child->data);

  intervals = g_settings_get_value (self->settings, CAFFEINE_INTERVALS_KEY);

  g_variant_iter_init (&iter, intervals);
  while (g_variant_iter_next (&iter, "u", &interval)) {
    row = phosh_interval_row_new (interval, FALSE);
    gtk_list_box_insert (self->listbox, GTK_WIDGET (row), -1);
  }

  on_selected_index_changed (self);
  gtk_stack_set_visible_child_name (self->stack,
                                    g_variant_n_children (intervals) ? "listbox" : "empty-state");
}


static void
phosh_caffeine_quick_setting_init (PhoshCaffeineQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/caffeine-quick-setting/icons");

  self->settings = g_settings_new (CAFFEINE_QUICK_SETTING_SCHEMA);

  gtk_list_box_set_sort_func (self->listbox, sort_rows_func, NULL, NULL);

  g_signal_connect_object (self->settings,
                           "changed::" CAFFEINE_INTERVALS_KEY,
                           G_CALLBACK (on_intervals_changed),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::" CAFFEINE_SELECTED_KEY,
                           G_CALLBACK (on_selected_index_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_object_bind_property (self, "inhibited",
                          self, "active",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (self, "inhibited",
                               self->info, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_to_icon_name,
                               NULL, NULL, NULL);

  g_object_bind_property_full (self, "inhibited",
                               self->info, "info",
                               G_BINDING_SYNC_CREATE,
                               transform_to_label,
                               NULL, self, NULL);

  on_intervals_changed (self);
}
