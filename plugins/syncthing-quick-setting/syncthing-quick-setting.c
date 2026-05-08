/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "folder-row.h"
#include "plugin-shell.h"
#include "status-page-placeholder.h"
#include "syncthing-quick-setting.h"
#include "syncbus.h"

#include <glib/gi18n.h>

#define SYNCBUS_BUS_NAME "mobi.phosh.syncbus"
#define SYNCBUS_MANAGER_IFACE "mobi.phosh.syncbus.Manager"
#define SYNCBUS_MANAGER_PATH "/mobi/phosh/syncbus/manager"
#define SYNCBUS_FOLDER_IFACE "mobi.phosh.syncbus.Folder"
#define SYNCBUS_FOLDERS_PATH "/mobi/phosh/syncbus/folders"

/**
 * PhoshSyncthingQuickSetting:
 *
 * A quick-setting for Syncthing.
 */

struct _PhoshSyncthingQuickSetting {
  PhoshQuickSetting   parent;

  GtkButton          *footer;
  PhoshStatusIcon    *info;
  PhoshStatusPagePlaceholder *placeholder;
  GtkButton          *placeholder_btn;
  GtkStack           *stack;
  GtkListBox         *folders_box;

  GCancellable       *cancel;
  SyncbusImplManager *manager;
  GDBusObjectManager *object_manager;
  /* key: path (string, owned), value: row (PhoshSyncthingFolderRow, not owned) */
  GHashTable         *folders;
};

G_DEFINE_TYPE (PhoshSyncthingQuickSetting, phosh_syncthing_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static void
on_start_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshSyncthingQuickSetting *self = data;
  g_autoptr (GError) err = NULL;

  if (!syncbus_impl_manager_call_start_finish (self->manager, result, &err)) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_critical ("Unable to start Syncthing: %s", err->message);
  }
}


static void
on_stop_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshSyncthingQuickSetting *self = data;
  g_autoptr (GError) err = NULL;

  if (!syncbus_impl_manager_call_stop_finish (self->manager, result, &err)) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_critical ("Unable to stop Syncthing: %s", err->message);
  }
}


static void
on_clicked (PhoshSyncthingQuickSetting *self)
{
  gboolean active = phosh_quick_setting_get_active (PHOSH_QUICK_SETTING (self));

  if (active)
    syncbus_impl_manager_call_stop (self->manager, self->cancel, on_stop_ready, self);
  else
    syncbus_impl_manager_call_start (self->manager, self->cancel, on_start_ready, self);
}


static void
on_app_launch_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  g_autoptr (GError) err = NULL;

  if (!g_app_info_launch_default_for_uri_finish (result, &err)) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_critical ("Unable to launch Syncthing GUI: %s", err->message);
  }
}


static void
on_footer_clicked (PhoshSyncthingQuickSetting *self)
{
  g_app_info_launch_default_for_uri_async (syncbus_impl_manager_get_url (self->manager), NULL,
                                           self->cancel, on_app_launch_ready, self);
}


static void
on_enabled_changed (PhoshSyncthingQuickSetting *self)
{
  gboolean enabled = syncbus_impl_manager_get_enabled (self->manager);
  const char *icon_name = enabled ? "cloud-filled-symbolic" : "cloud-disabled-symbolic";

  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), enabled);
  gtk_widget_set_sensitive (GTK_WIDGET (self->footer), enabled);
  phosh_status_icon_set_icon_name (self->info, icon_name);

  if (enabled)
    gtk_stack_set_visible_child_name (self->stack, "folders");
  else
    gtk_stack_set_visible_child_name (self->stack, "placeholder");

}


static void
on_object_added (PhoshSyncthingQuickSetting *self, GDBusObject *object)
{
  const char *path = g_dbus_object_get_object_path (object);
  g_autoptr (GDBusInterface) iface = g_dbus_object_get_interface (object, SYNCBUS_FOLDER_IFACE);
  GtkWidget *row = phosh_syncthing_folder_row_new (SYNCBUS_IMPL_FOLDER (iface));

  g_debug ("Adding folder: %s", path);
  gtk_container_add (GTK_CONTAINER (self->folders_box), row);
  g_hash_table_insert (self->folders, g_strdup (path), row);
}


