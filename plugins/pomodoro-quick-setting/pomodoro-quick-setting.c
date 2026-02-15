/*
 * Copyright (C) 2024 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "pomodoro-quick-setting.h"
#include "pomodoro-enums.h"
#include "plugin-shell.h"
#include "notify-manager.h"

#include <cui-call.h>

#include <glib/gi18n.h>

#define UPDATE_INTERVAL 1 /* seconds */
#define ACTIVE_ICON    "pomodoro-active-symbolic"
#define BREAK_ICON     "pomodoro-break-symbolic"

#define START_ON_UNLOCK_KEY "start-on-unlock"

/**
 * PhoshPomodoroQuickSetting:
 *
 * A quick setting for the Pomodoro technique
 */

enum {
  PROP_0,
  PROP_STATE,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

struct _PhoshPomodoroQuickSetting {
  PhoshQuickSetting        parent;

  PhoshStatusIcon         *info;
  PhoshPomodoroState       state;
  int                      remaining;
  guint                    update_id;
  GSettings               *settings;
};

G_DEFINE_TYPE (PhoshPomodoroQuickSetting, phosh_pomodoro_quick_setting, PHOSH_TYPE_QUICK_SETTING);

static gboolean _started_on_login;

static void
phosh_pomodoro_quick_setting_clear_timers (PhoshPomodoroQuickSetting *self)
{
  self->remaining = 0;
  g_clear_handle_id (&self->update_id, g_source_remove);
}


static void
show_notification (PhoshPomodoroQuickSetting *self)
{
  PhoshNotifyManager *noti_manager = phosh_notify_manager_get_default ();
  g_autoptr (PhoshNotification) noti = NULL;
  g_autoptr (GIcon) icon = NULL;
  g_autoptr (GIcon) app_icon = NULL;
  const char *summary;
  g_autofree char *body = NULL;

  switch (self->state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
    summary = _("Pomodoro start");
    body = g_strdup_printf (_("Focus on your task for %d minutes"),
                            g_settings_get_int (self->settings, "active-duration") / 60);
    icon = g_themed_icon_new (ACTIVE_ICON);
    break;
  case PHOSH_POMODORO_STATE_BREAK:
    summary = _("Take a break");
    /* Translators: Pomodoro is a technique, no need to translate it */
    body = g_strdup_printf (_("You have %d minutes until next Pomodoro"),
                            g_settings_get_int (self->settings, "break-duration") / 60);
    icon = g_themed_icon_new (BREAK_ICON);
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    return;
  }

  app_icon = g_themed_icon_new (ACTIVE_ICON);
  noti = g_object_new (PHOSH_TYPE_NOTIFICATION,
                       "summary", summary,
                       "body", body,
                       "image", icon,
                       /* Translators: Pomodoro is a technique, no need to translate it */
                       "app-name", _("Pomodoro Timer"),
                       "app-icon", app_icon,
                       NULL);
  phosh_notify_manager_add_shell_notification (noti_manager,
                                               noti,
                                               0,
                                               PHOSH_NOTIFICATION_DEFAULT_TIMEOUT);
}


static void
update_label (PhoshPomodoroQuickSetting *self)
{
  g_autofree char *label = NULL;

  switch (self->state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
  case PHOSH_POMODORO_STATE_BREAK:
    label = cui_call_format_duration ((double) self->remaining);
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    /* Translators: Pomodoro is a technique, no need to translate it */
    label = g_strdup_printf (_("Pomodoro Off"));
  }

  phosh_status_icon_set_info (self->info, label);
}

static void phosh_pomodoro_quick_setting_set_state (PhoshPomodoroQuickSetting *self,
                                                    PhoshPomodoroState         state);

static gboolean
on_update_expired (gpointer user_data)
{
  PhoshPomodoroQuickSetting *self = PHOSH_POMODORO_QUICK_SETTING (user_data);

  self->remaining -= UPDATE_INTERVAL;
  if (self->remaining > 0) {
    update_label (self);
    return G_SOURCE_CONTINUE;
  }

  switch (self->state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
    phosh_pomodoro_quick_setting_set_state (self, PHOSH_POMODORO_STATE_BREAK);
    break;
  case PHOSH_POMODORO_STATE_BREAK:
    phosh_pomodoro_quick_setting_set_state (self, PHOSH_POMODORO_STATE_ACTIVE);
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    g_return_val_if_reached (G_SOURCE_REMOVE);
  }

  update_label (self);
  return G_SOURCE_REMOVE;
}


static void
phosh_pomodoro_quick_setting_set_state (PhoshPomodoroQuickSetting *self, PhoshPomodoroState state)
{
  int timeout = 0;

  if (self->state == state)
    return;
  self->state = state;

  phosh_pomodoro_quick_setting_clear_timers (self);

  switch (self->state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
    timeout = g_settings_get_int (self->settings, "active-duration");
    break;
  case PHOSH_POMODORO_STATE_BREAK:
    timeout = g_settings_get_int (self->settings, "break-duration");
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    break;
  }

  if (timeout) {
    self->remaining = timeout;
    self->update_id = g_timeout_add_seconds (UPDATE_INTERVAL, on_update_expired, self);
  }

  update_label (self);
  show_notification (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
}


static void
on_shell_locked (PhoshPomodoroQuickSetting *self, GParamSpec *pspec, PhoshShell *shell)
{
  g_assert (PHOSH_IS_SHELL (shell));
  g_assert (PHOSH_IS_POMODORO_QUICK_SETTING (self));

  if (phosh_shell_get_locked (shell))
    return;

  if (self->state != PHOSH_POMODORO_STATE_OFF)
    return;

  if (!g_settings_get_boolean (self->settings, START_ON_UNLOCK_KEY))
    return;

  phosh_pomodoro_quick_setting_set_state (self, PHOSH_POMODORO_STATE_ACTIVE);
}


static void
phosh_pomodoro_quick_setting_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  PhoshPomodoroQuickSetting *self = PHOSH_POMODORO_QUICK_SETTING (object);

  switch (property_id) {
  case PROP_STATE:
    phosh_pomodoro_quick_setting_set_state (self, g_value_get_enum (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_pomodoro_quick_setting_get_property (GObject    *object,
                                           guint       property_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  PhoshPomodoroQuickSetting *self = PHOSH_POMODORO_QUICK_SETTING (object);

  switch (property_id) {
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_clicked (PhoshPomodoroQuickSetting *self)
{
  PhoshPomodoroState new_state;

  switch (self->state) {
  case PHOSH_POMODORO_STATE_OFF:
    new_state = PHOSH_POMODORO_STATE_ACTIVE;
    break;
  case PHOSH_POMODORO_STATE_ACTIVE:
  case PHOSH_POMODORO_STATE_BREAK:
  default:
    new_state = PHOSH_POMODORO_STATE_OFF;
    break;
  }

  phosh_pomodoro_quick_setting_set_state (self, new_state);
}


static gboolean
transform_to_active (GBinding     *binding,
                     const GValue *from_value,
                     GValue       *to_value,
                     gpointer      user_data)
{
  PhoshPomodoroState state = g_value_get_enum (from_value);
  gboolean active = FALSE;

  switch (state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
  case PHOSH_POMODORO_STATE_BREAK:
    active = TRUE;
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    break;
  }

  g_value_set_boolean (to_value, active);
  return TRUE;
}


static gboolean
transform_to_icon_name (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  PhoshPomodoroState state = g_value_get_enum (from_value);
  const char *icon_name;

  switch (state) {
  case PHOSH_POMODORO_STATE_ACTIVE:
    icon_name = ACTIVE_ICON;
    break;
  case PHOSH_POMODORO_STATE_BREAK:
    icon_name = BREAK_ICON;
    break;
  case PHOSH_POMODORO_STATE_OFF:
  default:
    icon_name = "pomodoro-off-symbolic";
    break;
  }

  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static void
phosh_pomodoro_quick_setting_finalize (GObject *gobject)
{
  PhoshPomodoroQuickSetting *self = PHOSH_POMODORO_QUICK_SETTING (gobject);

  phosh_pomodoro_quick_setting_clear_timers (self);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_pomodoro_quick_setting_parent_class)->finalize (gobject);
}


static void
phosh_pomodoro_quick_setting_class_init (PhoshPomodoroQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = phosh_pomodoro_quick_setting_finalize;
  object_class->set_property = phosh_pomodoro_quick_setting_set_property;
  object_class->get_property = phosh_pomodoro_quick_setting_get_property;

  props[PROP_STATE] =
    g_param_spec_enum ("state", "", "",
                       PHOSH_TYPE_POMODORO_STATE,
                       PHOSH_POMODORO_STATE_OFF,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/pomodoro-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshPomodoroQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_pomodoro_quick_setting_init (PhoshPomodoroQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/pomodoro-quick-setting/icons");

  self->settings = g_settings_new ("mobi.phosh.plugins.pomodoro");

  g_object_bind_property_full (self, "state",
                               self, "active",
                               G_BINDING_SYNC_CREATE,
                               transform_to_active,
                               NULL, NULL, NULL);

  g_object_bind_property_full (self, "state",
                               self->info, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_to_icon_name,
                               NULL, NULL, NULL);

  g_signal_connect_object (phosh_shell_get_default (),
                           "notify::locked",
                           G_CALLBACK (on_shell_locked),
                           self,
                           G_CONNECT_SWAPPED);
  if (g_settings_get_boolean (self->settings, START_ON_UNLOCK_KEY) && !_started_on_login) {
    phosh_pomodoro_quick_setting_set_state (self, PHOSH_POMODORO_STATE_ACTIVE);
    _started_on_login = TRUE;
  }

  update_label (self);
}
