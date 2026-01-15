/*
 * Copyright © 2019-2020 Zander Brown <zbrown@gnome.org>
 *             2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Inspired by gliststore.c:
 *     Copyright 2015 Lars Uebernickel
 *     Copyright 2015 Ryan Lortie
 * https://gitlab.gnome.org/GNOME/glib/blob/713fec9dcb1ee49c4f64bbb6f483a5cd1db9966a/gio/gliststore.c
 *
 * Author: Zander Brown <zbrown@gnome.org>
 */

#include "app-list-model.h"
#include "folder-info.h"

#include <gmobile.h>

#include <gio/gio.h>

typedef struct _PhoshAppListModelPrivate PhoshAppListModelPrivate;
struct _PhoshAppListModelPrivate {
  GAppInfoMonitor *monitor;

  GSequence       *items;

  guint debounce;

  /* cache */
  struct {
    gboolean       is_valid;
    guint          position;
    GSequenceIter *iter;
  } last;

  GSettings  *settings;

  GHashTable *startup_wm_class;
  GHashTable *exec_to_id;
};

static void list_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshAppListModel, phosh_app_list_model, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhoshAppListModel)
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_iface_init))


static void
phosh_app_list_model_finalize (GObject *object)
{
  PhoshAppListModel *self = PHOSH_APP_LIST_MODEL (object);
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  g_clear_handle_id (&priv->debounce, g_source_remove);

  g_clear_pointer (&priv->startup_wm_class, g_hash_table_destroy);
  g_clear_pointer (&priv->exec_to_id, g_hash_table_destroy);
  g_clear_object (&priv->monitor);
  g_clear_object (&priv->settings);

  g_sequence_free (priv->items);

  G_OBJECT_CLASS (phosh_app_list_model_parent_class)->finalize (object);
}


static void
phosh_app_list_model_class_init (PhoshAppListModelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = phosh_app_list_model_finalize;
}


static GType
list_get_item_type (GListModel *list)
{
  return G_TYPE_APP_INFO;
}


static gpointer
list_get_item (GListModel *list, guint position)
{
  PhoshAppListModel *self = PHOSH_APP_LIST_MODEL (list);
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);
  GSequenceIter *it = NULL;

  if (priv->last.is_valid)
    {
      if (position < G_MAXUINT && priv->last.position == position + 1)
        it = g_sequence_iter_prev (priv->last.iter);
      else if (position > 0 && priv->last.position == position - 1)
        it = g_sequence_iter_next (priv->last.iter);
      else if (priv->last.position == position)
        it = priv->last.iter;
    }

  if (it == NULL)
    it = g_sequence_get_iter_at_pos (priv->items, position);

  priv->last.iter = it;
  priv->last.position = position;
  priv->last.is_valid = TRUE;

  if (g_sequence_iter_is_end (it))
    return NULL;
  else
    return g_object_ref (g_sequence_get (it));

}


static unsigned int
list_get_n_items (GListModel *list)
{
  PhoshAppListModel *self = PHOSH_APP_LIST_MODEL (list);
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  return g_sequence_get_length (priv->items);
}


static void
list_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = list_get_item_type;
  iface->get_item = list_get_item;
  iface->get_n_items = list_get_n_items;
}


static GList*
filter_out_apps_in_folder (GList *apps, PhoshFolderInfo *folder_info)
{
  GList *node, *prev;

  /* The length of the list is always at least 2. The first element is always the `folder_info`.
   * This makes the logic easier as otherwise we would have to check for edge-cases like first
   * element itself part of folder etc. */

  for (node = g_list_next (apps); node; node = g_list_next (node)) {
    GAppInfo *app_info = G_APP_INFO (node->data);
    if (phosh_folder_info_contains (folder_info, app_info)) {
      prev = g_list_previous (node);
      apps = g_list_delete_link (apps, node);
      node = prev;
    }
  }

  return apps;
}


static void on_folder_children_changed (PhoshAppListModel *self);


static void
emit_items_changed (PhoshAppListModel *self)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);
  int added = g_sequence_get_length (priv->items);
  int removed = added;
  g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, added);
}


