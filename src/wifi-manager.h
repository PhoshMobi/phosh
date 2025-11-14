/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include <NetworkManager.h>
#include <wifi-network.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_WIFI_MANAGER (phosh_wifi_manager_get_type())

G_DECLARE_FINAL_TYPE (PhoshWifiManager, phosh_wifi_manager, PHOSH, WIFI_MANAGER, GObject)

PhoshWifiManager  *phosh_wifi_manager_new (void);
guint8             phosh_wifi_manager_get_strength (PhoshWifiManager *self);
const char        *phosh_wifi_manager_get_icon_name (PhoshWifiManager *self);
const char        *phosh_wifi_manager_get_ssid (PhoshWifiManager *self);
gboolean           phosh_wifi_manager_get_enabled (PhoshWifiManager *self);
void               phosh_wifi_manager_set_enabled (PhoshWifiManager *self, gboolean enabled);
gboolean           phosh_wifi_manager_get_present (PhoshWifiManager *self);
GListStore        *phosh_wifi_manager_get_networks (PhoshWifiManager *self);
gboolean           phosh_wifi_manager_is_hotspot_master (PhoshWifiManager *self);
void               phosh_wifi_manager_set_hotspot_master (PhoshWifiManager *self, gboolean is_hotspot_master);
void               phosh_wifi_manager_connect_network (PhoshWifiManager *self,
                                                       PhoshWifiNetwork *network);
void               phosh_wifi_manager_request_scan (PhoshWifiManager *self);
gboolean           phosh_wifi_manager_get_scanning (PhoshWifiManager *self);
NMActiveConnectionState phosh_wifi_manager_get_state (PhoshWifiManager *self);
NMActiveConnection     *phosh_wifi_manager_get_active_connection (PhoshWifiManager *self);

G_END_DECLS
