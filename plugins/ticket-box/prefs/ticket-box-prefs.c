/*
 * Copyright (C) 2022 Purism SPC
 *               2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-plugin-prefs-config.h"

#include "ticket-box-prefs.h"

#define TICKET_BOX_SCHEMA_ID "mobi.phosh.plugins.ticket-box"
#define TICKET_BOX_FOLDER_KEY "folder"

#include <glib/gi18n-lib.h>

/**
 * PhoshTicketBoxPrefs:
 *
 * Preferences for ticket-box plugin
 */
struct _PhoshTicketBoxPrefs {
  AdwPreferencesDialog  parent;

  GtkWidget            *folder_entry;
  GtkWidget            *folder_button;

  char                 *ticket_box_path;
  GSettings            *settings;
};

G_DEFINE_TYPE (PhoshTicketBoxPrefs, phosh_ticket_box_prefs, ADW_TYPE_PREFERENCES_DIALOG);

static gboolean
folder_get_mapping (GValue *value, GVariant *variant, gpointer user_data)
{
  g_autofree char *path = NULL;
  const char *folder;

  folder = g_variant_get_string (variant, NULL);

  if (folder[0] != '/')
    path = g_build_filename (g_get_home_dir (), folder, NULL);
  else
    path = g_strdup (folder);

  g_value_set_string (value, path);

  return TRUE;
}


static void
on_select_folder_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshTicketBoxPrefs *self = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (GFile) file = NULL;
  g_autofree char *filename = NULL;

  file = gtk_file_dialog_select_folder_finish (GTK_FILE_DIALOG (source_object),
                                               res,
                                               &err);
  if (!file) {
    if (!g_error_matches (err, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED))
      g_warning ("Failed to folders: %s", err->message);
    return;
  }

  self = PHOSH_TICKET_BOX_PREFS (user_data);
  g_assert (PHOSH_IS_TICKET_BOX_PREFS (self));

  filename = g_file_get_path (file);
  gtk_editable_set_text (GTK_EDITABLE (self->folder_entry), filename);
}


static void
on_folder_button_clicked (PhoshTicketBoxPrefs *self)
{
  g_autoptr (GtkFileDialog) file_dialog = NULL;
  const char *current;
  g_autoptr (GFile) current_file = NULL;
  GtkWindow *window;

  g_assert (PHOSH_IS_TICKET_BOX_PREFS (self));

  current = gtk_editable_get_text (GTK_EDITABLE (self->folder_entry));
  current_file = g_file_new_for_path (current);

  file_dialog = g_object_new (GTK_TYPE_FILE_DIALOG,
                              "accept-label", _("_Open"),
                              "title", _("Choose Folder"),
                              "initial-file", current_file,
                              "modal", TRUE,
                              NULL);

  window = GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_WINDOW));
  gtk_file_dialog_select_folder (file_dialog,
                                 window,
                                 NULL,
                                 on_select_folder_ready,
                                 self);
}


static void
phosh_ticket_box_prefs_finalize (GObject *object)
{
  PhoshTicketBoxPrefs *self = PHOSH_TICKET_BOX_PREFS (object);

  g_clear_object (&self->settings);
  g_clear_pointer (&self->ticket_box_path, g_free);

  G_OBJECT_CLASS (phosh_ticket_box_prefs_parent_class)->finalize (object);
}

static void
phosh_ticket_box_prefs_class_init (PhoshTicketBoxPrefsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_ticket_box_prefs_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/ticket-box-prefs/ticket-box-prefs.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshTicketBoxPrefs, folder_entry);
  gtk_widget_class_bind_template_callback (widget_class, on_folder_button_clicked);
}


static void
phosh_ticket_box_prefs_init (PhoshTicketBoxPrefs *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (TICKET_BOX_SCHEMA_ID);
  g_settings_bind_with_mapping (self->settings, TICKET_BOX_FOLDER_KEY,
                                self->folder_entry, "text",
                                G_SETTINGS_BIND_DEFAULT,
                                folder_get_mapping,
                                NULL /* set_mapping */,
                                NULL /* userdata */,
                                NULL /* destroyfunc */);
}
