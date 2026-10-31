/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ssh-quick-setting.h"
#include "plugin-shell.h"
#include "status-icon.h"
#include "util.h"

#include <glib/gi18n.h>

/* Unit names are not consistent across distros */
static const char * const SSH_UNITS[] = { "ssh.service", "sshd.service" };

/**
 * PhoshSshQuickSetting:
 *
 * Start and stop the ssh server via systemd.
 *
 * Active state follows systemd unit state. Quick setting is
 * insensitive when no (unmasked) ssh unit exists.
 */

struct _PhoshSshQuickSetting {
  PhoshQuickSetting parent;

  PhoshStatusIcon  *info;

  GDBusProxy       *unit;
  guint unit_idx;
  GCancellable     *cancel;
  gboolean          busy;
};

G_DEFINE_TYPE (PhoshSshQuickSetting, phosh_ssh_quick_setting, PHOSH_TYPE_QUICK_SETTING);


static void
update_state (PhoshSshQuickSetting *self)
{
  PhoshShell *shell = phosh_shell_get_default ();
  gboolean active = FALSE;
  const char *info = _("SSH Unavailable");

  if (self->unit) {
    active = phosh_util_systemd_unit_is_active (self->unit);
    info = active ? _("SSH On") : _("SSH Off");
  }

  phosh_quick_setting_set_active (PHOSH_QUICK_SETTING (self), active);
  phosh_status_icon_set_info (self->info, info);
  phosh_status_icon_set_icon_name (self->info,
                                   active ? "ssh-on-symbolic" : "ssh-off-symbolic");
  gtk_widget_set_sensitive (GTK_WIDGET (self),
                            self->unit != NULL && !self->busy && !phosh_shell_get_locked (shell));
}


static void
on_set_active_ready (GObject *source, GAsyncResult *res, gpointer data)
{
  PhoshSshQuickSetting *self = data;
  g_autoptr (GError) err = NULL;

  if (!phosh_util_systemd_unit_set_active_finish (G_DBUS_PROXY (source), res, &err)) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;
    g_warning ("Failed to toggle ssh server: %s", err->message);
  }

  self->busy = FALSE;
  update_state (self);
}


static void
on_clicked (PhoshSshQuickSetting *self)
{
  if (self->unit == NULL || self->busy)
    return;

  self->busy = TRUE;
  update_state (self);

  phosh_util_systemd_unit_set_active (self->unit,
                                      !phosh_util_systemd_unit_is_active (self->unit),
                                      self->cancel,
                                      on_set_active_ready,
                                      self);
}


static void
on_unit_properties_changed (PhoshSshQuickSetting *self)
{
  update_state (self);
}


static void on_unit_ready (GObject *source, GAsyncResult *res, gpointer data);


static void
load_unit (PhoshSshQuickSetting *self)
{
  phosh_util_systemd_unit_new (SSH_UNITS[self->unit_idx], self->cancel, on_unit_ready, self);
}


static void
on_unit_ready (GObject *source, GAsyncResult *res, gpointer data)
{
  PhoshSshQuickSetting *self = data;
  g_autoptr (GError) err = NULL;
  g_autoptr (GDBusProxy) unit = NULL;

  unit = phosh_util_systemd_unit_new_finish (res, &err);
  if (unit == NULL) {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;

    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_debug ("No %s: %s", SSH_UNITS[self->unit_idx], err->message);
      self->unit_idx++;
      if (self->unit_idx < G_N_ELEMENTS (SSH_UNITS))
        load_unit (self);
    } else if (g_error_matches (err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN)) {
      g_debug ("No systemd: %s", err->message);
    } else {
      g_warning ("Failed to look up %s: %s", SSH_UNITS[self->unit_idx], err->message);
    }
    return;
  }

  self->unit = g_steal_pointer (&unit);
  g_debug ("Using %s", SSH_UNITS[self->unit_idx]);
  g_signal_connect_swapped (self->unit, "g-properties-changed",
                            G_CALLBACK (on_unit_properties_changed), self);
  update_state (self);
}


static void
phosh_ssh_quick_setting_dispose (GObject *object)
{
  PhoshSshQuickSetting *self = PHOSH_SSH_QUICK_SETTING (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->unit);

  G_OBJECT_CLASS (phosh_ssh_quick_setting_parent_class)->dispose (object);
}


static void
phosh_ssh_quick_setting_class_init (PhoshSshQuickSettingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_ssh_quick_setting_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/ssh-quick-setting/qs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshSshQuickSetting, info);

  gtk_widget_class_bind_template_callback (widget_class, on_clicked);
}


static void
phosh_ssh_quick_setting_init (PhoshSshQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/ssh-quick-setting/icons");

  self->cancel = g_cancellable_new ();

  g_signal_connect_object (phosh_shell_get_default (),
                           "notify::locked",
                           G_CALLBACK (update_state),
                           self,
                           G_CONNECT_SWAPPED);

  /* Unavailable until we found a unit */
  update_state (self);
  load_unit (self);
}
