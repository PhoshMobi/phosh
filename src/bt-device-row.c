/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-bt-device-row"

#include "bt-device-row.h"
#include "shell-priv.h"

/**
 * PhoshBtDeviceRow:
 *
 * A widget to display a Bluetooth device
 */

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];


enum {
  ACTIVATING,
  DONE,
  N_SIGNALS
};
static guint signals[N_SIGNALS];


struct _PhoshBtDeviceRow {
  HdyActionRow      parent;

  GtkWidget        *icon;
  GtkSpinner       *spinner;
  GtkRevealer      *revealer;
  double            bat_percentage;

  BluetoothDevice  *device;

  GCancellable     *cancellable;
};

G_DEFINE_TYPE (PhoshBtDeviceRow, phosh_bt_device_row, HDY_TYPE_ACTION_ROW);


static void
bat_level_cb (PhoshBtDeviceRow *self, GParamSpec *pspec, BluetoothDevice *device)
{
  double current;
  gboolean connected;
  BluetoothBatteryType type;
  g_autofree char *subtitle = NULL;

  g_assert (PHOSH_IS_BT_DEVICE_ROW (self));
  g_assert (BLUETOOTH_IS_DEVICE (device));

  g_object_get (device,
                "battery-percentage", &current,
                "battery-type", &type,
                "connected", &connected,
                NULL);

  if (!connected || type != BLUETOOTH_BATTERY_TYPE_PERCENTAGE) {
    hdy_action_row_set_subtitle (HDY_ACTION_ROW (self), "");
    self->bat_percentage = -1.0;
    return;
  }

  current = round (current);
  if (G_APPROX_VALUE (round (self->bat_percentage), current, FLT_EPSILON))
    return;

  self->bat_percentage = current;
  /* Translators: a battery level in percent */
  subtitle = g_strdup_printf (_("Battery %.0f%%"), self->bat_percentage);
  hdy_action_row_set_subtitle (HDY_ACTION_ROW (self), subtitle);
}


static void
phosh_bt_device_row_set_device (PhoshBtDeviceRow *self, BluetoothDevice *device)
{
  g_set_object (&self->device, device);

  g_object_bind_property (self->device,
                          "connectable",
                          self,
                          "visible",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->device,
                          "alias",
                          self,
                          "title",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->device,
                          "icon",
                          self->icon,
                          "icon-name",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->device,
                          "connected",
                          self->revealer,
                          "reveal-child",
                          G_BINDING_DEFAULT | G_BINDING_SYNC_CREATE);

  g_object_connect (self->device,
                    "swapped-object-signal::notify::battery-percentage", bat_level_cb, self,
                    "swapped-object-signal::notify::battery-type", bat_level_cb, self,
                    "swapped-object-signal::notify::connected", bat_level_cb, self,
                    NULL);
  bat_level_cb (self, NULL, device);
}


static void
phosh_bt_device_row_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhoshBtDeviceRow *self = PHOSH_BT_DEVICE_ROW (object);

  switch (property_id) {
  case PROP_DEVICE:
    phosh_bt_device_row_set_device (self, g_value_dup_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_bt_device_row_dispose (GObject *object)
{
  PhoshBtDeviceRow *self = PHOSH_BT_DEVICE_ROW (object);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  g_clear_object (&self->device);

  G_OBJECT_CLASS (phosh_bt_device_row_parent_class)->dispose (object);
}


static void
on_connect_device_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (PhoshBtDeviceRow) self = PHOSH_BT_DEVICE_ROW (user_data);
  PhoshBtManager *manager = PHOSH_BT_MANAGER (source_object);
  g_autoptr (GError) err = NULL;
  gboolean success;

  success = phosh_bt_manager_connect_device_finish (manager, res, &err);
  if (!success)
    g_warning ("Failed to connect: %s", err->message);

  g_assert (PHOSH_IS_BT_DEVICE_ROW (user_data));

  gtk_spinner_stop (self->spinner);

  g_signal_emit (self, signals[DONE], 0, success);
}


static void
on_bt_row_activated (PhoshBtDeviceRow *self)
{
  PhoshBtManager *manager = phosh_shell_get_bt_manager (phosh_shell_get_default ());
  gboolean connected;
  g_autofree char *name = NULL;

  g_object_get (self->device, "connected", &connected, "alias", &name, NULL);

  g_cancellable_cancel (self->cancellable);
  g_set_object (&self->cancellable, g_cancellable_new ());

  g_debug ("%sonnecting device %s", !connected ? "C" : "Disc", name);

  gtk_spinner_start (self->spinner);
  phosh_bt_manager_connect_device_async (manager,
                                         self->device,
                                         !connected,
                                         on_connect_device_ready,
                                         self->cancellable,
                                         g_object_ref (self));
  g_signal_emit (self, signals[ACTIVATING], 0);
}


static void
phosh_bt_device_row_class_init (PhoshBtDeviceRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_bt_device_row_set_property;
  object_class->dispose = phosh_bt_device_row_dispose;

  /**
   * PhoshBtDeviceRow:device:
   *
   * The bluetooth device represented by the row
   */
  props[PROP_DEVICE] =
    g_param_spec_object ("device", "", "",
                         BLUETOOTH_TYPE_DEVICE,
                         G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/bt-device-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshBtDeviceRow, revealer);
  gtk_widget_class_bind_template_child (widget_class, PhoshBtDeviceRow, spinner);
  gtk_widget_class_bind_template_child (widget_class, PhoshBtDeviceRow, icon);

  gtk_widget_class_bind_template_callback (widget_class, on_bt_row_activated);

  /**
   * PhoshBtDeviceRow::activating
   *
   * The device is in the process of being activated / deactivated.
   */
  signals[ACTIVATING] = g_signal_new ("activating",
                                      G_TYPE_FROM_CLASS (klass),
                                      G_SIGNAL_RUN_LAST,
                                      0, NULL, NULL, NULL,
                                      G_TYPE_NONE,
                                      0);
  /**
   * PhoshBtDeviceRow::done
   * @self: The device row
   * @success: Whether the device (de)activation was successful
   *
   * The user selected the row and the activation finished.
   */
  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0, NULL, NULL, NULL,
                                G_TYPE_NONE,
                                1,
                                G_TYPE_BOOLEAN);
}


static void
phosh_bt_device_row_init (PhoshBtDeviceRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  /* Negative values mean "undefined" */
  self->bat_percentage = -1.0;
}


GtkWidget *
phosh_bt_device_row_new (BluetoothDevice *device)
{
  g_assert (BLUETOOTH_IS_DEVICE (device));

  return GTK_WIDGET (g_object_new (PHOSH_TYPE_BT_DEVICE_ROW, "device", device, NULL));
}
