/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "simple-custom-status-icon.h"

#define INTERVAL 10

/**
 * PhoshSimpleCustomStatusIcon:
 *
 * A simple custom status-icon for demonstration purposes.
 */

static char *ICON_NAMES[] = {"face-angel-symbolic", "face-angry-symbolic", "face-cool-symbolic",
                             "face-devilish-symbolic", "face-smile-symbolic",
                             "face-smirk-symbolic"};

struct _PhoshSimpleCustomStatusIcon {
  PhoshStatusIcon parent;

  guint timeout_id;
};

G_DEFINE_TYPE (PhoshSimpleCustomStatusIcon, phosh_simple_custom_status_icon,
               PHOSH_TYPE_STATUS_ICON);


static gboolean
on_timeout (gpointer data)
{
  PhoshSimpleCustomStatusIcon *self = data;
  int idx = g_random_int_range (0, sizeof (ICON_NAMES) / sizeof (char *));

  g_debug ("Changing icon name to %s", ICON_NAMES[idx]);
  phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self), ICON_NAMES[idx]);

  return G_SOURCE_CONTINUE;
}


static void
phosh_simple_custom_status_icon_destroy (GtkWidget *widget)
{
  PhoshSimpleCustomStatusIcon *self = PHOSH_SIMPLE_CUSTOM_STATUS_ICON (widget);

  if (self->timeout_id != 0) {
    g_source_remove (self->timeout_id);
    self->timeout_id = 0;
  }

  GTK_WIDGET_CLASS (phosh_simple_custom_status_icon_parent_class)->destroy (widget);
}


static void
phosh_simple_custom_status_icon_class_init (PhoshSimpleCustomStatusIconClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->destroy = phosh_simple_custom_status_icon_destroy;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/simple-custom-status-icon/si.ui");
}


static void
phosh_simple_custom_status_icon_init (PhoshSimpleCustomStatusIcon *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  self->timeout_id = g_timeout_add_seconds (INTERVAL, on_timeout, self);
  on_timeout (self);
}