static void
on_object_removed (PhoshSyncthingQuickSetting *self, GDBusObject *object)
{
  const char *path = g_dbus_object_get_object_path (object);
  GtkWidget *row = g_hash_table_lookup (self->folders, path);

  g_debug ("Removing folder: %s", path);
  gtk_container_remove (GTK_CONTAINER (self->folders_box), row);
  g_hash_table_remove (self->folders, path);
}


static void
on_object_manager_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshSyncthingQuickSetting *self = data;
  g_autoptr (GError) err = NULL;
  g_autolist (GDBusObject) objects = NULL;

  self->object_manager = g_dbus_object_manager_client_new_for_bus_finish (result, &err);
  if (!self->object_manager) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    phosh_status_page_placeholder_set_title (self->placeholder, _("Unable to connect to Syncbus"));
    gtk_widget_set_visible (GTK_WIDGET (self->placeholder_btn), FALSE);
    g_critical ("Unable to connect to Syncbus DBus server: %s", err->message);
    return;
  }

  objects = g_dbus_object_manager_get_objects (self->object_manager);
  for (GList *head = objects; head != NULL; head = head->next) {
    GDBusObject *object = head->data;
    on_object_added (self, object);
  }

  g_object_connect (self->manager,
                    "swapped-object-signal::notify::enabled", G_CALLBACK (on_enabled_changed), self,
                    NULL);
  g_object_connect (self->object_manager,
                    "swapped-object-signal::object-added", G_CALLBACK (on_object_added), self,
                    "swapped-object-signal::object-removed", G_CALLBACK (on_object_removed), self,
                    NULL);
  on_enabled_changed (self);
}


static void
on_manager_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshSyncthingQuickSetting *self = data;
  g_autoptr (GError) err = NULL;

  self->manager = syncbus_impl_manager_proxy_new_for_bus_finish (result, &err);
  if (!self->manager) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    phosh_status_page_placeholder_set_title (self->placeholder, _("Unable to connect to Syncbus"));
    gtk_widget_set_visible (GTK_WIDGET (self->placeholder_btn), FALSE);
    g_critical ("Unable to connect to Syncbus DBus server: %s", err->message);
    return;
  }

  syncbus_impl_object_manager_client_new_for_bus (G_BUS_TYPE_SESSION,
                                                  G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
                                                  SYNCBUS_BUS_NAME, SYNCBUS_FOLDERS_PATH,
                                                  self->cancel,
                                                  on_object_manager_ready,
                                                  self);
}


static void
phosh_syncthing_quick_setting_dispose (GObject *object)
{
  PhoshSyncthingQuickSetting *self = PHOSH_SYNCTHING_QUICK_SETTING (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->manager);
  g_clear_object (&self->object_manager);
  g_clear_pointer (&self->folders, g_hash_table_unref);

  G_OBJECT_CLASS (phosh_syncthing_quick_setting_parent_class)->dispose (object);
}


static void
phosh_syncthing_quick_setting_class_init (PhoshSyncthingQuickSettingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_syncthing_quick_setting_dispose;

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/syncthing-quick-setting/icons");
  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/"
                                               "syncthing-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, folders_box);
  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, footer);
  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, info);
  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, placeholder);
  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, placeholder_btn);
  gtk_widget_class_bind_template_child (widget_class, PhoshSyncthingQuickSetting, stack);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_footer_clicked);
}


static void
phosh_syncthing_quick_setting_init (PhoshSyncthingQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancel = g_cancellable_new ();
  self->folders = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  syncbus_impl_manager_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          SYNCBUS_BUS_NAME, SYNCBUS_MANAGER_PATH,
                                          self->cancel,
                                          on_manager_ready,
                                          self);
}
