/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "audio/audio-device.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_AUDIO_DEVICE_ROW (phosh_audio_device_row_get_type ())

G_DECLARE_FINAL_TYPE (PhoshAudioDeviceRow, phosh_audio_device_row, PHOSH, AUDIO_DEVICE_ROW, GtkListBoxRow)

PhoshAudioDeviceRow      *phosh_audio_device_row_new                   (PhoshAudioDevice    *audio_device);
PhoshAudioDevice         *phosh_audio_device_row_get_audio_device      (PhoshAudioDeviceRow *self);

G_END_DECLS
