/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Sebastian Krzyszkowiak <sebastian.krzyszkowiak@puri.sm>
 */

#include "toplevel-manager.h"
#include "toplevel.h"


enum {
  TOPLEVEL_ADDED,
  TOPLEVEL_CHANGED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

struct _PhoshToplevelManager {
  GObject parent;
};

G_DEFINE_TYPE (PhoshToplevelManager, phosh_toplevel_manager, G_TYPE_OBJECT);


static void
phosh_toplevel_manager_class_init (PhoshToplevelManagerClass *klass)
{
  signals[TOPLEVEL_ADDED] = g_signal_new ("toplevel-added",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL,
                                          G_TYPE_NONE,
                                          1,
                                          PHOSH_TYPE_TOPLEVEL);

  signals[TOPLEVEL_ADDED] = g_signal_new ("toplevel-changed",
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
}


PhoshToplevel *
phosh_toplevel_manager_get_toplevel (PhoshToplevelManager *self, guint num)
{
  return phosh_toplevel_new_from_handle (NULL);
}


guint
phosh_toplevel_manager_get_num_toplevels (PhoshToplevelManager *self)
{
  return 0;
}


PhoshToplevelManager *
phosh_toplevel_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_TOPLEVEL_MANAGER, NULL);
}

PhoshToplevel *
phosh_toplevel_manager_get_parent (PhoshToplevelManager *self, PhoshToplevel *toplevel)
{
  return NULL;
}
