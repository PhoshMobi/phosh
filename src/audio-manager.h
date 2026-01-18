/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "audio/audio-devices.h"

#include "gvc-mixer-control.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_AUDIO_MANAGER (phosh_audio_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshAudioManager, phosh_audio_manager, PHOSH, AUDIO_MANAGER, GObject)

PhoshAudioManager *phosh_audio_manager_get_default (void);
PhoshAudioDevices *phosh_audio_manager_get_input_devices (PhoshAudioManager *self);
PhoshAudioDevices *phosh_audio_manager_get_output_devices (PhoshAudioManager *self);
GvcMixerControl *  phosh_audio_manager_get_mixer_control (PhoshAudioManager *self);
GvcMixerStream *   phosh_audio_manager_get_default_sink (PhoshAudioManager *self);
void               phosh_audio_manager_change_input (PhoshAudioManager *self, guint id);
void               phosh_audio_manager_change_output (PhoshAudioManager *self, guint id);

G_END_DECLS
