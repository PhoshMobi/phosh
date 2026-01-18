/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-audio-settings"

#include "phosh-config.h"

#include <pulse/pulseaudio.h>

#include "audio/audio-device.h"
#include "audio-manager.h"
#include "fading-label.h"
#include "settings/audio-device-row.h"
#include "settings/audio-settings.h"
#include "settings/channel-bar.h"
#include "util.h"

#include "gvc-mixer-stream.h"

#include <gmobile.h>

#include <glib/gi18n.h>

#include <math.h>

/**
 * PhoshAudioSettings:
 *
 * Widget to control Audio device selection and volume.
 */

enum {
  PROP_0,
  PROP_IS_HEADPHONE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshAudioSettings {
  GtkBin             parent;

  /* Volume slider */
  GvcMixerStream    *output_stream;
  gboolean           allow_volume_above_100_percent;
  gboolean           setting_volume;
  gboolean           is_headphone;
  PhoshChannelBar   *output_vol_bar;

  /* Device select */
  GtkWidget         *stack_audio_details;
  GtkWidget         *toggle_audio_details;
  GtkWidget         *box_audio_input_devices;
  GtkWidget         *box_audio_output_devices;
  GtkWidget         *listbox_audio_input_devices;
  GtkWidget         *listbox_audio_output_devices;

  PhoshAudioManager *audio_manager;
};
G_DEFINE_TYPE (PhoshAudioSettings, phosh_audio_settings, GTK_TYPE_BIN)


static void
update_output_vol_bar (PhoshAudioSettings *self)
{
  GtkAdjustment *adj;

  self->setting_volume = TRUE;
  phosh_channel_bar_set_base_volume (self->output_vol_bar,
                                     gvc_mixer_stream_get_base_volume (self->output_stream));
  phosh_channel_bar_set_is_amplified (self->output_vol_bar,
                                      self->allow_volume_above_100_percent &&
                                      gvc_mixer_stream_get_can_decibel (self->output_stream));
  adj = phosh_channel_bar_get_adjustment (self->output_vol_bar);
  g_debug ("Adjusting volume to %d", gvc_mixer_stream_get_volume (self->output_stream));
  gtk_adjustment_set_value (adj, gvc_mixer_stream_get_volume (self->output_stream));
  self->setting_volume = FALSE;
}


static void
output_stream_notify_is_muted_cb (GvcMixerStream *stream, GParamSpec *pspec, gpointer data)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (data);
  gboolean muted;

  muted = gvc_mixer_stream_get_is_muted (stream);
  if (!self->setting_volume) {
    phosh_channel_bar_set_is_muted (self->output_vol_bar, muted);
    if (!muted)
      update_output_vol_bar (self);
  }
}


static void
output_stream_notify_volume_cb (GvcMixerStream *stream, GParamSpec *pspec, gpointer data)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (data);

  if (!self->setting_volume)
    update_output_vol_bar (self);
}


static gboolean
stream_uses_headphones (GvcMixerStream *stream)
{
  const char *form_factor;
  const GvcMixerStreamPort *port;

  form_factor = gvc_mixer_stream_get_form_factor (stream);
  if (g_strcmp0 (form_factor, "headset") == 0 ||
      g_strcmp0 (form_factor, "headphone") == 0) {
    return TRUE;
  }

  port = gvc_mixer_stream_get_port (stream);
  if (!port)
    return FALSE;

  if (g_strcmp0 (port->port, "[Out] Headphones") == 0 ||
      g_strcmp0 (port->port, "analog-output-headphones") == 0) {
    return TRUE;
  }

  return FALSE;
}


