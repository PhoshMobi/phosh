/*
 * Copyright (C) 2024 Guido Günther
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-cell-broadcast-prompt"

#include "phosh-config.h"

#include "cell-broadcast-prompt.h"

#include <glib/gi18n.h>

/**
 * PhoshCellBroadcastPrompt:
 *
 * A modal prompt to display cell broadcast messages
 *
 * The #PhoshCellBroadcastPrompt is used to show cell
 * broadcast messages from the mobile network.
 *
 * Since it's about emergencies it can be shown above the
 * lock screen.
 */

enum {
  PROP_0,
  PROP_MESSAGE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


enum {
  CLOSED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };


struct _PhoshCellBroadcastPrompt {
  PhoshSystemModalDialog parent;

  GtkLabel              *lbl_msg;
  char                  *message;
};
G_DEFINE_TYPE (PhoshCellBroadcastPrompt, phosh_cell_broadcast_prompt, PHOSH_TYPE_SYSTEM_MODAL_DIALOG);


static void
set_message (PhoshCellBroadcastPrompt *self, const char *message)
{
  g_return_if_fail (PHOSH_IS_CELL_BROADCAST_PROMPT (self));

  if (!g_strcmp0 (self->message, message))
    return;

  g_clear_pointer (&self->message, g_free);
  self->message = g_strdup (message);

  g_strstrip (self->message);
  gtk_label_set_label (self->lbl_msg, self->message);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MESSAGE]);
}


static void
phosh_cell_broadcast_prompt_set_property (GObject      *obj,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  PhoshCellBroadcastPrompt *self = PHOSH_CELL_BROADCAST_PROMPT (obj);

  switch (prop_id) {
  case PROP_MESSAGE:
    set_message (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}


static void
phosh_cell_broadcast_prompt_get_property (GObject    *obj,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  PhoshCellBroadcastPrompt *self = PHOSH_CELL_BROADCAST_PROMPT (obj);

  switch (prop_id) {
  case PROP_MESSAGE:
    g_value_set_string (value, self->message ?: "");
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
    break;
  }
}


static void
on_ok_clicked (PhoshCellBroadcastPrompt *self)
{
  g_signal_emit (self, signals[CLOSED], 0);
}


static void
on_dialog_canceled (PhoshCellBroadcastPrompt *self)
{
  g_signal_emit (self, signals[CLOSED], 0);
}


static void
phosh_cell_broadcast_prompt_finalize (GObject *obj)
{
  PhoshCellBroadcastPrompt *self = PHOSH_CELL_BROADCAST_PROMPT (obj);

  g_free (self->message);

  G_OBJECT_CLASS (phosh_cell_broadcast_prompt_parent_class)->finalize (obj);
}


static void
phosh_cell_broadcast_prompt_class_init (PhoshCellBroadcastPromptClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_cell_broadcast_prompt_get_property;
  object_class->set_property = phosh_cell_broadcast_prompt_set_property;
  object_class->finalize = phosh_cell_broadcast_prompt_finalize;

  props[PROP_MESSAGE] =
    g_param_spec_string ("message", "", "",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[CLOSED] = g_signal_new ("closed",
                                  G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                  NULL, G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/cell-broadcast-prompt.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshCellBroadcastPrompt, lbl_msg);
  gtk_widget_class_bind_template_callback (widget_class, on_dialog_canceled);
  gtk_widget_class_bind_template_callback (widget_class, on_ok_clicked);

  gtk_widget_class_set_css_name (widget_class, "phosh-cell-broadcast-prompt");
}


static void
phosh_cell_broadcast_prompt_init (PhoshCellBroadcastPrompt *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_cell_broadcast_prompt_new (const char *message, const char *title)
{
  return g_object_new (PHOSH_TYPE_CELL_BROADCAST_PROMPT,
                       "message", message,
                       "title", title,
                       NULL);
}
