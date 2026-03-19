/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-settings"

#include <glib/gi18n.h>

#include "phosh-config.h"

#include "brightness-settings.h"
#include "media-player.h"
#include "shell-priv.h"
#include "settings.h"
#include "quick-settings.h"
#include "settings/audio-settings.h"
#include "torch-manager.h"
#include "notifications/notify-manager.h"
#include "notifications/notification-frame.h"

#include <gio/gdesktopappinfo.h>
#include <xkbcommon/xkbcommon.h>

#define STACK_CHILD_NOTIFICATIONS    "notifications"
#define STACK_CHILD_NO_NOTIFICATIONS "no-notifications"

/**
 * PhoshSettings:
 *
 * The settings menu
 */
enum {
  PROP_0,
  PROP_ON_LOCKSCREEN,
  PROP_DRAG_HANDLE_OFFSET,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  SETTING_DONE,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

typedef struct _PhoshSettings {
  GtkBin     parent;

  gboolean   on_lockscreen;
  gint       drag_handle_offset;
  guint      debounce_handle;

  GtkWidget *scrolled_window;
  GtkWidget *box_sliders;
  GtkWidget *box_settings;
  GtkWidget *quick_settings;
  GtkWidget *media_player;
  PhoshAudioSettings *audio_settings;
  PhoshBrightnessSettings *brightness_settings;

  /* The area with media widget, notifications */
  GtkWidget *box_bottom_half;
  /* Notifications */
  GtkWidget *list_notifications;
  GtkWidget *stack_notifications;

  /* Torch */
  PhoshTorchManager *torch_manager;
  GtkWidget *revealer;
  GtkWidget *scale_torch;
  gboolean   setting_torch;
} PhoshSettings;


G_DEFINE_TYPE (PhoshSettings, phosh_settings, GTK_TYPE_BIN)


static void
set_on_lockscreen (PhoshSettings *self, gboolean on_lockscreen)
{
  if (self->on_lockscreen == on_lockscreen)
    return;

  self->on_lockscreen = on_lockscreen;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ON_LOCKSCREEN]);
}


static void
phosh_settings_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PhoshSettings *self = PHOSH_SETTINGS (object);

  switch (property_id) {
  case PROP_ON_LOCKSCREEN:
    set_on_lockscreen (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_settings_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhoshSettings *self = PHOSH_SETTINGS (object);

  switch (property_id) {
  case PROP_ON_LOCKSCREEN:
    g_value_set_boolean (value, self->on_lockscreen);
    break;
  case PROP_DRAG_HANDLE_OFFSET:
    g_value_set_int (value, self->drag_handle_offset);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
calc_drag_handle_offset (PhoshSettings *self)
{
  int h = 0;
  int box_height, sw_height, qs_height = 0;
  int qs_y = 0;
  gboolean success;

  h = gtk_widget_get_allocated_height (GTK_WIDGET (self));
  /* On the lock screen the whole surface is fine */
  if (self->on_lockscreen)
    goto out;

  box_height = gtk_widget_get_allocated_height (self->box_settings);
  sw_height = gtk_widget_get_allocated_height (self->scrolled_window);
  if (box_height > sw_height) {
    h = 0; /* Don't enlarge drag handle if box needs scrolling */
    goto out;
  }

  g_debug ("Calculating drag offset: on quick-settings");

  success = gtk_widget_translate_coordinates (self->quick_settings, GTK_WIDGET (self),
                                              0, 0, NULL, &qs_y);

  if (!success) {
    g_warning ("Calculating drag offset: Unable to get quick-setting's y coordinate");
    goto out;
  }

  qs_height = gtk_widget_get_allocated_height (self->quick_settings);
  h = qs_y + qs_height;

  g_debug ("Calculating drag offset: QS_y = %d, QS_height = %d, height = %d",
           qs_y, qs_height, h);

 out:
  if (self->drag_handle_offset == h)
    return;

  self->drag_handle_offset = h;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DRAG_HANDLE_OFFSET]);
}


static void
on_size_allocate (PhoshSettings *self)
{
  calc_drag_handle_offset (self);

  return;
}


static void
delayed_update_drag_handle_offset (gpointer data)
{
  PhoshSettings *self = PHOSH_SETTINGS (data);

  self->debounce_handle = 0;
  calc_drag_handle_offset (self);
}


static void
update_drag_handle_offset (PhoshSettings *self)
{
  g_clear_handle_id (&self->debounce_handle, g_source_remove);
  self->debounce_handle = g_timeout_add_once (200, delayed_update_drag_handle_offset, self);
  g_source_set_name_by_id (self->debounce_handle, "[phosh] delayed_update_drag_handle_offset");
}


static void
on_is_headphone_changed (PhoshSettings      *self,
                         GParamSpec         *pspec,
                         PhoshAudioSettings *audio_settings)
{
  PhoshMediaPlayer *media_player;

  g_return_if_fail (PHOSH_IS_SETTINGS (self));
  g_return_if_fail (PHOSH_IS_AUDIO_SETTINGS (audio_settings));
  media_player = PHOSH_MEDIA_PLAYER (self->media_player);

  if (phosh_audio_settings_get_output_is_headphone (self->audio_settings) ||
      !phosh_media_player_get_is_playable (media_player) ||
      phosh_media_player_get_status (media_player) != PHOSH_MEDIA_PLAYER_STATUS_PLAYING)
    return;

  phosh_media_player_toggle_play_pause (media_player);
}


static void
on_media_player_raised (PhoshSettings *self,
                        gpointer       unused)
{
  g_return_if_fail (PHOSH_IS_SETTINGS (self));
  g_signal_emit (self, signals[SETTING_DONE], 0);
}


static void
on_notifications_clear_all_clicked (PhoshSettings *self)
{
  PhoshNotifyManager *manager;

  manager = phosh_notify_manager_get_default ();
  phosh_notify_manager_close_all_notifications (manager, PHOSH_NOTIFICATION_REASON_DISMISSED);
}


static GtkWidget *
create_notification_row (gpointer item, gpointer data)
{
  PhoshShell *shell = phosh_shell_get_default ();
  GtkWidget *row = NULL;
  GtkWidget *frame = NULL;

  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "activatable", FALSE,
                      "visible", TRUE,
                      NULL);

  frame = phosh_notification_frame_new (TRUE, NULL);
  phosh_notification_frame_bind_model (PHOSH_NOTIFICATION_FRAME (frame), item);

  if (!(phosh_shell_get_state (shell) & PHOSH_STATE_SETTINGS))
    phosh_notification_frame_set_animate_show (PHOSH_NOTIFICATION_FRAME (frame), FALSE);
  gtk_widget_set_visible (frame, TRUE);

  gtk_container_add (GTK_CONTAINER (row), frame);

  return row;
}