static gboolean
items_changed (gpointer data)
{
  PhoshAppListModel *self = PHOSH_APP_LIST_MODEL (data);
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);
  g_auto (GStrv) folder_paths = NULL;
  g_autolist (GAppInfo) new_apps = NULL;
  int removed;
  int added = 0;

  new_apps = g_app_info_get_all ();

  g_return_val_if_fail (new_apps != NULL, G_SOURCE_REMOVE);

  removed = g_sequence_get_length (priv->items);
  g_sequence_remove_range (g_sequence_get_begin_iter (priv->items),
                           g_sequence_get_end_iter (priv->items));
  g_hash_table_remove_all (priv->startup_wm_class);
  g_hash_table_remove_all (priv->exec_to_id);

  folder_paths = g_settings_get_strv (priv->settings, "folder-children");

  for (int i = 0; i < g_strv_length (folder_paths); i++) {
    char *path = folder_paths[i];
    PhoshFolderInfo *folder_info = phosh_folder_info_new_from_folder_path (path);
    new_apps = g_list_prepend (new_apps, folder_info);
    new_apps = filter_out_apps_in_folder (new_apps, folder_info);
    g_signal_connect_object (folder_info, "apps-changed", G_CALLBACK (on_folder_children_changed),
                             self, G_CONNECT_SWAPPED);
    g_signal_connect_object (folder_info, "notify::name", G_CALLBACK (emit_items_changed),
                             self, G_CONNECT_SWAPPED);
  }

  for (GList *l = new_apps; l; l = g_list_next (l)) {
    const char *startup_wm_class;
    GAppInfo *app_info = l->data;

    /* We add folders irrespective of their emptiness because otherwise we won't be able to listen
     * for apps-changed signal. */
    if (!PHOSH_IS_FOLDER_INFO (app_info) && !g_app_info_should_show (app_info))
      continue;

    g_sequence_append (priv->items, g_object_ref (app_info));
    added++;

    if (!G_IS_DESKTOP_APP_INFO (app_info))
      continue;

    startup_wm_class = g_desktop_app_info_get_startup_wm_class (G_DESKTOP_APP_INFO (app_info));
    if (startup_wm_class) {
      g_hash_table_insert (priv->startup_wm_class,
                           g_strdup (startup_wm_class),
                           g_object_ref (app_info));
    }
  }

  priv->last.is_valid = FALSE;
  priv->last.iter = NULL;
  priv->last.position = 0;

  g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, added);

  priv->debounce = 0;

  return G_SOURCE_REMOVE;
}


static void
on_monitor_changed_cb (GAppInfoMonitor *monitor,
                       gpointer         data)
{
  PhoshAppListModel *self = PHOSH_APP_LIST_MODEL (data);
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  if (priv->debounce != 0) {
    g_source_remove (priv->debounce);
  }
  priv->debounce = g_timeout_add (500, items_changed, data);
  g_source_set_name_by_id (priv->debounce, "[phosh] debounce app changes");
}


static void
on_folder_children_changed (PhoshAppListModel *self)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  /* A folder has been created or destroyed or modified.
   * Rearrange the apps from scratch. */
  on_monitor_changed_cb (priv->monitor, self);
}


static void
phosh_app_list_model_init (PhoshAppListModel *self)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  priv->debounce = 0;
  priv->startup_wm_class = g_hash_table_new_full (g_str_hash,
                                                  g_str_equal,
                                                  g_free,
                                                  g_object_unref);
  priv->exec_to_id = g_hash_table_new_full (g_str_hash,
                                            g_str_equal,
                                            g_free,
                                            g_object_unref);

  priv->last.is_valid = FALSE;

  priv->items = g_sequence_new ((GDestroyNotify) g_object_unref);
  priv->monitor = g_app_info_monitor_get ();
  g_signal_connect (priv->monitor, "changed", G_CALLBACK (on_monitor_changed_cb), self);

  priv->settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  g_signal_connect_object (priv->settings, "changed::folder-children",
                           G_CALLBACK (on_folder_children_changed),
                           self, G_CONNECT_SWAPPED);

  on_monitor_changed_cb (priv->monitor, self);
}

/**
 * phosh_app_list_model_get_default:
 *
 * Return Value: (transfer none): The global #PhoshAppListModel singleton
 */
PhoshAppListModel *
phosh_app_list_model_get_default (void)
{
  static PhoshAppListModel *instance;

  if (instance == NULL) {
    instance = g_object_new (PHOSH_TYPE_APP_LIST_MODEL, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }

  return instance;
}

/**
 * phosh_app_list_model_lookup_by_startup_wm_class:
 * @self: The app list model
 * @class: The `StartupWMClass`
 *
 * Lookup the app-id by the wm class of the toplevel.
 * https://specifications.freedesktop.org/desktop-entry-spec/latest/recognized-keys.html
 *
 * Returns: (transfer full)(nullable): GDesktopAppInfo for requested app_id
 */
GDesktopAppInfo *
phosh_app_list_model_lookup_by_startup_wm_class (PhoshAppListModel *self,
                                                 const char        *class)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_APP_LIST_MODEL (self), NULL);
  g_return_val_if_fail (class, NULL);

  return g_hash_table_lookup (priv->startup_wm_class, class);
}


void
phosh_app_list_model_add_exec (PhoshAppListModel *self,
                               const char        *exec,
                               GAppInfo          *info)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);
  g_autofree char *bin = NULL, *cmd = NULL;
  g_auto (GStrv) parts = NULL;

  g_return_if_fail (PHOSH_IS_APP_LIST_MODEL (self));
  g_return_if_fail (G_IS_APP_INFO (info));
  g_return_if_fail (exec);

  parts = g_strsplit (exec, " ", -1);
  if (gm_strv_is_null_or_empty (parts))
    return;

  cmd = g_path_get_basename (parts[0]);
  if (gm_str_is_null_or_empty (cmd))
    return;

  g_hash_table_insert (priv->exec_to_id, g_steal_pointer (&cmd), g_object_ref (info));
}


GDesktopAppInfo *
phosh_app_list_model_lookup_by_exec (PhoshAppListModel *self, const char *exec)
{
  PhoshAppListModelPrivate *priv = phosh_app_list_model_get_instance_private (self);

  return g_hash_table_lookup (priv->exec_to_id, exec);
}
