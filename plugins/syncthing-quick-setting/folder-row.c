/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "folder-row.h"

#include <glib/gi18n.h>

/**
 * PhoshSyncthingFolderRow:
 *
 * A widget to display a single Syncthing folder.
 */

enum {
  PROP_0,
  PROP_FOLDER,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshSyncthingFolderRow {
  HdyActionRow       parent;

  GtkImage          *icon;

  SyncbusImplFolder *folder;
};

G_DEFINE_TYPE (PhoshSyncthingFolderRow, phosh_syncthing_folder_row, HDY_TYPE_ACTION_ROW);


static void
phosh_syncthing_folder_row_set_property (GObject      *object,
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  PhoshSyncthingFolderRow *self = PHOSH_SYNCTHING_FOLDER_ROW (object);

  switch (property_id) {
  case PROP_FOLDER:
    phosh_syncthing_folder_row_set_folder (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
on_activated (PhoshSyncthingFolderRow *self)
{
  gboolean paused = syncbus_impl_folder_get_paused (self->folder);

  syncbus_impl_folder_set_paused (self->folder, !paused);
}


static void
on_props_changed (PhoshSyncthingFolderRow *self)
{
  const char *label = syncbus_impl_folder_get_label (self->folder);
  const char *path = syncbus_impl_folder_get_path (self->folder);
  int completion = syncbus_impl_folder_get_completion (self->folder);
  const char *state = syncbus_impl_folder_get_state (self->folder);
  gboolean paused = syncbus_impl_folder_get_paused (self->folder);
  g_autofree char *title = NULL;
  g_autofree char *subtitle = NULL;

  /* Translators: This is completion status with a percent symbol */
  title = g_strdup_printf ("%s (%d%%)", label, completion);
  if (g_strcmp0 (state, "") == 0)
    subtitle = g_strdup (path);
  else
    subtitle = g_strdup_printf ("%s (%s)", path, state);

  hdy_preferences_row_set_title (HDY_PREFERENCES_ROW (self), title);
  hdy_action_row_set_subtitle (HDY_ACTION_ROW (self), subtitle);
  gtk_image_set_from_icon_name (self->icon,
                                paused ?
                                "media-playback-start-symbolic": "media-playback-stop-symbolic",
                                GTK_ICON_SIZE_INVALID);
}


static void
phosh_syncthing_folder_row_dispose (GObject *object)
{
  PhoshSyncthingFolderRow *self = PHOSH_SYNCTHING_FOLDER_ROW (object);

  g_clear_object (&self->folder);

  G_OBJECT_CLASS (phosh_syncthing_folder_row_parent_class)->dispose (object);
}


static void
phosh_syncthing_folder_row_class_init (PhoshSyncthingFolderRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_syncthing_folder_row_set_property;
  object_class->dispose = phosh_syncthing_folder_row_dispose;

  /**
   * PhoshSyncthingFolderRow:folder:
   *
   * The folder for row.
   */
  props[PROP_FOLDER] =
    g_param_spec_object ("folder", "", "",
                         SYNCBUS_IMPL_TYPE_FOLDER,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "syncthing-quick-setting/folder-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingFolderRow, icon);

  gtk_widget_class_bind_template_callback (widget_class, on_activated);
}


static void
phosh_syncthing_folder_row_init (PhoshSyncthingFolderRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_syncthing_folder_row_new (SyncbusImplFolder *folder)
{
  return g_object_new (PHOSH_TYPE_SYNCTHING_FOLDER_ROW, "folder", folder, NULL);
}


void
phosh_syncthing_folder_row_set_folder (PhoshSyncthingFolderRow *self, SyncbusImplFolder *folder)
{
  g_return_if_fail (PHOSH_IS_SYNCTHING_FOLDER_ROW (self));

  if (self->folder == folder)
    return;

  if (self->folder) {
    g_signal_handlers_disconnect_by_func (self->folder, on_props_changed, self);
    g_clear_object (&self->folder);
  }

  g_set_object (&self->folder, folder);
  g_signal_connect_object (self->folder,
                           "notify",
                           G_CALLBACK (on_props_changed),
                           self,
                           G_CONNECT_SWAPPED);

  on_props_changed (self);
}
