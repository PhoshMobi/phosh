/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-audio-manager"

#include "phosh-config.h"

#include "audio-manager.h"
#include "audio/audio-devices.h"

#include "gvc-mixer-control.h"

#include <glib/gi18n.h>

/**
 * PhoshAudioManager:
 *
 * Manage audio related properties
 */

struct _PhoshAudioManager {
  GObject            parent;

  GvcMixerControl   *mixer_control;

  PhoshAudioDevices *input_devices;
  PhoshAudioDevices *output_devices;
};

G_DEFINE_TYPE (PhoshAudioManager, phosh_audio_manager, G_TYPE_OBJECT)


static void
phosh_audio_manager_dispose (GObject *object)
{
  PhoshAudioManager *self = PHOSH_AUDIO_MANAGER (object);

  g_clear_object (&self->output_devices);
  g_clear_object (&self->input_devices);

  G_OBJECT_CLASS (phosh_audio_manager_parent_class)->dispose (object);
}


static void
phosh_audio_manager_finalize (GObject *object)
{
  PhoshAudioManager *self = PHOSH_AUDIO_MANAGER (object);

  g_clear_object (&self->mixer_control);

  G_OBJECT_CLASS (phosh_audio_manager_parent_class)->finalize (object);
}


static void
phosh_audio_manager_class_init (PhoshAudioManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_audio_manager_dispose;
  object_class->finalize = phosh_audio_manager_finalize;
}


static void
phosh_audio_manager_init (PhoshAudioManager *self)
{
  self->mixer_control = gvc_mixer_control_new (_("Phone Shell Volume Control"));
  g_return_if_fail (self->mixer_control);
  gvc_mixer_control_open (self->mixer_control);

  self->output_devices = phosh_audio_devices_new (self->mixer_control, FALSE);
  self->input_devices = phosh_audio_devices_new (self->mixer_control, TRUE);
}


PhoshAudioManager *
phosh_audio_manager_get_default (void)
{
  static PhoshAudioManager *instance;

  if (instance == NULL) {
    instance = g_object_new (PHOSH_TYPE_AUDIO_MANAGER, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }

  return instance;
}


PhoshAudioDevices *
phosh_audio_manager_get_input_devices (PhoshAudioManager *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_MANAGER (self), NULL);

  return self->input_devices;
}


PhoshAudioDevices *
phosh_audio_manager_get_output_devices (PhoshAudioManager *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_MANAGER (self), NULL);

  return self->output_devices;
}


GvcMixerControl *
phosh_audio_manager_get_mixer_control (PhoshAudioManager *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_MANAGER (self), NULL);

  return self->mixer_control;
}


GvcMixerStream *
phosh_audio_manager_get_default_sink (PhoshAudioManager *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_MANAGER (self), NULL);

  return gvc_mixer_control_get_default_sink (self->mixer_control);
}


void
phosh_audio_manager_change_input (PhoshAudioManager *self, guint id)
{
  GvcMixerUIDevice *device;

  g_return_if_fail (PHOSH_IS_AUDIO_MANAGER (self));

  device = gvc_mixer_control_lookup_input_id (self->mixer_control, id);
  g_return_if_fail (device);

  gvc_mixer_control_change_input (self->mixer_control, device);
}


void
phosh_audio_manager_change_output (PhoshAudioManager *self, guint id)
{
  GvcMixerUIDevice *device;

  g_return_if_fail (PHOSH_IS_AUDIO_MANAGER (self));

  device = gvc_mixer_control_lookup_output_id (self->mixer_control, id);
  g_return_if_fail (device);

  gvc_mixer_control_change_output (self->mixer_control, device);
}
