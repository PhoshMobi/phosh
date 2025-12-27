/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_AUDIO_DEVICE (phosh_audio_device_get_type ())

G_DECLARE_FINAL_TYPE (PhoshAudioDevice, phosh_audio_device, PHOSH, AUDIO_DEVICE, GObject)

PhoshAudioDevice        *phosh_audio_device_new             (guint             id,
                                                             const char       *icon_name,
                                                             const char       *description);
guint                    phosh_audio_device_get_id          (PhoshAudioDevice *self);
const char              *phosh_audio_device_get_description (PhoshAudioDevice *self);
void                     phosh_audio_device_set_active      (PhoshAudioDevice *self,
                                                             gboolean          active);

G_END_DECLS
