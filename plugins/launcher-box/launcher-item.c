/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "launcher-item.h"

#include <gio/gdesktopappinfo.h>

/**
 * PhoshLauncherItem:
 *
 * A launcher item keeps the app-info and the launcher's indicator state together
 */

enum {
  PROP_0,
  PROP_APP_INFO,
  PROP_PROGRESS_VISIBLE,
  PROP_PROGRESS,
  PROP_COUNT_VISIBLE,
  PROP_COUNT,
  PROP_HAS_DATA,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshLauncherItem {
  GObject               parent;

  GDesktopAppInfo      *app_info;
  gboolean              progress_visible;
  double                progress;
  gboolean              count_visible;
  gint64                count;
};
G_DEFINE_TYPE (PhoshLauncherItem, phosh_launcher_item, G_TYPE_OBJECT)


static void
phosh_launcher_item_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhoshLauncherItem *self = PHOSH_LAUNCHER_ITEM (object);

  switch (property_id) {
  case PROP_APP_INFO:
    g_set_object (&self->app_info, g_value_get_object (value));
    break;
  case PROP_PROGRESS_VISIBLE:
    phosh_launcher_item_set_progress_visible (self, g_value_get_boolean (value));
    break;
  case PROP_PROGRESS:
    phosh_launcher_item_set_progress (self, g_value_get_double (value));
    break;
  case PROP_COUNT_VISIBLE:
    phosh_launcher_item_set_count_visible (self, g_value_get_boolean (value));
    break;
  case PROP_COUNT:
    phosh_launcher_item_set_count (self, g_value_get_int64 (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_launcher_item_get_property (GObject    *object,
                      guint       property_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  PhoshLauncherItem *self = PHOSH_LAUNCHER_ITEM (object);

  switch (property_id) {
  case PROP_APP_INFO:
    g_value_set_object (value, self->app_info);
    break;
  case PROP_PROGRESS_VISIBLE:
    g_value_set_boolean (value, phosh_launcher_item_get_progress_visible (self));
    break;
  case PROP_PROGRESS:
    g_value_set_double (value, phosh_launcher_item_get_progress (self));
    break;
  case PROP_COUNT_VISIBLE:
    g_value_set_boolean (value, phosh_launcher_item_get_count_visible (self));
    break;
  case PROP_COUNT:
    g_value_set_int64 (value, phosh_launcher_item_get_count (self));
    break;
  case PROP_HAS_DATA:
    g_value_set_boolean (value, self->progress_visible || self->count_visible);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_launcher_item_dispose (GObject *object)
{
  PhoshLauncherItem *self = PHOSH_LAUNCHER_ITEM (object);

  g_clear_object (&self->app_info);

  G_OBJECT_CLASS (phosh_launcher_item_parent_class)->dispose (object);
}


static void
phosh_launcher_item_class_init (PhoshLauncherItemClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_launcher_item_get_property;
  object_class->set_property = phosh_launcher_item_set_property;
  object_class->dispose = phosh_launcher_item_dispose;

  props[PROP_APP_INFO] =
    g_param_spec_object ("app-info", "", "",
                         G_TYPE_DESKTOP_APP_INFO,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  props[PROP_PROGRESS_VISIBLE] =
    g_param_spec_boolean ("progress-visible", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_PROGRESS] =
    g_param_spec_double ("progress", "", "",
                         0.0, 1.0, 0.0,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_COUNT_VISIBLE] =
    g_param_spec_boolean ("count-visible", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_COUNT] =
    g_param_spec_int64 ("count", "", "",
                        0, G_MAXINT64, G_MAXINT64,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshLauncherItem:has-data:
   *
   * The launcher item wants to show data (either count, progress or both).
   */
  props[PROP_HAS_DATA] =
    g_param_spec_boolean ("has-data", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_launcher_item_init (PhoshLauncherItem *self)
{
}


PhoshLauncherItem *
phosh_launcher_item_new (GDesktopAppInfo *app_info)
{
  return g_object_new (PHOSH_TYPE_LAUNCHER_ITEM, "app-info", app_info, NULL);
}


gboolean
phosh_launcher_item_get_progress_visible (PhoshLauncherItem *self)
{
  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ITEM (self), FALSE);

  return self->progress_visible;
}


void
phosh_launcher_item_set_progress_visible (PhoshLauncherItem *self, gboolean visible)
{
  g_return_if_fail (PHOSH_IS_LAUNCHER_ITEM (self));

  if (self->progress_visible == visible)
    return;

  self->progress_visible = visible;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PROGRESS_VISIBLE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_DATA]);
}


double
phosh_launcher_item_get_progress (PhoshLauncherItem *self)
{
  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ITEM (self), 0.0);

  return self->progress;
}


void
phosh_launcher_item_set_progress (PhoshLauncherItem *self, double progress)
{
  g_return_if_fail (PHOSH_IS_LAUNCHER_ITEM (self));

  if (G_APPROX_VALUE (self->progress, progress, DBL_EPSILON))
    return;

  self->progress = progress;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PROGRESS]);
}


gboolean
phosh_launcher_item_get_count_visible (PhoshLauncherItem *self)
{
  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ITEM (self), FALSE);

  return self->count_visible;
}


void
phosh_launcher_item_set_count_visible (PhoshLauncherItem *self, gboolean visible)
{
  g_return_if_fail (PHOSH_IS_LAUNCHER_ITEM (self));

  if (self->count_visible == visible)
    return;

  self->count_visible = visible;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COUNT_VISIBLE]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_DATA]);
}


void
phosh_launcher_item_set_count (PhoshLauncherItem *self, gint64 count)
{
  g_return_if_fail (PHOSH_IS_LAUNCHER_ITEM (self));

  if (count == self->count)
    return;

  self->count = count;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COUNT]);
}


gint64
phosh_launcher_item_get_count (PhoshLauncherItem *self)
{
  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ITEM (self), 0);

  return self->count;
}


GDesktopAppInfo *
phosh_launcher_item_get_app_info (PhoshLauncherItem *self)
{
  g_return_val_if_fail (PHOSH_IS_LAUNCHER_ITEM (self), NULL);

  return self->app_info;
}
