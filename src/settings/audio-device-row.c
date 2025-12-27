/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-audio_device-row"

#include "phosh-config.h"

#include "audio/audio-device.h"
#include "audio-device-row.h"

/**
 * PhoshAudioDeviceRow:
 *
 * A widget intended to be stored in a `GtkListBox` to represent and audio device.
 */

enum {
  PROP_0,
  PROP_AUDIO_DEVICE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshAudioDeviceRow {
  GtkListBoxRow        parent;

  PhoshAudioDevice    *audio_device;

  GtkWidget           *icon;
  GtkWidget           *description;
  GtkWidget           *revealer;
};
G_DEFINE_TYPE (PhoshAudioDeviceRow, phosh_audio_device_row, GTK_TYPE_LIST_BOX_ROW)


static gboolean
transform_icon_name_to_icon (GBinding     *binding,
                             const GValue *from_value,
                             GValue       *to_value,
                             gpointer      unused)
{
  const char *icon_name = g_value_get_string (from_value);
  GIcon *icon = NULL;

  if (icon_name == NULL)
    icon_name = "audio-speakers-symbolic";

  icon = g_themed_icon_new_with_default_fallbacks (icon_name);

  g_value_take_object (to_value, icon);
  return TRUE;
}


static void
set_audio_device (PhoshAudioDeviceRow *self, PhoshAudioDevice *device)
{
  g_set_object (&self->audio_device, device);

  g_object_bind_property (device, "description",
                          self->description, "label",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (device, "icon-name",
                               self->icon, "gicon",
                               G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE,
                               transform_icon_name_to_icon,
                               NULL, NULL, NULL);

  g_object_bind_property (device, "active",
                          self->revealer, "reveal-child",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);
}


static void
phosh_audio_device_row_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  PhoshAudioDeviceRow *self = PHOSH_AUDIO_DEVICE_ROW (object);

  switch (property_id) {
  case PROP_AUDIO_DEVICE:
    set_audio_device (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_audio_device_row_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  PhoshAudioDeviceRow *self = PHOSH_AUDIO_DEVICE_ROW (object);

  switch (property_id) {
  case PROP_AUDIO_DEVICE:
    g_value_set_object (value, self->audio_device);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_audio_device_row_dispose (GObject *object)
{
  PhoshAudioDeviceRow *self = PHOSH_AUDIO_DEVICE_ROW(object);

  g_clear_object (&self->audio_device);

  G_OBJECT_CLASS (phosh_audio_device_row_parent_class)->dispose (object);
}


static void
phosh_audio_device_row_class_init (PhoshAudioDeviceRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_audio_device_row_get_property;
  object_class->set_property = phosh_audio_device_row_set_property;
  object_class->dispose = phosh_audio_device_row_dispose;

  props[PROP_AUDIO_DEVICE] =
    g_param_spec_object ("audio-device", "", "",
                         PHOSH_TYPE_AUDIO_DEVICE,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/audio-device-row.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioDeviceRow, description);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioDeviceRow, icon);
  gtk_widget_class_bind_template_child (widget_class, PhoshAudioDeviceRow, revealer);
}


static void
phosh_audio_device_row_init (PhoshAudioDeviceRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


PhoshAudioDeviceRow *
phosh_audio_device_row_new (PhoshAudioDevice *audio_device)
{
  return g_object_new (PHOSH_TYPE_AUDIO_DEVICE_ROW,
                       "audio-device", audio_device,
                       NULL);
}

/**
 * phosh_audio_device_row_get_audio_device:
 * @self: An audio device row
 *
 * Get the audio device associated with this row
 *
 * Returns:(transfer none): The audio device
 */
PhoshAudioDevice *
phosh_audio_device_row_get_audio_device (PhoshAudioDeviceRow *self)
{
  g_return_val_if_fail (PHOSH_IS_AUDIO_DEVICE_ROW (self), NULL);

  return self->audio_device;
}
