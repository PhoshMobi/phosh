/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-cell-broadcast-manager"

#include "phosh-config.h"

#include "cell-broadcast-manager.h"
#include "cell-broadcast-prompt.h"
#include "feedback-manager.h"
#include "shell-priv.h"
#include "wwan/wwan-manager.h"

#include <gtk/gtk.h>

#define PHOSH_CELL_BROOADCAST_SCHEMA_ID "mobi.phosh.shell.cell-broadcast"

/**
 * PhoshCellBroadcastManager:
 *
 * Handles the display of Cell Broadcast messages
 *
 *  Since: 0.44.0
 */

enum {
  PROP_0,
  PROP_ENABLED,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshCellBroadcastManager {
  GObject    parent;

  gboolean   enabled;
  GSettings *settings;
};
G_DEFINE_TYPE (PhoshCellBroadcastManager, phosh_cell_broadcast_manager, G_TYPE_OBJECT)


static const char *
id_to_title (guint id)
{
  /* Translators: These strings aren't translated */
  switch (id) {
    /* TODO: Mapping by country */
  case 4383:
  case 4370:
    return "National Emergency Alert";
  case 4371:
    return "Emergency Alert";
  default:
    return "Cell broadcast";
  }
}


static void
on_new_cbm (PhoshCellBroadcastManager *self, const char *message, guint id)
{
  GtkWidget *prompt;

  g_debug ("New cbm: %s", message);

  if (!self->enabled)
    return;

  prompt = phosh_cell_broadcast_prompt_new (message, id_to_title (id));
  g_signal_connect (prompt, "closed", G_CALLBACK (gtk_widget_destroy), NULL);
  gtk_widget_set_visible (prompt, TRUE);
  phosh_trigger_feedback ("message-new-cellbroadcast", FALSE);
  phosh_shell_activate_action (phosh_shell_get_default (), "screensaver.wakeup-screen", NULL);

  /* We rely on the Chat application to remove the CBM later on */
}


static void
phosh_cell_broadcast_manager_set_property (GObject      *object,
                                           guint         property_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
  PhoshCellBroadcastManager *self = PHOSH_CELL_BROADCAST_MANAGER (object);

  switch (property_id) {
  case PROP_ENABLED:
    self->enabled = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_cell_broadcast_manager_get_property (GObject    *object,
                                           guint       property_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
  PhoshCellBroadcastManager *self = PHOSH_CELL_BROADCAST_MANAGER (object);

  switch (property_id) {
  case PROP_ENABLED:
    g_value_set_boolean (value, self->enabled);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_cell_broadcast_manager_finalize (GObject *object)
{
  PhoshCellBroadcastManager *self = PHOSH_CELL_BROADCAST_MANAGER (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_cell_broadcast_manager_parent_class)->finalize (object);
}


static void
phosh_cell_broadcast_manager_class_init (PhoshCellBroadcastManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_cell_broadcast_manager_get_property;
  object_class->set_property = phosh_cell_broadcast_manager_set_property;
  object_class->finalize = phosh_cell_broadcast_manager_finalize;

  /**
   * PhoshCellbroadcastmanager:enabled:
   *
   * Whether display of cell broadcast messages is enabled
   */
  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_cell_broadcast_manager_init (PhoshCellBroadcastManager *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshWWan *wwan = phosh_shell_get_wwan (shell);

  self->settings = g_settings_new (PHOSH_CELL_BROOADCAST_SCHEMA_ID);
  g_settings_bind (self->settings, "enabled", self, "enabled", G_SETTINGS_BIND_GET);

  g_signal_connect_object (wwan, "new-cbm", G_CALLBACK (on_new_cbm), self, G_CONNECT_SWAPPED);
}


PhoshCellBroadcastManager *
phosh_cell_broadcast_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_CELL_BROADCAST_MANAGER, NULL);
}