static void
on_output_stream_port_changed (GvcMixerStream *stream, GParamSpec *pspec, gpointer data)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (data);
  const char *icon = NULL;
  gboolean is_headphone = FALSE;
  const GvcMixerStreamPort *port;
  GvcMixerControl *mixer_control;

  mixer_control = phosh_audio_manager_get_mixer_control (self->audio_manager);
  port = gvc_mixer_stream_get_port (stream);
  if (port)
    g_debug ("Port changed: %s (%s)", port->human_port ?: port->port, port->port);

  is_headphone = stream_uses_headphones (stream);
  if (is_headphone) {
    icon = "audio-headphones";
  } else {
    GvcMixerUIDevice *output;

    output = gvc_mixer_control_lookup_device_from_stream (mixer_control, stream);
    if (output)
      icon = gvc_mixer_ui_device_get_icon_name (output);
  }

  if (gm_str_is_null_or_empty (icon) || g_str_has_prefix (icon, "audio-card"))
    icon = "audio-speakers";

  phosh_channel_bar_set_icon_name (self->output_vol_bar, icon);

  if (is_headphone == self->is_headphone)
    return;
  self->is_headphone = is_headphone;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_HEADPHONE]);
}


static void
mixer_control_output_update_cb (GvcMixerControl *mixer, guint id, gpointer data)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (data);

  g_debug ("Audio output updated: %d", id);

  g_return_if_fail (PHOSH_IS_AUDIO_SETTINGS (self));

  if (self->output_stream)
    g_signal_handlers_disconnect_by_data (self->output_stream, self);

  g_set_object (&self->output_stream, phosh_audio_manager_get_default_sink (self->audio_manager));
  g_return_if_fail (self->output_stream);

  g_signal_connect_object (self->output_stream,
                           "notify::volume",
                           G_CALLBACK (output_stream_notify_volume_cb),
                           self, 0);

  g_signal_connect_object (self->output_stream,
                           "notify::is-muted",
                           G_CALLBACK (output_stream_notify_is_muted_cb),
                           self, 0);

  g_signal_connect_object (self->output_stream,
                           "notify::port",
                           G_CALLBACK (on_output_stream_port_changed),
                           self, 0);
  on_output_stream_port_changed (self->output_stream, NULL, self);

  update_output_vol_bar (self);
}


static void
vol_bar_value_changed_cb (PhoshChannelBar *bar, PhoshAudioSettings *self)
{
  double volume, rounded;
  g_autofree char *name = NULL;

  if (!self->output_stream)
    self->output_stream = g_object_ref (phosh_audio_manager_get_default_sink (self->audio_manager));

  volume = phosh_channel_bar_get_volume (bar);
  rounded = round (volume);

  g_object_get (self->output_vol_bar, "name", &name, NULL);
  g_debug ("Setting stream volume %lf (rounded: %lf) for bar '%s'", volume, rounded, name);

  g_return_if_fail (self->output_stream);
  if (gvc_mixer_stream_set_volume (self->output_stream, (pa_volume_t) rounded) != FALSE)
    gvc_mixer_stream_push_volume (self->output_stream);

  gvc_mixer_stream_change_is_muted (self->output_stream, (int) rounded == 0);
}


static void
on_audio_input_device_row_activated (PhoshAudioSettings  *self,
                                     PhoshAudioDeviceRow *row,
                                     GtkListBox          *list)
{
  PhoshAudioDevice *audio_device = phosh_audio_device_row_get_audio_device (row);
  guint id;

  g_return_if_fail (PHOSH_IS_AUDIO_DEVICE (audio_device));
  id = phosh_audio_device_get_id (audio_device);

  phosh_audio_manager_change_input (self->audio_manager, id);
}


static void
on_audio_output_device_row_activated (PhoshAudioSettings  *self,
                                      PhoshAudioDeviceRow *row,
                                      GtkListBox          *list)
{
  PhoshAudioDevice *audio_device = phosh_audio_device_row_get_audio_device (row);
  guint id;

  g_return_if_fail (PHOSH_IS_AUDIO_DEVICE (audio_device));
  id = phosh_audio_device_get_id (audio_device);

  phosh_audio_manager_change_output (self->audio_manager, id);
}


static GtkWidget *
create_audio_device_row (gpointer item, gpointer user_data)
{
  PhoshAudioDevice *audio_device = PHOSH_AUDIO_DEVICE (item);

  return GTK_WIDGET (phosh_audio_device_row_new (audio_device));
}


