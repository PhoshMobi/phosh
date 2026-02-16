/*
 * Copyright (C) 2019 Purism SPC
 *               2023-2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#define G_LOG_DOMAIN "phosh-toplevel-manager"

#include "toplevel-manager.h"
#include "toplevel.h"
#include "phosh-wayland.h"
#include "shell-priv.h"
#include "util.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"

#include <gdk/gdkwayland.h>

/**
 * PhoshToplevelManager:
 *
 * Tracks and interacts with toplevel surfaces for window management
 * purposes using the wlr-foreign-toplevel-unstable-v1 wayland
 * protocol.
 */

enum {
  PROP_0,
  PROP_NUM_TOPLEVELS,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


enum {
  TOPLEVEL_ADDED,
  TOPLEVEL_CHANGED,
  TOPLEVEL_MISSING,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

#define MAX_INITIAL_TOPLEVEL_TIMEOUT 30 /* s */

typedef struct {
  GAppInfo *app_info;
  guint     timeout_id;
  PhoshToplevelManager *manager; /* unowned */
} LaunchingAppInfo;

struct _PhoshToplevelManager {
  GObject          parent;

  GPtrArray       *toplevels;         /* (element-type: PhoshToplevel) */
  GPtrArray       *toplevels_pending; /* (element-type: PhoshToplevel) */

  PhoshAppTracker *app_tracker;
  GPtrArray       *launching_apps;    /* (element-type: LaunchingToplevelInfo */
};

G_DEFINE_TYPE (PhoshToplevelManager, phosh_toplevel_manager, G_TYPE_OBJECT);


static void
on_initial_toplevel_timeout (gpointer data)
{
  LaunchingAppInfo *info = data;

  g_signal_emit (info->manager, signals[TOPLEVEL_MISSING], 0, info->app_info);
  info->timeout_id = 0;

  if (!g_ptr_array_remove_fast (info->manager->launching_apps, info))
    g_critical ("Failed to find launching info for %s", g_app_info_get_id (info->app_info));
}


static void
launching_app_info_free (LaunchingAppInfo *info)
{
  g_clear_handle_id (&info->timeout_id, g_source_remove);
  g_clear_object (&info->app_info);
  g_free (info);
}


static LaunchingAppInfo *
launching_app_info_new (PhoshToplevelManager *toplevel_manager, GAppInfo *app_info)
{
  LaunchingAppInfo *info = g_new0 (LaunchingAppInfo, 1);

  info->app_info = g_object_ref (app_info);
  info->manager = toplevel_manager;

  info->timeout_id = g_timeout_add_seconds_once (MAX_INITIAL_TOPLEVEL_TIMEOUT,
                                                 on_initial_toplevel_timeout,
                                                 info);
  return info;
}


static gboolean
app_info_has_toplevel (PhoshToplevelManager *self, GAppInfo *app_info)
{
  const char *app_id = g_app_info_get_id (app_info);

  for (int i = 0; i < self->toplevels->len; i++) {
    PhoshToplevel *toplevel = g_ptr_array_index (self->toplevels, i);

    if (g_strcmp0 (phosh_toplevel_get_app_id (toplevel), app_id) == 0)
      return TRUE;
  }

  return FALSE;
}


static void
remove_from_launching (PhoshToplevelManager *self, PhoshToplevel *toplevel)
{
  g_autoptr (GAppInfo) needle = NULL;
  const char *app_id;

  app_id = phosh_toplevel_get_app_id (toplevel);
  if (app_id)
    return;

  needle = G_APP_INFO (phosh_get_desktop_app_info_for_app_id (app_id));
  if (!needle)
    return;

  for (int i = 0; i < self->launching_apps->len; i++) {
    LaunchingAppInfo *info = g_ptr_array_index (self->launching_apps, i);

    /* TODO: we don't know the toplevels activation token (startup-id) so we can just go
     * by app-id and thus can't distinguish multi window apps */
    if (g_app_info_equal (G_APP_INFO (needle), info->app_info)) {
      g_ptr_array_remove_index_fast (self->launching_apps, i);
      g_debug ("Found toplevel for launching app %s", g_app_info_get_id (needle));
      return;
    }
  }

  g_debug ("Couldn't find app info toplevel %s", app_id);
}


static void
on_app_launched (PhoshToplevelManager *self, GAppInfo *app_info, const char *startup_id)
{
  LaunchingAppInfo *info;
  const char *app_id = g_app_info_get_id (app_info);

  if (app_info_has_toplevel (self, app_info)) {
    g_debug ("App info %s already has a toplevel", app_id);
    return;
  }

  g_warning ("Tracking %s as there's no toplevel yet", app_id);
  info = launching_app_info_new (self, app_info);
  g_ptr_array_add (self->launching_apps, info);
}


static void
phosh_toplevel_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhoshToplevelManager *self = PHOSH_TOPLEVEL_MANAGER (object);