static void
on_torch_scale_value_changed (PhoshSettings *self, GtkScale *scale_torch)
{
  double value;

  g_return_if_fail (PHOSH_IS_SETTINGS (self));
  g_return_if_fail (PHOSH_IS_TORCH_MANAGER (self->torch_manager));

  /* Only react to scale changes when torch is enabled */
  if (!phosh_torch_manager_get_enabled (self->torch_manager))
    return;

  self->setting_torch = TRUE;
  value = gtk_range_get_value (GTK_RANGE (self->scale_torch));
  g_debug ("Setting torch brightness to %.2f", value);
  phosh_torch_manager_set_scaled_brightness (self->torch_manager, value / 100.0);
}


static void
on_torch_brightness_changed (PhoshSettings *self, GParamSpec *pspec, PhoshTorchManager *manager)
{
  g_return_if_fail (PHOSH_IS_SETTINGS (self));
  g_return_if_fail (PHOSH_IS_TORCH_MANAGER (manager));

  if (self->setting_torch) {
    self->setting_torch = FALSE;
    return;
  }

  gtk_range_set_value (GTK_RANGE (self->scale_torch),
                       100.0 * phosh_torch_manager_get_scaled_brightness (self->torch_manager));
}


static void
on_notification_frames_items_changed (PhoshSettings *self,
                                      guint          position,
                                      guint          removed,
                                      guint          added,
                                      GListModel    *list)
{
  gboolean is_empty;
  const char *child_name;

  g_return_if_fail (PHOSH_IS_SETTINGS (self));
  g_return_if_fail (G_IS_LIST_MODEL (list));

  is_empty = !g_list_model_get_n_items (list);
  g_debug ("Notification list empty: %d", is_empty);

  child_name = is_empty ? STACK_CHILD_NO_NOTIFICATIONS : STACK_CHILD_NOTIFICATIONS;
  gtk_stack_set_visible_child_name (GTK_STACK (self->stack_notifications), child_name);
  update_drag_handle_offset (self);
}


static void
setup_torch (PhoshSettings *self)
{
  PhoshShell *shell = phosh_shell_get_default ();

  self->torch_manager = g_object_ref (phosh_shell_get_torch_manager (shell));

  g_object_bind_property (self->torch_manager, "enabled",
                          self->revealer, "reveal-child",
                          G_BINDING_SYNC_CREATE);
  gtk_range_set_range (GTK_RANGE (self->scale_torch), 0, 100);
  gtk_range_set_value (GTK_RANGE (self->scale_torch),
                       phosh_torch_manager_get_scaled_brightness (self->torch_manager) * 100.0);
  g_signal_connect_object (self->torch_manager,
                           "notify::brightness",
                           G_CALLBACK (on_torch_brightness_changed),
                           self,
                           G_CONNECT_SWAPPED);
  g_object_bind_property (self->torch_manager, "can-scale",
                          self->revealer, "visible",
                          G_BINDING_SYNC_CREATE);
}