static void
phosh_audio_settings_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (object);

  switch (property_id) {
  case PROP_IS_HEADPHONE:
    g_value_set_boolean (value, phosh_audio_settings_get_output_is_headphone (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_audio_settings_dispose (GObject *object)
{
  PhoshAudioSettings *self = PHOSH_AUDIO_SETTINGS (object);

  g_clear_object (&self->output_stream);

  G_OBJECT_CLASS (phosh_audio_settings_parent_class)->dispose (object);
}


static void
phosh_audio_settings_class_init (PhoshAudioSettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_audio_settings_get_property;
  object_class->dispose = phosh_audio_settings_dispose;

  /**
   * PhoshAudioSettings:is-headphone:
   *
   * Whether the current output is a headphone
   */
  props[PROP_IS_HEADPHONE] =
    g_param_spec_boolean ("is-headphone", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  g_type_ensure (PHOSH_TYPE_CHANNEL_BAR);
  g_type_ensure (PHOSH_TYPE_FADING_LABEL);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/audio-settings.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, box_audio_input_devices);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, box_audio_output_devices);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, listbox_audio_input_devices);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, listbox_audio_output_devices);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, output_vol_bar);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, stack_audio_details);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioSettings, toggle_audio_details);

  gtk_widget_class_bind_template_callback (widget_class, on_audio_input_device_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_audio_output_device_row_activated);

  gtk_widget_class_set_css_name (widget_class, "phosh-audio-settings");
}


static gboolean
transform_toggle_stack_child_name (GBinding     *binding,
                                   const GValue *from_value,
                                   GValue       *to_value,
                                   gpointer      user_data)
{
  gboolean active = g_value_get_boolean (from_value);

  g_value_set_string (to_value, active ? "audio-details" : "no-audio-details");

  return TRUE;
}


static void
phosh_audio_settings_init (PhoshAudioSettings *self)
{
  PhoshAudioDevices *input_devices, *output_devices;
  GvcMixerControl *mixer_control;

  self->audio_manager = g_object_ref (phosh_audio_manager_get_default ());

  gtk_widget_init_template (GTK_WIDGET (self));

  mixer_control = phosh_audio_manager_get_mixer_control (self->audio_manager);
  if (mixer_control)
    g_return_if_fail (mixer_control);

  /* Volume slider */
  g_signal_connect_object (mixer_control,
                           "active-output-update",
                           G_CALLBACK (mixer_control_output_update_cb),
                           self,
                           G_CONNECT_DEFAULT);
  g_signal_connect (self->output_vol_bar,
                    "value-changed",
                    G_CALLBACK (vol_bar_value_changed_cb),
                    self);

  /* Toggle details button */
  g_object_bind_property_full (self->toggle_audio_details, "active",
                               self->stack_audio_details, "visible-child-name",
                               G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                               transform_toggle_stack_child_name,
                               NULL, NULL, NULL);

  /* Audio device selection */
  output_devices = phosh_audio_manager_get_output_devices (self->audio_manager);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->listbox_audio_output_devices),
                           G_LIST_MODEL (output_devices),
                           create_audio_device_row,
                           self,
                           NULL);
  g_object_bind_property (output_devices, "has-devices",
                          self->box_audio_output_devices, "visible",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  input_devices = phosh_audio_manager_get_input_devices (self->audio_manager);
  gtk_list_box_bind_model (GTK_LIST_BOX (self->listbox_audio_input_devices),
                           G_LIST_MODEL (input_devices),
                           create_audio_device_row,
                           self,
                           NULL);
  g_object_bind_property (input_devices, "has-devices",
                          self->box_audio_input_devices, "visible",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
}


PhoshAudioSettings *
phosh_audio_settings_new (void)
{
  return g_object_new (PHOSH_TYPE_AUDIO_SETTINGS, NULL);
}


gboolean
phosh_audio_settings_get_output_is_headphone (PhoshAudioSettings *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_SETTINGS (self), FALSE);

  return self->is_headphone;
}

/**
 * phosh_audio_settings_hide_details:
 * @self: The audio settings widget
 *
 * Hides the audio settings details
 */
void
phosh_audio_settings_hide_details (PhoshAudioSettings *self)
{
  g_return_if_fail (PHOSH_IS_AUDIO_SETTINGS (self));

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->toggle_audio_details), FALSE);
}
