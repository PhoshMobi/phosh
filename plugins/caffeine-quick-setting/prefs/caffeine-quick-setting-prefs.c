/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Rudra Pratap Singh <rudrasingh900@gmail.com>
 */

#include "phosh-plugin-prefs-config.h"

#include "caffeine-quick-setting-prefs.h"
#include "caffeine-quick-setting-enums.h"
#include "caffeine-quick-setting-enum-types.h"

#include <cui-call.h>

#include <glib/gi18n.h>

#define INFINITY_VALUE                G_MAXUINT32
#define CAFFEINE_QUICK_SETTING_SCHEMA "mobi.phosh.plugins.caffeine-quick-setting"
#define CAFFEINE_INTERVALS_KEY        "intervals"
#define CAFFEINE_MODE_KEY             "mode"

enum {
  PROP_0,
  PROP_MODE,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

struct _PhoshCaffeineQuickSettingPrefs {
  AdwPreferencesDialog parent;

  GtkStack *stack;
  GtkListBox          *listbox;
  GtkSpinButton       *hours_btn;
  GtkSpinButton       *minutes_btn;
  GtkSpinButton       *seconds_btn;
  AdwDialog *add_interval_dialog;
  GSimpleActionGroup  *action_group;
  GSettings *settings;

  AdwSwitchRow        *idle_switch;
  AdwSwitchRow        *suspend_switch;
  PhoshCaffeineInhibitModeFlags mode;
};

G_DEFINE_TYPE (PhoshCaffeineQuickSettingPrefs,
               phosh_caffeine_quick_setting_prefs,
               ADW_TYPE_PREFERENCES_DIALOG);


static void
phosh_caffeine_quick_setting_prefs_set_mode (PhoshCaffeineQuickSettingPrefs *self,
                                             PhoshCaffeineInhibitModeFlags   mode)
{
  if (self->mode == mode)
    return;

  self->mode = mode;

  g_object_freeze_notify (G_OBJECT (self));
  adw_switch_row_set_active (self->idle_switch,
                             !!(self->mode & PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_IDLE));
  adw_switch_row_set_active (self->suspend_switch,
                             !!(self->mode & PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_SUSPEND));
  g_object_thaw_notify (G_OBJECT (self));

  g_debug ("New mode: 0x%x", self->mode);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODE]);
}