static void
phosh_settings_constructed (GObject *object)
{
  PhoshSettings *self = PHOSH_SETTINGS (object);
  PhoshNotifyManager *manager;

  G_OBJECT_CLASS (phosh_settings_parent_class)->constructed (object);

  setup_torch (self);

  manager = phosh_notify_manager_get_default ();
  gtk_list_box_bind_model (GTK_LIST_BOX (self->list_notifications),
                           G_LIST_MODEL (phosh_notify_manager_get_list (manager)),
                           create_notification_row,
                           NULL,
                           NULL);
  g_signal_connect_object (phosh_notify_manager_get_list (manager),
                           "items-changed",
                           G_CALLBACK (on_notification_frames_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_notification_frames_items_changed (self, -1, -1, -1,
                                        G_LIST_MODEL (phosh_notify_manager_get_list (manager)));

  g_object_bind_property (phosh_shell_get_default (),
                          "locked",
                          self,
                          "on-lockscreen",
                          G_BINDING_SYNC_CREATE);
}


static void
phosh_settings_dispose (GObject *object)
{
  PhoshSettings *self = PHOSH_SETTINGS (object);

  g_clear_object (&self->torch_manager);

  G_OBJECT_CLASS (phosh_settings_parent_class)->dispose (object);
}


static void
phosh_settings_finalize (GObject *object)
{
  PhoshSettings *self = PHOSH_SETTINGS (object);

  g_clear_handle_id (&self->debounce_handle, g_source_remove);

  G_OBJECT_CLASS (phosh_settings_parent_class)->finalize (object);
}


static void
phosh_settings_class_init (PhoshSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_settings_dispose;
  object_class->finalize = phosh_settings_finalize;
  object_class->constructed = phosh_settings_constructed;
  object_class->set_property = phosh_settings_set_property;
  object_class->get_property = phosh_settings_get_property;

  /**
   * PhoshSettings:on-lockscreen:
   *
   * Whether settings are shown on lockscreen (%TRUE) or in the unlocked shell
   * (%FALSE).
   *
   * Consider this property to be read only. It's only r/w so we can
   * use a property binding with the [type@Shell]s "locked" property.
   */
  props[PROP_ON_LOCKSCREEN] =
    g_param_spec_boolean ("on-lockscreen", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshSettings:handle-offset:
   *
   * The offset from the bottom of the widget where it's safe to start
   * dragging. See phosh_settings_get_drag_drag_handle_offset().
   */
  props[PROP_DRAG_HANDLE_OFFSET] =
    g_param_spec_int ("drag-handle-offset", "", "",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  g_type_ensure (PHOSH_TYPE_AUDIO_SETTINGS);
  g_type_ensure (PHOSH_TYPE_BRIGHTNESS_SETTINGS);
  g_type_ensure (PHOSH_TYPE_QUICK_SETTINGS);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/settings.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, audio_settings);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, brightness_settings);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, box_bottom_half);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, box_sliders);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, box_settings);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, list_notifications);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, media_player);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, quick_settings);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, revealer);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, scale_torch);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, stack_notifications);
  gtk_widget_class_bind_template_child (widget_class, PhoshSettings, scrolled_window);

  gtk_widget_class_bind_template_callback (widget_class, on_media_player_raised);
  gtk_widget_class_bind_template_callback (widget_class, on_is_headphone_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_notifications_clear_all_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_torch_scale_value_changed);
  gtk_widget_class_bind_template_callback (widget_class, update_drag_handle_offset);
}


static void
phosh_settings_init (PhoshSettings *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect (self, "size-allocate", G_CALLBACK (on_size_allocate), NULL);
}


GtkWidget *
phosh_settings_new (void)
{
  return g_object_new (PHOSH_TYPE_SETTINGS, NULL);
}

/**
 * phosh_settings_get_drag_handle_offset:
 * @self: The settings
 *
 * Get the y coordinate from the top of the widget where dragging
 * can start. E.g. we don't want drag to work on notifications as
 * notifications need to scroll in vertical direction.
 *
 * Returns: The y coordinate at which dragging the surface can start.
 */
gint
phosh_settings_get_drag_handle_offset (PhoshSettings *self)
{
  g_return_val_if_fail (PHOSH_IS_SETTINGS (self), 0);

  return self->drag_handle_offset;
}


void
phosh_settings_hide_details (PhoshSettings *self)
{
  g_return_if_fail (PHOSH_IS_SETTINGS (self));

  phosh_audio_settings_hide_details (self->audio_settings);
  phosh_brightness_settings_hide_details (self->brightness_settings);
  phosh_quick_settings_hide_status (PHOSH_QUICK_SETTINGS (self->quick_settings));
}
