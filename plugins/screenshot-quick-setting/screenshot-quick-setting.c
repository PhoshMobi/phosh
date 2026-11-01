/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "screenshot-quick-setting.h"
#include "plugin-shell.h"
#include "quick-setting-selection-row.h"
#include "screenshot-manager.h"

#include <glib/gi18n.h>

#define SCREENSHOT_QUICK_SETTING_SCHEMA "mobi.phosh.plugins.screenshot-quick-setting"
#define KEY_CAPTURE_DELAY               "capture-delay"
#define KEY_CAPTURE_AREA_ENABLED        "capture-area-enabled"

/**
 * PhoshScreenshotQuickSetting:
 *
 * A quick setting to take screenshots
 */

struct _PhoshScreenshotQuickSetting {
  PhoshQuickSetting parent;

  PhoshStatusIcon  *info;
  PhoshStatusPage  *status_page;
  GtkListBox       *list_box;
  GtkButton        *btn_capture;
  GtkLabel         *btn_label;

  GSettings        *settings;

  gboolean          area_mode;
  guint selected_delay;

  PhoshQuickSettingSelectionRow *row_full;
  PhoshQuickSettingSelectionRow *row_area;
  PhoshQuickSettingSelectionRow *row_delay_0;
  PhoshQuickSettingSelectionRow *row_delay_1;
  PhoshQuickSettingSelectionRow *row_delay_3;
  PhoshQuickSettingSelectionRow *row_delay_10;

  /* Capture armed while the quick settings panel folds */
  gboolean             capture_pending;
  guint                capture_timeout_id;
  /* An area selection is in progress */
  gboolean             area_select_pending;
  /* The shell state we last saw so we can tell a fold from unrelated changes */
  PhoshShellStateFlags shell_state;

  GCancellable        *cancel;
  GSimpleActionGroup  *actions;
};

G_DEFINE_TYPE (PhoshScreenshotQuickSetting, phosh_screenshot_quick_setting,
               PHOSH_TYPE_QUICK_SETTING)


static void
update_capture_button (PhoshScreenshotQuickSetting *self)
{
  gboolean busy = self->capture_pending ||
                  self->capture_timeout_id != 0 ||
                  self->area_select_pending;

  gtk_widget_set_sensitive (GTK_WIDGET (self->btn_capture), !busy);
}


static void
on_area_selected (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshScreenshotManager *manager = PHOSH_SCREENSHOT_MANAGER (source_object);
  PhoshScreenshotQuickSetting *self = PHOSH_SCREENSHOT_QUICK_SETTING (user_data);
  g_autofree GdkRectangle *area = NULL;
  g_autoptr (GError) err = NULL;

  area = phosh_screenshot_manager_select_area_finish (manager, res, &err);
  /* Cancelled means the object is going away, don't touch self */
  if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  if (area == NULL)
    g_warning ("Area selection failed: %s", err->message);
  else
    phosh_screenshot_manager_take_screenshot (manager, area, NULL, TRUE, FALSE);

  self->area_select_pending = FALSE;
  update_capture_button (self);
}


static void
start_capture (PhoshScreenshotQuickSetting *self)
{
  PhoshScreenshotManager *manager = phosh_shell_get_screenshot_manager (phosh_shell_get_default ());

  g_return_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (manager));

  if (self->area_mode) {
    /* The callback is tied to our cancellable and holds no extra reference */
    self->area_select_pending = TRUE;
    phosh_screenshot_manager_select_area_async (manager,
                                                self->cancel,
                                                on_area_selected,
                                                self);
  } else {
    phosh_screenshot_manager_take_screenshot (manager, NULL, NULL, TRUE, FALSE);
  }

  update_capture_button (self);
}


static void
on_capture_timeout (gpointer user_data)
{
  PhoshScreenshotQuickSetting *self = PHOSH_SCREENSHOT_QUICK_SETTING (user_data);

  self->capture_timeout_id = 0;
  start_capture (self);
}


static void
cancel_pending_capture (PhoshScreenshotQuickSetting *self)
{
  g_clear_handle_id (&self->capture_timeout_id, g_source_remove);
  self->capture_pending = FALSE;
  update_capture_button (self);
}


