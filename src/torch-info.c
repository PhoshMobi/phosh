/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-torch-info"

#include "phosh-config.h"

#include "shell-priv.h"
#include "torch-info.h"
#include "torch-manager.h"

/**
 * PhoshTorchInfo:
 *
 * A widget to display the torch status
 *
 * #PhoshTorchInfo displays the current torch status based on information
 * from #PhoshTorchManager. To figure out if the widget should be shown
 * the #PhoshTorchInfo:enabled property can be useful.
 */

enum {
  PROP_0,
  PROP_ENABLED,
  PROP_PRESENT,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


struct _PhoshTorchInfo {
  PhoshStatusIcon    parent;

  gboolean           enabled;
  gboolean           present;
  PhoshTorchManager *torch;
};
G_DEFINE_TYPE (PhoshTorchInfo, phosh_torch_info, PHOSH_TYPE_STATUS_ICON);


static void
phosh_torch_info_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PhoshTorchInfo *self = PHOSH_TORCH_INFO (object);

  switch (property_id) {
  case PROP_ENABLED:
    g_value_set_boolean (value, self->enabled);
    break;
  case PROP_PRESENT:
    g_value_set_boolean (value, self->present);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
update_info (PhoshTorchInfo *self, GParamSpec *pspec, PhoshTorchManager *torch)
{
  g_return_if_fail (PHOSH_IS_TORCH_INFO (self));

  if (phosh_torch_manager_get_enabled (self->torch)) {
    g_autofree char *str = NULL;
    double frac = phosh_torch_manager_get_scaled_brightness (self->torch);

    /* If the flashlight can't scale brightness on means 100% */
    if (!phosh_torch_manager_get_can_scale (self->torch))
      frac = 1.0;

    str = g_strdup_printf ("%d%%", (int)(frac * 100.0));
    phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), str);
  } else {
    phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), _("Torch"));
  }
}


static void
on_torch_enabled (PhoshTorchInfo *self, GParamSpec *pspec, PhoshTorchManager *torch)
{
  gboolean enabled;

  g_return_if_fail (PHOSH_IS_TORCH_INFO (self));
  g_return_if_fail (PHOSH_IS_TORCH_MANAGER (torch));

  enabled = phosh_torch_manager_get_enabled (torch);
  if (self->enabled == enabled)
    return;

  self->enabled = enabled;
  g_debug ("Updating torch enabled: %d", enabled);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);
}


static void
on_torch_present (PhoshTorchInfo *self, GParamSpec *pspec, PhoshTorchManager *torch)
{
  gboolean present;

  g_return_if_fail (PHOSH_IS_TORCH_INFO (self));
  g_return_if_fail (PHOSH_IS_TORCH_MANAGER (torch));

  present = phosh_torch_manager_get_present (torch);
  if (self->present == present)
    return;

  self->present = present;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
phosh_torch_info_idle_init (PhoshStatusIcon *icon)
{
  PhoshTorchInfo *self = PHOSH_TORCH_INFO (icon);

  g_object_bind_property (self->torch, "icon-name", self, "icon-name",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect_swapped (self->torch,
                            "notify::brightness",
                            G_CALLBACK (update_info),
                            self);
  update_info (self, NULL, self->torch);

  /* We don't use a binding for self->enabled so we can keep
     the property r/o */
  g_signal_connect_swapped (self->torch,
                            "notify::enabled",
                            G_CALLBACK (on_torch_enabled),
                            self);
  on_torch_enabled (self, NULL, self->torch);

  g_signal_connect_swapped (self->torch,
                            "notify::present",
                            G_CALLBACK (on_torch_present),
                            self);
  on_torch_present (self, NULL, self->torch);
}


static void
phosh_torch_info_constructed (GObject *object)
{
  PhoshTorchInfo *self = PHOSH_TORCH_INFO (object);
  PhoshShell *shell;

  G_OBJECT_CLASS (phosh_torch_info_parent_class)->constructed (object);

  shell = phosh_shell_get_default ();
  self->torch = g_object_ref (phosh_shell_get_torch_manager (shell));

  if (self->torch == NULL) {
    g_warning ("Failed to get torch manager");
    return;
  }
}


static void
phosh_torch_info_dispose (GObject *object)
{
  PhoshTorchInfo *self = PHOSH_TORCH_INFO (object);

  if (self->torch) {
    g_signal_handlers_disconnect_by_data (self->torch, self);
    g_clear_object (&self->torch);
  }

  G_OBJECT_CLASS (phosh_torch_info_parent_class)->dispose (object);
}


static void
phosh_torch_info_class_init (PhoshTorchInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshStatusIconClass *status_icon_class = PHOSH_STATUS_ICON_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = phosh_torch_info_constructed;
  object_class->dispose = phosh_torch_info_dispose;
  object_class->get_property = phosh_torch_info_get_property;

  status_icon_class->idle_init = phosh_torch_info_idle_init;

  gtk_widget_class_set_css_name (widget_class, "phosh-torch-info");

  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          "enabled",
                          "Whether the torch is enabled",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_PRESENT] =
    g_param_spec_boolean ("present",
                          "Present",
                          "Whether torch hardware is present",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_torch_info_init (PhoshTorchInfo *self)
{
}


GtkWidget *
phosh_torch_info_new (void)
{
  return g_object_new (PHOSH_TYPE_TORCH_INFO, NULL);
}
