/*
 * Copyright (C) 2023 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arunmani@peartree.to>
 */

#define G_LOG_DOMAIN "phosh-wifi-network-row"

#include "wifi-network-row.h"

/**
 * PhoshWifiNetworkRow:
 *
 * A widget to display a `PhoshWifiNetwork`.
 */

enum {
  PROP_0,
  PROP_NETWORK,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshWifiNetworkRow {
  HdyActionRow      parent;

  GtkWidget        *wifi_icon;
  GtkWidget        *encrypted_icon;
  GtkWidget        *active_indicator;

  PhoshWifiNetwork *network;
};

G_DEFINE_TYPE (PhoshWifiNetworkRow, phosh_wifi_network_row, HDY_TYPE_ACTION_ROW);

static gboolean
update_icon (GBinding     *binding,
             const GValue *from_value,
             GValue       *to_value,
             gpointer      user_data)
{
  PhoshWifiNetworkRow *self = PHOSH_WIFI_NETWORK_ROW (user_data);
  guint strength = phosh_wifi_network_get_strength (self->network);
  gboolean is_connecting = phosh_wifi_network_get_is_connecting (self->network);
  const char *icon_name = phosh_util_get_icon_by_wifi_strength (strength, is_connecting);

  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static void
set_network (PhoshWifiNetworkRow *self, PhoshWifiNetwork *network)
{
  g_assert (self->network == NULL);
  self->network = g_object_ref (network);

  g_object_bind_property (self->network, "active",
                          self->active_indicator, "visible",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->network, "ssid",
                          self, "title",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property (self->network, "secured",
                          self->encrypted_icon, "visible",
                          G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (self->network, "strength",
                               self->wifi_icon, "icon_name",
                               G_BINDING_SYNC_CREATE,
                               update_icon,
                               NULL,
                               self,
                               NULL);

  g_object_bind_property_full (self->network, "is-connecting",
                               self->wifi_icon, "icon_name",
                               G_BINDING_SYNC_CREATE,
                               update_icon,
                               NULL,
                               self,
                               NULL);
}


static void
phosh_wifi_network_row_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  PhoshWifiNetworkRow *self = PHOSH_WIFI_NETWORK_ROW (object);

  switch (property_id) {
  case PROP_NETWORK:
    set_network (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_wifi_network_row_dispose (GObject *object)
{
  PhoshWifiNetworkRow *self = PHOSH_WIFI_NETWORK_ROW (object);

  g_clear_object (&self->network);

  G_OBJECT_CLASS (phosh_wifi_network_row_parent_class)->dispose (object);
}


static void
phosh_wifi_network_row_class_init (PhoshWifiNetworkRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_wifi_network_row_set_property;
  object_class->dispose = phosh_wifi_network_row_dispose;

  /**
   * PhoshWifiNetworkRow:network:
   *
   * Network represented by the row
   */
  props[PROP_NETWORK] =
    g_param_spec_object ("network", "", "",
                         PHOSH_TYPE_WIFI_NETWORK,
                         G_PARAM_WRITABLE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/wifi-network-row.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshWifiNetworkRow, wifi_icon);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiNetworkRow, encrypted_icon);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiNetworkRow, active_indicator);
}


static void
phosh_wifi_network_row_init (PhoshWifiNetworkRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_wifi_network_row_new (PhoshWifiNetwork *network)
{
  g_assert (PHOSH_IS_WIFI_NETWORK (network));
  return GTK_WIDGET (g_object_new (PHOSH_TYPE_WIFI_NETWORK_ROW, "network", network, NULL));
}
