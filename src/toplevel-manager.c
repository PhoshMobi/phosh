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
  N_SIGNALS
};
static guint signals[N_SIGNALS];


struct _PhoshToplevelManager {
  GObject parent;

  GPtrArray *toplevels;         /* (element-type: PhoshToplevel) */
  GPtrArray *toplevels_pending; /* (element-type: PhoshToplevel) */
};

G_DEFINE_TYPE (PhoshToplevelManager, phosh_toplevel_manager, G_TYPE_OBJECT);

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
}


static void
phosh_toplevel_manager_init (PhoshToplevelManager *self)
{
  struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager;
  PhoshWayland *wayland = phosh_wayland_get_default ();

  toplevel_manager = phosh_wayland_get_zwlr_foreign_toplevel_manager_v1 (wayland);

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