static void
on_shell_state_changed (PhoshScreenshotQuickSetting *self,
                        GParamSpec                  *pspec,
                        PhoshShell                  *shell)
{
  PhoshShellStateFlags state;
  gboolean settings_unfolded, was_unfolded;

  g_return_if_fail (PHOSH_IS_SCREENSHOT_QUICK_SETTING (self));
  g_return_if_fail (PHOSH_IS_SHELL (shell));

  state = phosh_shell_get_state (shell);
  settings_unfolded = !!(state & PHOSH_STATE_SETTINGS);
  was_unfolded = !!(self->shell_state & PHOSH_STATE_SETTINGS);
  self->shell_state = state;

  /* Only react to the settings panel unfolding or folding */
  if (settings_unfolded == was_unfolded)
    return;

  if (!self->capture_pending)
    return;

  if (settings_unfolded) {
    /* The settings panel got unfolded again, cancel the capture */
    cancel_pending_capture (self);
    return;
  }

  /* The settings panel finished folding, the screen is ready to capture */
  self->capture_pending = FALSE;

  if (self->selected_delay > 0) {
    self->capture_timeout_id = g_timeout_add_seconds_once (self->selected_delay,
                                                           on_capture_timeout, self);
    g_source_set_name_by_id (self->capture_timeout_id, "[phosh] screenshot capture delay");
    update_capture_button (self);
  } else {
    start_capture (self);
  }
}


static void
on_capture_clicked (PhoshScreenshotQuickSetting *self)
{
  /* Allow one capture at a time */
  if (self->capture_pending || self->capture_timeout_id != 0 || self->area_select_pending)
    return;

  self->capture_pending = TRUE;
  update_capture_button (self);

  phosh_shell_fold_top_panel (phosh_shell_get_default ());
  g_signal_emit_by_name (self, "hide-status");
}


static const char *
button_label_text (gboolean area_mode, guint delay)
{
  static const struct {
    guint       delay;
    const char *full;
    const char *area;
  } labels[] = {
    { 0,
      /* Translators: Label for taking a full-screen screenshot immediately. */
      N_("Screenshot now"),
      /* Translators: Label for taking a screenshot of a selected area immediately. */
      N_("Screenshot area now") },
    { 1,
      /* Translators: Label for taking a full-screen screenshot after a delay. */
      N_("Screenshot in 1s"),
      /* Translators: Label for taking a screenshot of a selected area after a delay. */
      N_("Screenshot area in 1s") },
    { 3,
      /* Translators: Label for taking a full-screen screenshot after a delay. */
      N_("Screenshot in 3s"),
      /* Translators: Label for taking a screenshot of a selected area after a delay. */
      N_("Screenshot area in 3s") },
    { 10,
      /* Translators: Label for taking a full-screen screenshot after a delay. */
      N_("Screenshot in 10s"),
      /* Translators: Label for taking a screenshot of a selected area after a delay. */
      N_("Screenshot area in 10s") },
  };

  for (guint i = 0; i < G_N_ELEMENTS (labels); i++) {
    if (labels[i].delay == delay)
      return area_mode ? _(labels[i].area) : _(labels[i].full);
  }

  return area_mode ? _("Screenshot area") : _("Screenshot");
}


static void
update_rows (PhoshScreenshotQuickSetting *self)
{
  phosh_quick_setting_selection_row_set_selected (self->row_full, !self->area_mode);
  phosh_quick_setting_selection_row_set_selected (self->row_area, self->area_mode);
  phosh_quick_setting_selection_row_set_selected (self->row_delay_0, self->selected_delay == 0);
  phosh_quick_setting_selection_row_set_selected (self->row_delay_1, self->selected_delay == 1);
  phosh_quick_setting_selection_row_set_selected (self->row_delay_3, self->selected_delay == 3);
  phosh_quick_setting_selection_row_set_selected (self->row_delay_10, self->selected_delay == 10);

  gtk_label_set_text (self->btn_label,
                      button_label_text (self->area_mode, self->selected_delay));
}


