/*
 * Copyright (C) 2023 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "gvc-mixer-control.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_AUDIO_DEVICES (phosh_audio_devices_get_type ())

G_DECLARE_FINAL_TYPE (PhoshAudioDevices, phosh_audio_devices, PHOSH, AUDIO_DEVICES, GObject)

PhoshAudioDevices         *phosh_audio_devices_new       (gpointer  mixer_control,
                                                          gboolean  is_input);

G_END_DECLS