static void
phosh_caffeine_quick_setting_prefs_set_property (GObject      *object,
                                                 guint         property_id,
                                                 const GValue *value,
                                                 GParamSpec   *pspec)
{
  PhoshCaffeineQuickSettingPrefs *self = PHOSH_CAFFEINE_QUICK_SETTING_PREFS (object);

  switch (property_id) {
  case PROP_MODE:
    phosh_caffeine_quick_setting_prefs_set_mode (self, g_value_get_flags (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phosh_caffeine_quick_setting_prefs_get_property (GObject    *object,
                                                 guint       property_id,
                                                 GValue     *value,
                                                 GParamSpec *pspec)
{
  PhoshCaffeineQuickSettingPrefs *self = PHOSH_CAFFEINE_QUICK_SETTING_PREFS (object);

  switch (property_id) {
  case PROP_MODE:
    g_value_set_flags (value, self->mode);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phosh_caffeine_quick_setting_prefs_finalize (GObject *object)
{
  PhoshCaffeineQuickSettingPrefs *self = PHOSH_CAFFEINE_QUICK_SETTING_PREFS (object);

  g_clear_object (&self->settings);
  g_clear_object (&self->action_group);

  G_OBJECT_CLASS (phosh_caffeine_quick_setting_prefs_parent_class)->finalize (object);
}


static GVariantBuilder *
rebuild_intervals_without_row (PhoshCaffeineQuickSettingPrefs *self,
                               GtkListBoxRow                  *remove_row)
{
  uint i = 0;
  GtkListBoxRow *row;
  GVariantBuilder *builder = g_variant_builder_new (G_VARIANT_TYPE ("au"));

  while ((row = gtk_list_box_get_row_at_index (self->listbox, i++))) {
    uint value;

    if (row == remove_row)
      continue;

    value = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (row), "value"));
    g_variant_builder_add (builder, "u", value);
  }

  return builder;
}


static void
clear_values (PhoshCaffeineQuickSettingPrefs *self)
{
  gtk_spin_button_set_value (self->hours_btn, 0);
  gtk_spin_button_set_value (self->minutes_btn, 0);
  gtk_spin_button_set_value (self->seconds_btn, 0);
}


static void
on_quickstart_interval_clicked (GSimpleAction *action, GVariant *param, gpointer user_data)
{
  PhoshCaffeineQuickSettingPrefs *self = PHOSH_CAFFEINE_QUICK_SETTING_PREFS (user_data);
  uint value = g_variant_get_uint32 (param);
  g_autoptr (GVariantBuilder) builder = rebuild_intervals_without_row (self, NULL);

  g_variant_builder_add (builder, "u", value);
  g_settings_set_value (self->settings, CAFFEINE_INTERVALS_KEY, g_variant_builder_end (builder));

  adw_dialog_close (self->add_interval_dialog);
}


static void
on_add_interval_added (PhoshCaffeineQuickSettingPrefs *self,
                       GtkButton                      *button)
{
  uint remaining = 0;
  uint hours = (uint) gtk_spin_button_get_value (self->hours_btn);
  uint minutes_to_s = (uint) gtk_spin_button_get_value (self->minutes_btn) * 60;
  uint seconds = (uint) gtk_spin_button_get_value (self->seconds_btn);
  g_autoptr (GVariantBuilder) builder = NULL;

  /* Just watch out for hours, as it can potentially overflow */
  if (hours < INFINITY_VALUE / 3600 + INFINITY_VALUE % 3600) {
    uint t = INFINITY_VALUE - hours * 3600;

    if (t > minutes_to_s) {
      t -= minutes_to_s;
      if (t > seconds)
        remaining = t - seconds;
    }
  }

  builder = rebuild_intervals_without_row (self, NULL);
  g_variant_builder_add (builder, "u", INFINITY_VALUE - remaining);
  g_settings_set_value (self->settings, CAFFEINE_INTERVALS_KEY, g_variant_builder_end (builder));

  adw_dialog_close (self->add_interval_dialog);
}


static void
on_add_interval_cancelled (PhoshCaffeineQuickSettingPrefs *self)
{
  clear_values (self);
  adw_dialog_close (self->add_interval_dialog);
}


static void
on_add_interval_clicked (PhoshCaffeineQuickSettingPrefs *self,
                         GtkButton                      *button)
{
  adw_dialog_present (self->add_interval_dialog, GTK_WIDGET (self));
}


static void
flip_flag (PhoshCaffeineQuickSettingPrefs *self,
           PhoshCaffeineInhibitModeFlags   flag,
           gboolean                        enable)
{
  PhoshCaffeineInhibitModeFlags mode;

  if (enable)
    mode = self->mode | flag;
  else
    mode = self->mode & ~flag;

  phosh_caffeine_quick_setting_prefs_set_mode (self, mode);
}


static void
on_suspend_row_activated (PhoshCaffeineQuickSettingPrefs *self,
                          GParamSpec                     *pspec,
                          AdwSwitchRow                   *row)
{
  gboolean active;

  g_assert (PHOSH_IS_CAFFEINE_QUICK_SETTING_PREFS (self));
  g_assert (ADW_IS_SWITCH_ROW (row));

  active = adw_switch_row_get_active (row);
  flip_flag (self, PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_SUSPEND, active);
}


static void
on_idle_row_activated (PhoshCaffeineQuickSettingPrefs *self,
                       GParamSpec                     *pspec,
                       AdwSwitchRow                   *row)
{
  gboolean active;

  g_assert (PHOSH_IS_CAFFEINE_QUICK_SETTING_PREFS (self));
  g_assert (ADW_IS_SWITCH_ROW (row));

  active = adw_switch_row_get_active (row);
  flip_flag (self, PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_IDLE, active);
}


static void
phosh_caffeine_quick_setting_prefs_class_init (PhoshCaffeineQuickSettingPrefsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_caffeine_quick_setting_prefs_finalize;
  object_class->set_property = phosh_caffeine_quick_setting_prefs_set_property;
  object_class->get_property = phosh_caffeine_quick_setting_prefs_get_property;

  props[PROP_MODE] =
    g_param_spec_flags ("mode", "", "",
                        PHOSH_TYPE_CAFFEINE_INHIBIT_MODE_FLAGS,
                        PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_SUSPEND |
                        PHOSH_CAFFEINE_QUICK_SETTING_INHIBIT_MODE_IDLE,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "caffeine-quick-setting-prefs/prefs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs,
                                        add_interval_dialog);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, hours_btn);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, idle_switch);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, listbox);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, minutes_btn);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, seconds_btn);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs, stack);
  gtk_widget_class_bind_template_child (widget_class, PhoshCaffeineQuickSettingPrefs,
                                        suspend_switch);

  gtk_widget_class_bind_template_callback (widget_class, on_add_interval_added);
  gtk_widget_class_bind_template_callback (widget_class, on_add_interval_cancelled);
  gtk_widget_class_bind_template_callback (widget_class, on_add_interval_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_idle_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_suspend_row_activated);
}