static void
read_settings (PhoshScreenshotQuickSetting *self)
{
  guint delay;

  delay = g_settings_get_uint (self->settings, KEY_CAPTURE_DELAY);
  if (delay != 0 && delay != 1 && delay != 3 && delay != 10) {
    g_warning ("Invalid screenshot delay %u, using 0", delay);
    delay = 0;
    g_settings_set_uint (self->settings, KEY_CAPTURE_DELAY, delay);
  }

  self->selected_delay = delay;
  self->area_mode = g_settings_get_boolean (self->settings, KEY_CAPTURE_AREA_ENABLED);

  update_rows (self);
}


static void
on_settings_changed (PhoshScreenshotQuickSetting *self, const char *key, GSettings *settings)
{
  g_return_if_fail (PHOSH_IS_SCREENSHOT_QUICK_SETTING (self));
  g_return_if_fail (G_IS_SETTINGS (settings));

  read_settings (self);
}


static void
on_mode_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  PhoshScreenshotQuickSetting *self = PHOSH_SCREENSHOT_QUICK_SETTING (user_data);

  g_return_if_fail (PHOSH_IS_SCREENSHOT_QUICK_SETTING (self));
  g_return_if_fail (g_variant_is_of_type (parameter, G_VARIANT_TYPE_BOOLEAN));

  self->area_mode = g_variant_get_boolean (parameter);
  g_settings_set_boolean (self->settings, KEY_CAPTURE_AREA_ENABLED, self->area_mode);

  update_rows (self);
}


static void
on_delay_activated (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  PhoshScreenshotQuickSetting *self = PHOSH_SCREENSHOT_QUICK_SETTING (user_data);

  g_return_if_fail (PHOSH_IS_SCREENSHOT_QUICK_SETTING (self));
  g_return_if_fail (g_variant_is_of_type (parameter, G_VARIANT_TYPE_UINT32));

  g_settings_set_uint (self->settings, KEY_CAPTURE_DELAY,
                       g_variant_get_uint32 (parameter));
}


static const GActionEntry action_entries[] = {
  { .name = "set-mode", .parameter_type = "b", .activate = on_mode_activated },
  { .name = "set-delay", .parameter_type = "u", .activate = on_delay_activated },
};


static void
phosh_screenshot_quick_setting_dispose (GObject *object)
{
  PhoshScreenshotQuickSetting *self = PHOSH_SCREENSHOT_QUICK_SETTING (object);

  g_cancellable_cancel (self->cancel);
  cancel_pending_capture (self);
  g_clear_object (&self->cancel);
  g_clear_object (&self->actions);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_screenshot_quick_setting_parent_class)->dispose (object);
}


static void
phosh_screenshot_quick_setting_class_init (PhoshScreenshotQuickSettingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_screenshot_quick_setting_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "screenshot-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, info);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, status_page);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, list_box);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, btn_capture);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, btn_label);

  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_full);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_area);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_delay_0);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_delay_1);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_delay_3);
  gtk_widget_class_bind_template_child (widget_class, PhoshScreenshotQuickSetting, row_delay_10);

  gtk_widget_class_bind_template_callback (widget_class, on_capture_clicked);
}


static void
phosh_screenshot_quick_setting_init (PhoshScreenshotQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancel = g_cancellable_new ();
  self->actions = g_simple_action_group_new ();
  g_action_map_add_action_entries (G_ACTION_MAP (self->actions),
                                   action_entries,
                                   G_N_ELEMENTS (action_entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self->status_page),
                                  "screenshot",
                                  G_ACTION_GROUP (self->actions));

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/screenshot-quick-setting/icons");

  self->settings = g_settings_new (SCREENSHOT_QUICK_SETTING_SCHEMA);
  read_settings (self);

  g_signal_connect_object (self->settings, "changed::" KEY_CAPTURE_DELAY,
                           G_CALLBACK (on_settings_changed), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings, "changed::" KEY_CAPTURE_AREA_ENABLED,
                           G_CALLBACK (on_settings_changed), self,
                           G_CONNECT_SWAPPED);

  self->shell_state = phosh_shell_get_state (phosh_shell_get_default ());

  /* Take the screenshot once the settings panel finished folding */
  g_signal_connect_object (phosh_shell_get_default (), "notify::shell-state",
                           G_CALLBACK (on_shell_state_changed), self,
                           G_CONNECT_SWAPPED);

  update_capture_button (self);
}
