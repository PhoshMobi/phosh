/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-bt-info"

#include "phosh-config.h"

#include "shell-priv.h"
#include "bt-info.h"
#include "bt-manager.h"

#include "gmobile.h"

/**
 * PhoshBtInfo:
 *
 * A widget to display the bluetooth status
 *
 * #PhoshBtInfo displays the current bluetooth status based on information
 * from #PhoshBtManager. To figure out if the widget should be shown
 * the #PhoshBtInfo:enabled property can be useful.
 */

enum {
  PROP_0,
  PROP_ENABLED,
  PROP_PRESENT,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

struct _PhoshBtInfo {
  PhoshStatusIcon parent;

  gboolean        enabled;
  gboolean        present;
  PhoshBtManager *bt;
};
G_DEFINE_TYPE (PhoshBtInfo, phosh_bt_info, PHOSH_TYPE_STATUS_ICON);


static void
phosh_bt_info_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PhoshBtInfo *self = PHOSH_BT_INFO (object);

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
update_icon (PhoshBtInfo *self, GParamSpec *pspec, PhoshBtManager *bt)
{
  const char *icon_name;

  g_return_if_fail (PHOSH_IS_BT_INFO (self));
  g_return_if_fail (PHOSH_IS_BT_MANAGER (bt));

  icon_name = phosh_bt_manager_get_icon_name (bt);
  g_debug ("Updating bt icon to %s", icon_name);
  if (icon_name)
    phosh_status_icon_set_icon_name (PHOSH_STATUS_ICON (self), icon_name);
}


static void
update_info (PhoshBtInfo *self)
{
  g_autofree char *msg = NULL;
  gboolean enabled;
  guint n_connected;

  g_return_if_fail (PHOSH_IS_BT_INFO (self));

  enabled = phosh_bt_manager_get_enabled (self->bt);
  if (!enabled) {
    phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), _("Bluetooth"));
    return;
  }

  n_connected = phosh_bt_manager_get_n_connected (self->bt);
  switch (n_connected) {
  case 0:
    break;
  case 1: {
    const char *info = phosh_bt_manager_get_info (self->bt);
    if (gm_str_is_null_or_empty (info)) {
      /* Translators: One connected Bluetooth device */
      msg = g_strdup_printf ("One device");
    } else {
      msg = g_strdup (info);
    }
    break;
  }
  default:
    /* Translators: The number of currently connected Bluetooth devices */
    msg = g_strdup_printf ("%d devices", n_connected);
  }

  if (msg) {
    phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), msg);
    return;
  }

  phosh_status_icon_set_info (PHOSH_STATUS_ICON (self), C_("bluetooth:enabled", "On"));
}


static void
on_bt_enabled (PhoshBtInfo *self, GParamSpec *pspec, PhoshBtManager *bt)
{
  gboolean enabled;

  g_return_if_fail (PHOSH_IS_BT_INFO (self));
  g_return_if_fail (PHOSH_IS_BT_MANAGER (bt));

  enabled = phosh_bt_manager_get_enabled (bt);
  g_debug ("Updating bt enabled %d", enabled);
  if (self->enabled == enabled)
    return;

  self->enabled = enabled;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);

  update_info (self);
}


static void
on_bt_present (PhoshBtInfo *self, GParamSpec *pspec, PhoshBtManager *bt)
{
  gboolean present;

  g_return_if_fail (PHOSH_IS_BT_INFO (self));
  g_return_if_fail (PHOSH_IS_BT_MANAGER (bt));

  present = phosh_bt_manager_get_present (bt);
  if (self->present == present)
    return;

  self->present = present;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
}


static void
phosh_bt_info_idle_init (PhoshStatusIcon *icon)
{
  PhoshBtInfo *self = PHOSH_BT_INFO (icon);

  update_icon (self, NULL, self->bt);
  update_info (self);
  on_bt_enabled (self, NULL, self->bt);
}


static void
phosh_bt_info_constructed (GObject *object)
{
  PhoshBtInfo *self = PHOSH_BT_INFO (object);
  PhoshShell *shell;

  G_OBJECT_CLASS (phosh_bt_info_parent_class)->constructed (object);

  shell = phosh_shell_get_default ();
  self->bt = g_object_ref (phosh_shell_get_bt_manager (shell));

  if (self->bt == NULL) {
    g_warning ("Failed to get bt manager");
    return;
  }

  g_object_connect (self->bt,
                    "swapped-signal::notify::icon-name", update_icon, self,
                    "swapped-signal::notify::enabled", on_bt_enabled, self,
                    "swapped-signal::notify::present", on_bt_present, self,
                    "swapped-signal::notify::n-connected", update_info, self,
                    NULL);
}


static void
phosh_bt_info_dispose (GObject *object)
{
  PhoshBtInfo *self = PHOSH_BT_INFO (object);

  if (self->bt) {
    g_signal_handlers_disconnect_by_data (self->bt, self);
    g_clear_object (&self->bt);
  }

  G_OBJECT_CLASS (phosh_bt_info_parent_class)->dispose (object);
}


static void
phosh_bt_info_class_init (PhoshBtInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PhoshStatusIconClass *status_icon_class = PHOSH_STATUS_ICON_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = phosh_bt_info_constructed;
  object_class->dispose = phosh_bt_info_dispose;
  object_class->get_property = phosh_bt_info_get_property;

  status_icon_class->idle_init = phosh_bt_info_idle_init;

  gtk_widget_class_set_css_name (widget_class, "phosh-bt-info");

  /**
   * PhoshBtInfo:enabled:
   *
   * Whether a Bluetooth adapter is enabled
   */
  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshBtInfo:present:
   *
   * Whether a Bluetooth adapter is present
   */
  props[PROP_PRESENT] =
    g_param_spec_boolean ("present", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_bt_info_init (PhoshBtInfo *self)
{
}


GtkWidget *
phosh_bt_info_new (void)
{
  return g_object_new (PHOSH_TYPE_BT_INFO, NULL);
}