static int
sort_rows_func (GtkListBoxRow *r1, GtkListBoxRow *r2, gpointer user_data)
{
  uint v1 = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (r1), "value"));
  uint v2 = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (r2), "value"));

  /* Overflow is possible as values are unsigned,
   * so we compare them manually. */
  if (v1 == v2)
    return 0;

  return v1 < v2 ? -1 : 1;
}


typedef struct {
  PhoshCaffeineQuickSettingPrefs *self;
  AdwActionRow *row; /* Row to which the button is suffixed */
} RemoveButtonData;

static void
remove_button_data_free (RemoveButtonData *data)
{
  g_object_unref (data->self);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RemoveButtonData, remove_button_data_free);


static void
on_remove_btn_clicked (gpointer user_data)
{
  g_autoptr (RemoveButtonData) data = user_data;
  g_autoptr (GVariantBuilder) builder = NULL;

  g_assert (data);

  builder = rebuild_intervals_without_row (data->self, GTK_LIST_BOX_ROW (data->row));
  g_settings_set_value (data->self->settings,
                        CAFFEINE_INTERVALS_KEY,
                        g_variant_builder_end (builder));
}


static GtkWidget *
create_remove_button (AdwActionRow *row)
{
  GtkWidget *remove_btn = gtk_button_new_from_icon_name ("list-remove-symbolic");

  gtk_widget_add_css_class (remove_btn, "flat");
  gtk_widget_set_hexpand (remove_btn, TRUE);
  gtk_widget_set_halign (remove_btn, GTK_ALIGN_END);

  return remove_btn;
}


static void
on_intervals_changed (PhoshCaffeineQuickSettingPrefs *self)
{
  uint interval;
  GVariantIter iter;
  AdwActionRow *row;
  g_autoptr (GVariant) intervals = NULL;

  gtk_list_box_remove_all (self->listbox);
  intervals = g_settings_get_value (self->settings, CAFFEINE_INTERVALS_KEY);

  g_variant_iter_init (&iter, intervals);
  while (g_variant_iter_next (&iter, "u", &interval)) {
    GtkWidget *remove_btn;
    RemoveButtonData *remove_btn_data;

    row = ADW_ACTION_ROW (adw_action_row_new ());
    remove_btn = create_remove_button (row);
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), remove_btn);

    remove_btn_data = g_new0 (RemoveButtonData, 1);
    remove_btn_data->self = g_object_ref (self);
    remove_btn_data->row = row;

    g_object_set_data (G_OBJECT (row), "value", GUINT_TO_POINTER (interval));

    if (interval < INFINITY_VALUE) {
      g_autofree char *title = cui_call_format_duration (interval);
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
    } else {
      adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), _("No timeout (∞)"));
    }

    g_signal_connect_swapped (remove_btn,
                              "clicked",
                              G_CALLBACK (on_remove_btn_clicked),
                              remove_btn_data);

    gtk_list_box_insert (self->listbox, GTK_WIDGET (row), -1);
  }

  gtk_stack_set_visible_child_name (self->stack,
                                    g_variant_n_children (intervals) ? "listbox" : "empty-state");
}


static GActionEntry entries[] =
{
  { .name = "update-value", .activate = on_quickstart_interval_clicked, .parameter_type = "u" },
};

static void
phosh_caffeine_quick_setting_prefs_init (PhoshCaffeineQuickSettingPrefs *self)
{
  g_autoptr (GtkCssProvider) css_provider = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (CAFFEINE_QUICK_SETTING_SCHEMA);
  g_settings_bind (self->settings, CAFFEINE_MODE_KEY,
                   self, "mode",
                   G_SETTINGS_BIND_DEFAULT);

  gtk_list_box_set_sort_func (self->listbox, sort_rows_func, NULL, NULL);
  g_signal_connect_object (self->settings,
                           "changed::" CAFFEINE_INTERVALS_KEY,
                           G_CALLBACK (on_intervals_changed),
                           self,
                           G_CONNECT_SWAPPED);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider,
                                       "/mobi/phosh/plugins/caffeine-quick-setting-prefs/stylesheet/common.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_for_display (gdk_display_get_default ()),
                                    "/mobi/phosh/plugins/caffeine-quick-setting-prefs/icons");

  self->action_group = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->action_group),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self->add_interval_dialog),
                                  "interval",
                                  G_ACTION_GROUP (self->action_group));

  on_intervals_changed (self);
}
