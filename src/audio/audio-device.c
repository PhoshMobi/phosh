/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-audio-device"

#include "phosh-config.h"

#include "audio-device.h"

/**
 * PhoshAudioDevice:
 *
 * Audio device information stored in [class@AudioDevices].
 */

enum {
  PROP_0,
  PROP_ID,
  PROP_ICON_NAME,
  PROP_DESCRIPTION,
  PROP_ACTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshAudioDevice {
  GObject  parent;

  guint    id;
  char    *icon_name;
  char    *description;
  gboolean active;
};
G_DEFINE_TYPE (PhoshAudioDevice, phosh_audio_device, G_TYPE_OBJECT)


static void
phosh_audio_device_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshAudioDevice *self = PHOSH_AUDIO_DEVICE (object);

  switch (property_id) {
  case PROP_ID:
    self->id = g_value_get_uint (value);
    break;
  case PROP_ICON_NAME:
    self->icon_name = g_value_dup_string (value);
    break;
  case PROP_DESCRIPTION:
    self->description = g_value_dup_string (value);
    break;
  case PROP_ACTIVE:
    phosh_audio_device_set_active (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_audio_device_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshAudioDevice *self = PHOSH_AUDIO_DEVICE (object);

  switch (property_id) {
  case PROP_ID:
    g_value_set_uint (value, self->id);
    break;
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  case PROP_DESCRIPTION:
    g_value_set_string (value, self->description);
    break;
  case PROP_ACTIVE:
    g_value_set_boolean (value, self->active);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_audio_device_finalize (GObject *object)
{
  PhoshAudioDevice *self = PHOSH_AUDIO_DEVICE (object);

  g_clear_pointer (&self->icon_name, g_free);
  g_clear_pointer (&self->description, g_free);

  G_OBJECT_CLASS (phosh_audio_device_parent_class)->finalize (object);
}


static void
phosh_audio_device_class_init (PhoshAudioDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_audio_device_get_property;
  object_class->set_property = phosh_audio_device_set_property;
  object_class->finalize = phosh_audio_device_finalize;

  props[PROP_ID] =
    g_param_spec_uint ("id", "", "",
                       0, G_MAXUINT, G_MAXUINT,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_DESCRIPTION] =
    g_param_spec_string ("description", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_EXPLICIT_NOTIFY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_audio_device_init (PhoshAudioDevice *self)
{
}


PhoshAudioDevice *
phosh_audio_device_new (guint id, const char *icon_name, const char *description)
{
  return g_object_new (PHOSH_TYPE_AUDIO_DEVICE,
                       "id", id,
                       "icon-name", icon_name,
                       "description", description,
                       NULL);
}


const char *
phosh_audio_device_get_description (PhoshAudioDevice *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_DEVICE (self), NULL);

  return self->description;
}


guint
phosh_audio_device_get_id (PhoshAudioDevice *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_DEVICE (self), 0);

  return self->id;
}

void
phosh_audio_device_set_active (PhoshAudioDevice *self, gboolean active)
{
  g_return_if_fail (PHOSH_IS_AUDIO_DEVICE (self));

  if (self->active == active)
    return;

  self->active = active;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
}
