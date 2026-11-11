/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "camera-status-icon.h"
#include "plugin-shell.h"

/**
 * PhoshCameraStatusIcon:
 *
 * A status icon that is shown while a camera is in use.
 */

struct _PhoshCameraStatusIcon {
  PhoshStatusIcon parent;
};

G_DEFINE_TYPE (PhoshCameraStatusIcon, phosh_camera_status_icon, PHOSH_TYPE_STATUS_ICON);


static void
phosh_camera_status_icon_realize (GtkWidget *widget)
{
  PhoshCameraManager *manager = phosh_shell_get_camera_manager (phosh_shell_get_default ());
  GtkWidget *gtkrevealer, *phoshrevealer;

  GTK_WIDGET_CLASS (phosh_camera_status_icon_parent_class)->realize (widget);

  /* We want to hide completely when cameras are off so hide the revealer embedding us */
  gtkrevealer = gtk_widget_get_parent (widget);
  phoshrevealer = gtk_widget_get_parent (gtkrevealer);

  g_object_bind_property (manager, "in-use", phoshrevealer, "show-child", G_BINDING_SYNC_CREATE);
}



static void
phosh_camera_status_icon_class_init (PhoshCameraStatusIconClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  widget_class->realize = phosh_camera_status_icon_realize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/camera-status-icon/si.ui");
}


static void
phosh_camera_status_icon_init (PhoshCameraStatusIcon *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}
