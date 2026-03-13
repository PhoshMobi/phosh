/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phosh-favorite-list-model"

#define FAVORITES_KEY "favorites"

#include "favorite-list-model.h"

#include "folder-info.h"

#include <gio/gio.h>

/**
 * PhoshFavoriteListModel:
 *
 * A `GListModel` of the users favorite applications
 *
 * Since: 0.1.3
 */

typedef struct _PhoshFavoriteListModelPrivate {
  /* The complete list as stored in @settings */
  GStrv items_inc_missing;

  /* The sanitised list */
  GStrv items;
  /* Cached length of @items */
  guint len;

  GSettings *settings;
} PhoshFavoriteListModelPrivate;

static void list_iface_init (GListModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (PhoshFavoriteListModel, phosh_favorite_list_model, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (PhoshFavoriteListModel)
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_iface_init))


static void
phosh_favorite_list_model_finalize (GObject *object)
{
  PhoshFavoriteListModel *self = PHOSH_FAVORITE_LIST_MODEL (object);
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (self);

  g_clear_object (&priv->settings);

  g_clear_pointer (&priv->items_inc_missing, g_strfreev);
  g_clear_pointer (&priv->items, g_strfreev);

  G_OBJECT_CLASS (phosh_favorite_list_model_parent_class)->finalize (object);
}


static void
phosh_favorite_list_model_class_init (PhoshFavoriteListModelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = phosh_favorite_list_model_finalize;
}


static GType
list_get_item_type (GListModel *list)
{
  return G_TYPE_APP_INFO;
}


static gpointer
list_get_item (GListModel *list, guint position)
{
  PhoshFavoriteListModel *self = PHOSH_FAVORITE_LIST_MODEL (list);
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (self);

  if (position >= priv->len) {
    return NULL;
  }

  return g_desktop_app_info_new (priv->items[position]);
}


static unsigned int
list_get_n_items (GListModel *list)
{
  PhoshFavoriteListModel *self = PHOSH_FAVORITE_LIST_MODEL (list);
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (self);

  return priv->len;
}


static void
list_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = list_get_item_type;
  iface->get_item = list_get_item;
  iface->get_n_items = list_get_n_items;
}


static void
favorites_changed (GSettings *settings, const char *key, PhoshFavoriteListModel *self)
{
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (self);
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  int removed;

  /* Clear the old items */
  removed = priv->len;

  g_clear_pointer (&priv->items_inc_missing, g_strfreev);
  g_clear_pointer (&priv->items, g_strfreev);

  /* Get the new list */
  priv->items_inc_missing = g_settings_get_strv (settings, key);

  for (int i = 0; priv->items_inc_missing[i]; i++) {
    g_autoptr (GDesktopAppInfo) info = NULL;

    /* We don't actually care about this value, just that it isn't NULL */
    info = g_desktop_app_info_new (priv->items_inc_missing[i]);

    if (G_UNLIKELY (info == NULL)) {
      g_debug ("Missing favorite %s, skipping", priv->items_inc_missing[i]);
      continue;
    }

    g_strv_builder_add (builder, priv->items_inc_missing[i]);
  }

  priv->items = g_strv_builder_end (builder);
  priv->len  = g_strv_length (priv->items);

  g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, priv->len);
}


static void
phosh_favorite_list_model_init (PhoshFavoriteListModel *self)
{
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (self);

  priv->settings = g_settings_new ("sm.puri.phosh");
  g_signal_connect (priv->settings, "changed::" FAVORITES_KEY,
                    G_CALLBACK (favorites_changed), self);
  favorites_changed (priv->settings, FAVORITES_KEY, self);
}


/**
 * phosh_favorite_list_model_get_default:
 *
 * Returns: (transfer none): The global #PhoshFavoriteListModel singleton
 */
PhoshFavoriteListModel *
phosh_favorite_list_model_get_default (void)
{
  static PhoshFavoriteListModel *instance;

  if (instance == NULL) {
    instance = g_object_new (PHOSH_TYPE_FAVORITE_LIST_MODEL, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *) &instance);
  }

  return instance;
}


/**
 * phosh_favorite_list_model_app_is_favorite:
 * @self: (nullable): the #PhoshFavoriteListModel, use %NULL for the default
 * @app: a #GAppInfo to lookup
 *
 * Returns: %TRUE if @app if currently a favorite, otherwise %FALSE
 */
gboolean
phosh_favorite_list_model_app_is_favorite (PhoshFavoriteListModel *self, GAppInfo *app)
{
  PhoshFavoriteListModel *list = self != NULL ? self : phosh_favorite_list_model_get_default ();
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (list);
  const char *id;

  if (PHOSH_IS_FOLDER_INFO (app))
    return FALSE;

  g_return_val_if_fail (G_IS_APP_INFO (app), FALSE);

  id = g_app_info_get_id (app);

  if (G_UNLIKELY (id == NULL))
    return FALSE;

  if (g_strv_contains ((const char *const *) priv->items_inc_missing, id))
    return TRUE;

  return FALSE;
}


void
phosh_favorite_list_model_add_app (PhoshFavoriteListModel *self, GAppInfo *app)
{
  PhoshFavoriteListModel *list = self != NULL ? self : phosh_favorite_list_model_get_default ();
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (list);
  const char *id;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_auto (GStrv) new_favorites = NULL;

  g_return_if_fail (G_IS_APP_INFO (app));

  id = g_app_info_get_id (app);
  if (G_UNLIKELY (id == NULL)) {
    g_critical ("Can't add `%p`, doesn't have an id", app);
    return;
  }

  for (int i = 0; priv->items_inc_missing[i]; i++) {
    /* Avoid having the same favorite twice */
    if (G_UNLIKELY (g_strcmp0 (priv->items_inc_missing[i], id) == 0)) {
      g_warning ("%s is already a favorite", id);
      return;
    }
    g_strv_builder_add (builder, priv->items_inc_missing[i]);
  }
  /* Add the new id */
  g_strv_builder_add (builder, id);
  new_favorites = g_strv_builder_end (builder);

  /* Indirectly calls favorites_changed which updates the model */
  g_settings_set_strv (priv->settings, FAVORITES_KEY, (const char *const *) new_favorites);
}


void
phosh_favorite_list_model_remove_app (PhoshFavoriteListModel *self, GAppInfo *app)
{
  PhoshFavoriteListModel *list = self != NULL ? self : phosh_favorite_list_model_get_default ();
  PhoshFavoriteListModelPrivate *priv = phosh_favorite_list_model_get_instance_private (list);
  const char *id;
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_auto (GStrv) new_favorites = NULL;

  g_return_if_fail (G_IS_APP_INFO (app));

  id = g_app_info_get_id (app);

  if (G_UNLIKELY (id == NULL)) {
    g_critical ("Can't remove `%p`, doesn't have an id", app);
    return;
  }

  for (int i = 0; priv->items_inc_missing[i]; i++) {
    /* Skip over the favorite being removed */
    if (g_strcmp0 (priv->items_inc_missing[i], id) == 0)
      continue;

    g_strv_builder_add (builder, priv->items_inc_missing[i]);
  }

  new_favorites = g_strv_builder_end (builder);
  /* If we actually removed id then there should be 1 item less */
  if (!(g_strv_length (new_favorites) < g_strv_length (priv->items_inc_missing))) {
    g_warning ("%s wasn't a favorite", id);
    return;
  }

  /* Indirectly calls favorites_changed which updates the model */
  g_settings_set_strv (priv->settings, FAVORITES_KEY, (const char *const *) new_favorites);
}