  switch (property_id) {
  case PROP_NUM_TOPLEVELS:
    g_value_set_int (value, self->toplevels->len);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
on_toplevel_closed (PhoshToplevelManager *self, PhoshToplevel *toplevel)
{
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));

  /* Check if toplevel exists in toplevels_pending, in that case it is
   * not yet configured and we just remove it from toplevels_pending
   * without touching the regular toplevels array. */
  if (g_ptr_array_find (self->toplevels_pending, toplevel, NULL)) {
    g_assert_true (g_ptr_array_remove (self->toplevels_pending, toplevel));
    g_object_unref (toplevel);
    return;
  }

  g_assert_true (g_ptr_array_remove (self->toplevels, toplevel));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NUM_TOPLEVELS]);
}


static void
on_toplevel_configured (PhoshToplevelManager *self, GParamSpec *pspec, PhoshToplevel *toplevel)
{
  gboolean configured;
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self));
  g_return_if_fail (PHOSH_IS_TOPLEVEL (toplevel));

  configured = phosh_toplevel_is_configured (toplevel);

  if (!configured)
    return;

  if (g_ptr_array_find (self->toplevels, toplevel, NULL)) {
    g_signal_emit (self, signals[TOPLEVEL_CHANGED], 0, toplevel);
  } else {
    g_assert_true (g_ptr_array_remove (self->toplevels_pending, toplevel));
    g_ptr_array_add (self->toplevels, toplevel);
    g_signal_emit (self, signals[TOPLEVEL_ADDED], 0, toplevel);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_NUM_TOPLEVELS]);

    remove_from_launching (self, toplevel);
  }
}


static void
handle_zwlr_foreign_toplevel_manager_toplevel (void                                    *data,
                                               struct zwlr_foreign_toplevel_manager_v1 *unused,
                                               struct zwlr_foreign_toplevel_handle_v1  *handle)
{
  PhoshToplevelManager *self = data;
  PhoshToplevel *toplevel;
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self));
  toplevel = phosh_toplevel_new_from_handle (handle);

  g_ptr_array_add (self->toplevels_pending, toplevel);

  g_signal_connect_swapped (toplevel, "closed", G_CALLBACK (on_toplevel_closed), self);
  g_signal_connect_swapped (toplevel,
                            "notify::configured",
                            G_CALLBACK (on_toplevel_configured),
                            self);

  g_debug ("Got toplevel %p", toplevel);
}


static void
handle_zwlr_foreign_toplevel_manager_finished (void                                    *data,
                                               struct zwlr_foreign_toplevel_manager_v1 *unused)
{
  g_debug ("wlr_foreign_toplevel_manager_finished");
}


static const
struct zwlr_foreign_toplevel_manager_v1_listener zwlr_foreign_toplevel_manager_listener = {
  handle_zwlr_foreign_toplevel_manager_toplevel,
  handle_zwlr_foreign_toplevel_manager_finished,
};


static void
phosh_toplevel_manager_dispose (GObject *object)
{
  PhoshToplevelManager *self = PHOSH_TOPLEVEL_MANAGER (object);
  if (self->toplevels) {
    g_ptr_array_free (self->toplevels, TRUE);
    self->toplevels = NULL;
  }
  if (self->toplevels_pending) {
    g_ptr_array_free (self->toplevels_pending, TRUE);
    self->toplevels_pending = NULL;
  }

  g_clear_pointer (&self->launching_apps, g_ptr_array_unref);
  g_clear_object (&self->app_tracker);

  G_OBJECT_CLASS (phosh_toplevel_manager_parent_class)->dispose (object);
}


