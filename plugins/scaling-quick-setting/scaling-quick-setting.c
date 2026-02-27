/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Adam Honse <calcprogrammer1@gmail.com>
 *         Guido Günther <agx@sigxcpu.org>
 */

#include "scaling-quick-setting.h"
#include "plugin-shell.h"
#include "scale-row.h"
#include "util.h"

#include <glib/gi18n.h>

/**
 * PhoshScalingQuickSetting:
 *
 * A quick setting for adjusting display scaling.
 */

struct _PhoshScalingQuickSetting {
  PhoshQuickSetting parent;

  GtkListBox       *list_box;
  PhoshStatusPage  *status_page;

  double            scale;
  double            last_scale;
  PhoshStatusIcon  *info;
  PhoshMonitor     *monitor;
  guint             current_mode;
};

G_DEFINE_TYPE (PhoshScalingQuickSetting, phosh_scaling_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static gboolean
transform_to_sensitive (GBinding     *binding,
                        const GValue *from_value,
                        GValue       *to_value,
                        gpointer      user_data)
{
  guint n_monitors = g_value_get_int (from_value);

  g_value_set_boolean (to_value, n_monitors == 1);
  return TRUE;
}


static void
fill_scales (PhoshScalingQuickSetting *self, guint32 width, guint32 height)
{
  int n_scales;
  g_autoptr (GList) children = NULL;
  g_autofree float *scales = NULL;

  scales = phosh_util_calculate_supported_mode_scales (width, height, &n_scales, TRUE);

  children = gtk_container_get_children (GTK_CONTAINER (self->list_box));
  for (GList *child = children; child; child = child->next)
    gtk_container_remove (GTK_CONTAINER (self->list_box), child->data);

  for (int i = 0; i < n_scales; i++) {
    gboolean selected = G_APPROX_VALUE (self->scale, scales[i], FLT_EPSILON);
    GtkWidget *row = phosh_scale_row_new (scales[i], selected);

    gtk_list_box_insert (self->list_box, row, -1);
  }
}


static void
on_monitor_configured (PhoshScalingQuickSetting *self, PhoshMonitor *monitor)
{
  double scale;

  g_assert (PHOSH_IS_SCALING_QUICK_SETTING (self));
  g_assert (PHOSH_IS_MONITOR (monitor));

  scale = phosh_monitor_get_fractional_scale (monitor);
  if (!G_APPROX_VALUE (self->scale, scale, FLT_EPSILON)) {
    const char *icon_name;
    g_autofree char *label = NULL;

    self->last_scale = self->scale;
    self->scale = scale;

    /* Translators: This is scale factor of a monitor in percent */
    label = g_strdup_printf (_("%d%%"), (int)round(scale * 100));
    phosh_status_icon_set_info (self->info, label);

    icon_name = self->scale < 2.0 ? "screen-scaling-small-symbolic" :
      "screen-scaling-large-symbolic";
    phosh_status_icon_set_icon_name (self->info, icon_name);
  }

  fill_scales (self, monitor->width, monitor->height);
}


static void
set_primary_monitor (PhoshScalingQuickSetting *self, PhoshMonitor *monitor)
{
  g_assert (PHOSH_IS_SCALING_QUICK_SETTING (self));
  g_assert (PHOSH_IS_MONITOR (monitor));

  if (self->monitor == monitor)
    return;

  if (self->monitor) {
    g_signal_handlers_disconnect_by_data (self->monitor, self);
    g_object_remove_weak_pointer (G_OBJECT (self->monitor), (gpointer *)&self->monitor);
    self->current_mode = -1;
    self->last_scale = 1.0;
  }

  self->monitor = monitor;

  if (self->monitor) {
    g_object_add_weak_pointer (G_OBJECT (self->monitor), (gpointer *)&self->monitor);
    g_signal_connect_object (self->monitor,
                             "configured",
                             G_CALLBACK (on_monitor_configured),
                             self,
                             G_CONNECT_SWAPPED);
  }
}


static void
on_primary_monitor_changed (PhoshScalingQuickSetting *self, GParamSpec *pspec, PhoshShell *shell)
{
  set_primary_monitor (self, phosh_shell_get_primary_monitor (shell));
}


static void
set_scale (PhoshScalingQuickSetting *self, double scale)
{
  PhoshMonitorManager *monitor_manager;

  monitor_manager = phosh_shell_get_monitor_manager (phosh_shell_get_default ());
  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (monitor_manager));
  g_return_if_fail (PHOSH_IS_MONITOR (self->monitor));
  g_return_if_fail (scale >= 0.0);

  g_debug ("Setting monior scale to %f", scale);

  phosh_monitor_manager_set_monitor_scale (monitor_manager, self->monitor, scale);
  phosh_monitor_manager_apply_monitor_config (monitor_manager);
}


static void
on_clicked (PhoshScalingQuickSetting *self)
{
  double scale;

  scale = self->last_scale >= 1.0 ? self->last_scale : 1.0;
  set_scale (self, scale);
}


static void
on_scale_row_activated (PhoshScalingQuickSetting *self, GtkListBoxRow *row)
{
  PhoshScaleRow *scale_row = PHOSH_SCALE_ROW (row);
  double scale;

  scale = phosh_scale_row_get_scale (scale_row);
  set_scale (self, scale);
  g_signal_emit_by_name (self->status_page, "done", TRUE);
}


static void
phosh_scaling_quick_setting_class_init (PhoshScalingQuickSettingClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/scaling-quick-setting/icons");

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/scaling-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshScalingQuickSetting, info);
  gtk_widget_class_bind_template_child (widget_class, PhoshScalingQuickSetting, list_box);
  gtk_widget_class_bind_template_child (widget_class, PhoshScalingQuickSetting, status_page);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_scale_row_activated);
}


static void
phosh_scaling_quick_setting_init (PhoshScalingQuickSetting *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshMonitorManager *monitor_manager;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->last_scale = 1.0;
  self->current_mode = -1;

  monitor_manager = phosh_shell_get_monitor_manager (shell);
  g_object_bind_property_full (monitor_manager, "n-monitors",
                               self, "sensitive",
                               G_BINDING_SYNC_CREATE,
                               transform_to_sensitive,
                               NULL, NULL, NULL);

  g_signal_connect_object (shell,
                           "notify::primary-monitor",
                           G_CALLBACK (on_primary_monitor_changed),
                           self,
                           G_CONNECT_SWAPPED);

  /* Catch up with current monitor */
  on_primary_monitor_changed (self, NULL, shell);
  if (self->monitor)
    on_monitor_configured (self, self->monitor);
}