static void
phosh_toplevel_manager_class_init (PhoshToplevelManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_toplevel_manager_dispose;
  object_class->get_property = phosh_toplevel_get_property;

  /**
   * PhoshToplevelManager:num-toplevels:
   *
   * The current number of toplevels
   */
  props[PROP_NUM_TOPLEVELS] =
    g_param_spec_int ("num-toplevels", "", "",
                      0, G_MAXINT, 0,
                      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshToplevelManager::toplevel-added:
   * @manager: The #PhoshToplevelManager emitting the signal.
   * @toplevel: The #PhoshToplevel being added to the list.
   *
   * Emitted whenever a toplevel has been added to the list.
   */
  signals[TOPLEVEL_ADDED] = g_signal_new ("toplevel-added",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL,
                                          G_TYPE_NONE,
                                          1,
                                          PHOSH_TYPE_TOPLEVEL);
  /**
   * PhoshToplevelManager::toplevel-changed:
   * @manager: The #PhoshToplevelManager emitting the signal.
   * @toplevel: The #PhoshToplevel that changed properties.
   *
   * Emitted whenever a toplevel has changed properties.
   */
  signals[TOPLEVEL_CHANGED] = g_signal_new ("toplevel-changed",
                                            G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE,
                                            1,
                                            PHOSH_TYPE_TOPLEVEL);
  /**
   * PhoshToplevelManager::toplevel-missing:
   * @manager: The #PhoshToplevelManager emitting the signal.
   * @appinfo: The app info that didn't see a toplevel
   *
   * Emitted whenever an app from launching app list didn't see a
   * toplevel in time.
   */
  signals[TOPLEVEL_MISSING] = g_signal_new ("toplevel-missing",
                                            G_TYPE_FROM_CLASS (klass),
                                            G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL,
                                            G_TYPE_NONE,
                                            1,
                                            G_TYPE_APP_INFO);
}


static void
phosh_toplevel_manager_init (PhoshToplevelManager *self)
{
  struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;
  PhoshWayland *wayland = phosh_wayland_get_default ();

  toplevel_manager = phosh_wayland_get_zwlr_foreign_toplevel_manager_v1 (wayland);

  self->launching_apps = g_ptr_array_new_with_free_func ((GDestroyNotify)launching_app_info_free);
  self->toplevels = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
  self->toplevels_pending = g_ptr_array_new ();

  if (!toplevel_manager) {
    g_critical ("Missing wlr-foreign-toplevel-management protocol extension");
    return;
  }

  zwlr_foreign_toplevel_manager_v1_add_listener (toplevel_manager,
                                                 &zwlr_foreign_toplevel_manager_listener,
                                                 self);
}


PhoshToplevelManager *
phosh_toplevel_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_TOPLEVEL_MANAGER, NULL);
}

/**
 * phosh_toplevel_manager_get_toplevel:
 * @self: The toplevel manager
 *
 * Get the nth toplevel in the list of toplevels
 *
 * Returns:(transfer none): The toplevel
 */
PhoshToplevel *
phosh_toplevel_manager_get_toplevel (PhoshToplevelManager *self, guint num)
{
  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self), NULL);
  g_return_val_if_fail (self->toplevels, NULL);

  g_return_val_if_fail (num < self->toplevels->len, NULL);

  return g_ptr_array_index (self->toplevels, num);
}


guint
phosh_toplevel_manager_get_num_toplevels (PhoshToplevelManager *self)
{
  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self), 0);
  g_return_val_if_fail (self->toplevels, 0);

  return self->toplevels->len;
}

/**
 * phosh_toplevel_manager_get_parent:
 * @self: The toplevel manager
 * @toplevel: The toplevel to get the parent for
 *
 * Gets the parent toplevel of a given toplevel
 *
 * Returns:(transfer none)(nullable): The toplevel
 */
PhoshToplevel *
phosh_toplevel_manager_get_parent (PhoshToplevelManager *self, PhoshToplevel *toplevel)
{
  struct zwlr_foreign_toplevel_handle_v1 *parent_handle;

  g_return_val_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self), NULL);
  g_return_val_if_fail (self->toplevels, NULL);

  parent_handle = phosh_toplevel_get_parent_handle (toplevel);
  if (parent_handle == NULL)
    return NULL;

  for (int i = 0; i < self->toplevels->len; i++) {
    PhoshToplevel *t;

    t = g_ptr_array_index (self->toplevels, i);
    if (parent_handle == phosh_toplevel_get_handle (t))
      return t;
  }
  return NULL;
}


void
phosh_toplevel_manager_set_app_tracker (PhoshToplevelManager *self,
                                        PhoshAppTracker      *app_tracker)
{
  g_return_if_fail (PHOSH_IS_TOPLEVEL_MANAGER (self));
  g_return_if_fail (PHOSH_IS_APP_TRACKER (app_tracker));

  if (self->app_tracker) {
    g_signal_handlers_disconnect_by_data (self->app_tracker, self);
    g_clear_object (&self->app_tracker);
  }


  if (app_tracker) {
    self->app_tracker = g_object_ref (app_tracker);
    g_signal_connect_swapped (self->app_tracker,
                              "app-launched",
                              G_CALLBACK (on_app_launched),
                              self);
  }
}
