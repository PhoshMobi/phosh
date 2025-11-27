/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#define G_LOG_DOMAIN "phosh-wifi-manager"

#include "phosh-config.h"

#include "wifi-manager.h"
#include "util.h"

#include <NetworkManager.h>

/**
 * PhoshWifiManager:
 *
 * Tracks the Wi-Fi status and handle Wi-Fi credentials entry
 *
 * Manages Wi-Fi information and state
 *
 * The code to create hotspot connection are based on GNOME Control Center's and NMCLI's code for
 * the same.
 */

enum {
  PROP_0,
  PROP_ICON_NAME,
  PROP_SSID,
  PROP_ENABLED,
  PROP_PRESENT,
  PROP_IS_HOTSPOT_MASTER,
  PROP_NETWORKS,
  PROP_STATE,
  PROP_SCANNING,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshWifiManager {
  GObject             parent;

  /* Is Wi-Fi radio on (rfkill off) */
  gboolean            enabled;
  /* Whether we have a Wi-Fi device at all (independent from the
   * connection state */
  gboolean            present;
  gboolean            is_hotspot_master;

  const char         *icon_name;
  char               *ssid;

  gboolean            scanning;
  guint               scanning_id;
  gint64              last_scan;

  NMClient           *nmclient;
  GCancellable       *cancel;

  /* The access point we're connected to */
  NMAccessPoint      *ap;
  /* The active connection (if it has a Wi-Fi device) */
  NMActiveConnection *active;
  /* The state of the active connection */
  NMActiveConnectionState state;
  /* The Wi-Fi device used in the active connection */
  NMDeviceWifi       *conn_dev;
  /* The Wi-Fi device of the system */
  NMDeviceWifi       *dev;
  /* The list of available Wi-Fi networks */
  GListStore         *networks; /* (element-type: PhoshWifiNetwork) */
};
G_DEFINE_TYPE (PhoshWifiManager, phosh_wifi_manager, G_TYPE_OBJECT);


static gboolean
is_active_connection_hotspot_master (PhoshWifiManager *self)
{
  NMSettingIPConfig *ip4_setting;
  NMConnection *c;

  if (!self->conn_dev || !self->active ||
      nm_active_connection_get_state (self->active) != NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
    return FALSE;
  }

  c = NM_CONNECTION (nm_active_connection_get_connection (self->active));
  ip4_setting = nm_connection_get_setting_ip4_config (c);

  if (ip4_setting &&
      g_strcmp0 (nm_setting_ip_config_get_method (ip4_setting),
                 NM_SETTING_IP4_CONFIG_METHOD_SHARED) == 0) {
    return TRUE;
  }

  return FALSE;
}


static gboolean
check_is_hotspot_master (PhoshWifiManager *self)
{
  gboolean is_hotspot_master = is_active_connection_hotspot_master (self);

  if (is_hotspot_master != self->is_hotspot_master) {
    self->is_hotspot_master = is_hotspot_master;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_HOTSPOT_MASTER]);
  }

  return self->is_hotspot_master;
}

static const char *
get_icon_name (PhoshWifiManager *self)
{
  NMActiveConnectionState state;
  guint8 strength;

  if (!self->conn_dev) {
    if (self->enabled && self->present)
      return "network-wireless-offline-symbolic";
    return "network-wireless-disabled-symbolic";
  }

  state = nm_active_connection_get_state (self->active);

  switch (state) {
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATING:
    return "network-wireless-acquiring-symbolic";
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATED:
    if (self->is_hotspot_master) {
      return "network-wireless-hotspot-symbolic";
    } else if (!self->ap) {
      return "network-wireless-connected-symbolic";
    } else {
      strength = phosh_wifi_manager_get_strength (self);
      return phosh_util_get_icon_by_wifi_strength (strength, FALSE);
    }
  case NM_ACTIVE_CONNECTION_STATE_UNKNOWN:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATING:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATED:
    return "network-wireless-offline-symbolic";
  default:
    return "network-wireless-disabled-symbolic";
  }
}


static void
update_icon_name (PhoshWifiManager *self)
{
  const char *old_icon_name;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));

  old_icon_name = self->icon_name;
  self->icon_name = get_icon_name (self);

  if (g_strcmp0 (self->icon_name, old_icon_name) != 0)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}


/* Update enabled property based on nm's state and Wi-Fi device availability */
static void
update_enabled (PhoshWifiManager *self)
{
  gboolean enabled;

  g_return_if_fail (NM_IS_CLIENT (self->nmclient));
  enabled = nm_client_wireless_get_enabled (self->nmclient) && self->present;
  g_debug ("NM Wi-Fi enabled: %d, present: %d", enabled, self->present);

  if (enabled != self->enabled) {
    self->enabled = enabled;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ENABLED]);
  }
}


static void
update_properties (PhoshWifiManager *self)
{
  update_enabled (self);
  check_is_hotspot_master (self);
  update_icon_name (self);
}


static void
on_connection_state_changed (NMActiveConnection           *connection,
                             NMActiveConnectionState       state,
                             NMActiveConnectionStateReason reason,
                             gpointer                      data)
{
  PhoshWifiNetwork *network = PHOSH_WIFI_NETWORK (data);

  if (state != NM_ACTIVE_CONNECTION_STATE_ACTIVATING)
    phosh_wifi_network_set_is_connecting (network, FALSE);

  switch (state) {
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATED:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATED:
    g_signal_handlers_disconnect_by_func (connection, on_connection_state_changed, network);
    g_object_unref (connection);
    break;
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATING:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATING:
  case NM_ACTIVE_CONNECTION_STATE_UNKNOWN:
  default:
    break;
  }
}


static void
on_hotspot_connection_add_and_activated (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      data)
{
  NMClient *client = NM_CLIENT (object);
  g_autoptr (GError) err = NULL;
  g_autoptr (NMActiveConnection) conn = NULL;

  conn = nm_client_add_and_activate_connection_finish (client, result, &err);
  if (conn != NULL)
    g_debug ("Adding and activating hotspot connection");
  else
    g_warning ("Failed to add and activate hotspot connection: %s", err->message);
}


static void
on_hotspot_connection_activated (GObject      *object,
                                 GAsyncResult *result,
                                 gpointer      data)
{
  NMClient *client = NM_CLIENT (object);
  g_autoptr (GError) err = NULL;
  g_autoptr (NMActiveConnection) conn = NULL;

  conn = nm_client_activate_connection_finish (client, result, &err);
  if (conn != NULL)
    g_debug ("Activating hotspot connection");
  else
    g_warning ("Failed to activate hotspot connection: %s", err->message);
}


static char *
generate_wpa_key (void)
{
  int length = 8;
  GString *key = g_string_new (NULL);

  for (int i = 0; i < length; i++) {
    int c = 0;
    /* too many non alphanumeric characters are hard to remember for humans */
    while (!g_ascii_isalnum (c))
      c = g_random_int_range (33, 126);
    g_string_append_c (key, c);
  }

  return g_string_free_and_steal (key);
}


static char *
generate_wep_key (void)
{
  int length = 10;
  const char *hexdigits = "0123456789abcdef";
  GString *key = g_string_new (NULL);

  for (int i = 0; i < length; i++) {
    int digit = g_random_int_range (0, 16);
    g_string_append_c (key, hexdigits[digit]);
  }

  return g_string_free_and_steal (key);
}


static NMConnection *
create_hotspot_connection (const char              *con_name,
                           const char              *wifi_mode,
                           NMDeviceWifiCapabilities caps)
{
  NMConnection *connection;
  NMSetting *s_con;
  NMSetting *s_wifi;
  NMSettingWirelessSecurity *s_wsec;
  NMSetting *s_ip4, *s_ip6;
  NMSetting *s_proxy;
  g_autofree char *hostname = NULL;
  g_autoptr (GBytes) ssid = NULL;
  g_autofree char *key = NULL;
  const char *key_mgmt;

  g_return_val_if_fail (wifi_mode != NULL, NULL);

  hostname = g_hostname_to_ascii (g_get_host_name ());
  ssid = g_bytes_new (hostname, strlen (hostname));

  connection = nm_simple_connection_new ();
  s_con = nm_setting_connection_new ();
  nm_connection_add_setting (connection, s_con);
  g_object_set (s_con,
                NM_SETTING_CONNECTION_ID,
                con_name,
                NM_SETTING_CONNECTION_AUTOCONNECT,
                FALSE,
                NULL);

  s_wifi = nm_setting_wireless_new ();
  nm_connection_add_setting (connection, s_wifi);
  g_object_set (s_wifi,
                NM_SETTING_WIRELESS_MODE,
                wifi_mode,
                NM_SETTING_WIRELESS_SSID,
                ssid,
                NULL);

  s_wsec = NM_SETTING_WIRELESS_SECURITY (nm_setting_wireless_security_new ());
  nm_connection_add_setting (connection, NM_SETTING (s_wsec));

  s_ip4 = nm_setting_ip4_config_new ();
  nm_connection_add_setting (connection, s_ip4);
  g_object_set (s_ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_SHARED, NULL);

  s_ip6 = nm_setting_ip6_config_new ();
  nm_connection_add_setting (connection, s_ip6);
  g_object_set (s_ip6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_IGNORE, NULL);

  s_proxy = nm_setting_proxy_new ();
  nm_connection_add_setting (connection, s_proxy);
  g_object_set (s_proxy, NM_SETTING_PROXY_METHOD, NM_SETTING_PROXY_METHOD_NONE, NULL);

  if (g_str_equal (wifi_mode, NM_SETTING_WIRELESS_MODE_AP)) {
    if (caps & NM_WIFI_DEVICE_CAP_RSN) {
      nm_setting_wireless_security_add_proto (s_wsec, "rsn");
      nm_setting_wireless_security_add_pairwise (s_wsec, "ccmp");
      nm_setting_wireless_security_add_group (s_wsec, "ccmp");
      key_mgmt = "wpa-psk";
    } else if (caps & NM_WIFI_DEVICE_CAP_WPA) {
      nm_setting_wireless_security_add_proto (s_wsec, "wpa");
      nm_setting_wireless_security_add_pairwise (s_wsec, "tkip");
      nm_setting_wireless_security_add_group (s_wsec, "tkip");
      key_mgmt = "wpa-psk";
    } else {
      key_mgmt = "none";
    }
  } else {
    key_mgmt = "none";
  }

  if (g_str_equal (key_mgmt, "wpa-psk")) {
    /* use WPA */
    key = generate_wpa_key ();
    g_object_set (s_wsec,
                  NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
                  key_mgmt,
                  NM_SETTING_WIRELESS_SECURITY_PSK,
                  key,
                  NULL);
  } else {
    /* use WEP */
    key = generate_wep_key ();
    g_object_set (s_wsec,
                  NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
                  key_mgmt,
                  NM_SETTING_WIRELESS_SECURITY_WEP_KEY0,
                  key,
                  NM_SETTING_WIRELESS_SECURITY_WEP_KEY_TYPE,
                  NM_WEP_KEY_TYPE_KEY,
                  NULL);
  }

  return connection;
}


static void
start_hotspot (PhoshWifiManager *self)
{
  NMDeviceWifiCapabilities caps;
  const char *wifi_mode;
  const GPtrArray *connections;
  NMConnection *hotspot_conn = NULL;
  NMSettingWireless *s_wifi;

  caps = nm_device_wifi_get_capabilities (self->dev);
  if (caps & NM_WIFI_DEVICE_CAP_AP) {
    wifi_mode = NM_SETTING_WIRELESS_MODE_AP;
  } else if (caps & NM_WIFI_DEVICE_CAP_ADHOC) {
    wifi_mode = NM_SETTING_WIRELESS_MODE_ADHOC;
  } else {
    g_message ("Device does not support AP or Ad-Hoc mode");
    return;
  }

  connections = nm_client_get_connections (self->nmclient);
  for (int i = 0; i < connections->len; i++) {
    NMConnection *conn = g_ptr_array_index (connections, i);
    s_wifi = nm_connection_get_setting_wireless (conn);

    if (!s_wifi)
      continue;

    if (g_strcmp0 (nm_setting_wireless_get_mode (s_wifi), wifi_mode) != 0)
      continue;

    if (!nm_device_connection_compatible (NM_DEVICE (self->dev), conn, NULL))
      continue;

    hotspot_conn = conn;
    break;
  }

  if (!hotspot_conn) {
    g_autoptr (NMConnection) new_hotspot_conn;
    g_message ("Creating a new hotspot connection as no existing connection was found");
    new_hotspot_conn = create_hotspot_connection ("Phosh Hotspot", wifi_mode, caps);
    nm_client_add_and_activate_connection_async (self->nmclient,
                                                 new_hotspot_conn,
                                                 NM_DEVICE (self->dev),
                                                 NULL,
                                                 self->cancel,
                                                 on_hotspot_connection_add_and_activated,
                                                 NULL);
  } else {
    nm_client_activate_connection_async (self->nmclient,
                                         hotspot_conn,
                                         NM_DEVICE (self->dev),
                                         NULL,
                                         self->cancel,
                                         on_hotspot_connection_activated,
                                         NULL);
  }
}


static void
on_hotspot_connection_deactivated (GObject *object, GAsyncResult *result, gpointer data)
{
  NMClient *client = NM_CLIENT (object);
  g_autoptr (GError) err = NULL;
  gboolean success = nm_client_deactivate_connection_finish (client, result, &err);

  if (success)
    g_debug ("Deactivating hotspot connection");
  else
    g_warning ("Failed to deactivate hotspot connection: %s", err->message);
}


static void
stop_hotspot (PhoshWifiManager *self)
{
  g_return_if_fail (is_active_connection_hotspot_master (self));

  nm_client_deactivate_connection_async (self->nmclient,
                                         self->active,
                                         self->cancel,
                                         on_hotspot_connection_deactivated,
                                         NULL);
}


static void
on_wifi_connection_added_and_activated (GObject *object, GAsyncResult *result, gpointer data)
{
  NMClient *client = NM_CLIENT (object);
  PhoshWifiNetwork *network = PHOSH_WIFI_NETWORK (data);
  const char *ssid = phosh_wifi_network_get_ssid (network);
  g_autoptr (GError) err = NULL;
  NMActiveConnection *conn = nm_client_add_and_activate_connection_finish (client, result, &err);

  if (conn != NULL) {
    g_debug ("Connecting to Wi-Fi network using a new connection: %s", ssid);
    g_signal_connect_object (conn,
                             "state-changed",
                             G_CALLBACK (on_connection_state_changed),
                             network,
                             G_CONNECT_DEFAULT);
  } else {
    g_warning ("Failed to connect to Wi-Fi network: %s - %s", ssid, err->message);
    phosh_wifi_network_set_is_connecting (network, FALSE);
  }
}


static void
on_wifi_connection_activated (GObject *object, GAsyncResult *result, gpointer data)
{
  NMClient *client = NM_CLIENT (object);
  PhoshWifiNetwork *network = PHOSH_WIFI_NETWORK (data);
  const char *ssid = phosh_wifi_network_get_ssid (network);
  g_autoptr (GError) err = NULL;
  NMActiveConnection *conn = nm_client_activate_connection_finish (client, result, &err);

  if (conn != NULL) {
    g_debug ("Connecting to Wi-Fi network using available connections: %s", ssid);
    g_signal_connect_object (conn,
                             "state-changed",
                             G_CALLBACK (on_connection_state_changed),
                             network,
                             G_CONNECT_DEFAULT);
  } else {
    g_warning ("Failed to connect to Wi-Fi network: %s - %s", ssid, err->message);
    phosh_wifi_network_set_is_connecting (network, FALSE);
  }
}


static void
set_scanning (PhoshWifiManager *self, gboolean scanning)
{
  if (self->scanning == scanning)
    return;

  self->scanning = scanning;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SCANNING]);

}


static gboolean
check_scanning (gpointer user_data)
{
  PhoshWifiManager *self = PHOSH_WIFI_MANAGER (user_data);
  gint64 last_scan;

  if (!self->dev) {
    self->scanning_id = 0;
    set_scanning (self, FALSE);
    return G_SOURCE_REMOVE;
  }

  last_scan = nm_device_wifi_get_last_scan (self->dev);
  if (self->last_scan != last_scan) {
    self->scanning_id = 0;
    self->last_scan = last_scan;
    set_scanning (self, FALSE);
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}


static void
on_request_scan (GObject *object, GAsyncResult *result, gpointer data)
{
  PhoshWifiManager *self;
  NMDeviceWifi *dev = NM_DEVICE_WIFI (object);
  g_autoptr (GError) err = NULL;
  gboolean success = nm_device_wifi_request_scan_finish (dev, result, &err);

  if (!success) {
    g_warning ("Failed to scan for access points: %s", err->message);
    return;
  }

  self = PHOSH_WIFI_MANAGER (data);

  if (self->scanning_id)
    return;

  self->scanning_id = g_timeout_add (2000, check_scanning, self);
  set_scanning (self, TRUE);
}


static void
phosh_wifi_manager_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshWifiManager *self = PHOSH_WIFI_MANAGER (object);

  switch (property_id) {
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name);
    break;
  case PROP_SSID:
    g_value_set_string (value, self->ssid);
    break;
  case PROP_ENABLED:
    g_value_set_boolean (value, self->enabled);
    break;
  case PROP_PRESENT:
    g_value_set_boolean (value, self->present);
    break;
  case PROP_IS_HOTSPOT_MASTER:
    g_value_set_boolean (value, self->is_hotspot_master);
    break;
  case PROP_NETWORKS:
    g_value_set_object (value, self->networks);
    break;
  case PROP_STATE:
    g_value_set_enum (value, self->state);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static char *
get_access_point_ssid (NMAccessPoint *ap)
{
  GBytes *ssid_bytes;
  char *ssid = NULL;

  ssid_bytes = nm_access_point_get_ssid (ap);
  if (!ssid_bytes || !g_bytes_get_size (ssid_bytes))
    return FALSE;

  ssid = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid_bytes, NULL),
                                g_bytes_get_size (ssid_bytes));

  return ssid;
}

static gboolean
is_valid_access_point (NMAccessPoint *ap)
{
  g_autofree char *ssid = get_access_point_ssid (ap);

  return ssid != NULL;
}


static void
on_nm_access_point_added (PhoshWifiManager *self, NMAccessPoint *ap)
{
  g_autoptr (PhoshWifiNetwork) n = NULL;
  guint length = g_list_model_get_n_items (G_LIST_MODEL (self->networks));
  g_autofree char *ssid = get_access_point_ssid (ap);

  g_assert (NM_IS_ACCESS_POINT (ap));

  if (!is_valid_access_point (ap)) {
    g_debug ("Discarding access point due to no SSID");
    return;
  }

  for (guint i = 0; i < length; i++) {
    g_autoptr (PhoshWifiNetwork) network = NULL;
    network = g_list_model_get_item (G_LIST_MODEL (self->networks), i);

    if (phosh_wifi_network_matches_access_point (network, ap)) {
      g_debug ("Adding access point to existing network: %s", ssid);
      phosh_wifi_network_add_access_point (network, ap, self->ap == ap);
      return;
    }
  }
  g_debug ("Creating network: %s", ssid);
  n = phosh_wifi_network_new_from_access_point (ap, self->ap == ap);
  g_list_store_append (self->networks, n);
}


static void
on_nm_access_point_removed (PhoshWifiManager *self, NMAccessPoint *ap)
{
  guint length = g_list_model_get_n_items (G_LIST_MODEL (self->networks));
  g_autofree char *ssid = get_access_point_ssid (ap);

  if (!is_valid_access_point (ap))
    return;

  g_debug ("Removing AP: %s", ssid);

  for (guint i = 0; i < length; i++) {
    g_autoptr (PhoshWifiNetwork) network = NULL;
    network = g_list_model_get_item (G_LIST_MODEL (self->networks), i);

    if (phosh_wifi_network_matches_access_point (network, ap)) {
      if (phosh_wifi_network_remove_access_point (network, ap)) {
        g_debug ("Removing network: %s", ssid);
        g_list_store_remove (self->networks, i);
      }
      return;
    }
  }
}


static void
reset_active_wifi_network (PhoshWifiManager *self)
{
  guint length = g_list_model_get_n_items (G_LIST_MODEL (self->networks));

  for (guint i = 0; i < length; i++) {
    g_autoptr (PhoshWifiNetwork) network = NULL;
    network = g_list_model_get_item (G_LIST_MODEL (self->networks), i);
    phosh_wifi_network_update_active (network, self->ap);
  }
}


static void
refresh_access_points (PhoshWifiManager *self)
{
  const GPtrArray *aps;
  NMAccessPoint *ap;

  if (self->dev == NULL)
    return;

  g_list_store_remove_all (G_LIST_STORE (self->networks));
  aps = nm_device_wifi_get_access_points (self->dev);

  if (aps == NULL)
    return;

  for (int i = 0; i < aps->len; i++) {
    ap = g_ptr_array_index (aps, i);
    on_nm_access_point_added (self, ap);
  }
}


static void
on_nm_access_point_strength_changed (PhoshWifiManager *self, GParamSpec *pspec, NMAccessPoint *ap)
{
  guint8 strength;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_ACCESS_POINT (ap));

  strength = phosh_wifi_manager_get_strength (self);
  g_debug ("Strength changed: %d", strength);

  update_properties (self);
}


static void
on_nm_device_wifi_active_access_point_changed (PhoshWifiManager *self,
                                               GParamSpec       *pspec,
                                               NMDeviceWifi     *dev)
{
  NMAccessPoint *old_ap;
  GBytes *ssid;
  g_autofree char *old_ssid = NULL;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_DEVICE_WIFI (dev));

  old_ap = self->ap;

  self->ap = nm_device_wifi_get_active_access_point (self->conn_dev);
  if (old_ap == self->ap)
    return;

  reset_active_wifi_network (self);

  if (self->ap)
    g_debug ("Wi-Fi access point changed to '%s'", nm_access_point_get_bssid (self->ap));
  else
    g_debug ("Wi-Fi access point changed");

  if (self->ap)
    g_object_ref (self->ap);
  if (old_ap) {
    g_signal_handlers_disconnect_by_data (old_ap, self);
    g_object_unref (old_ap);
  }

  old_ssid = self->ssid;
  self->ssid = NULL;

  if (self->ap) {
    g_signal_connect_swapped (self->ap, "notify::strength",
                              G_CALLBACK (on_nm_access_point_strength_changed), self);
    on_nm_access_point_strength_changed (self, NULL, self->ap);

    ssid = nm_access_point_get_ssid (self->ap);
    self->ssid = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid, NULL), g_bytes_get_size (ssid));
    g_debug ("Wi-Fi network name was set to '%s'", self->ssid);
  }

  if (g_strcmp0 (self->ssid, old_ssid) != 0)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SSID]);
}


static void
check_connected_device (PhoshWifiManager *self)

{
  const GPtrArray *devs;
  NMDevice *dev;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));

  devs = nm_active_connection_get_devices (self->active);
  if (!devs || !devs->len) {
    g_warning ("Found active connection but no device");
    return;
  }

  dev = g_ptr_array_index (devs, 0);
  if (NM_IS_DEVICE_WIFI (dev)) {
    g_debug ("Connection %p uses a Wi-Fi device", self->active);

    /* Is this still the same device? */
    if (dev != NM_DEVICE (self->conn_dev)) {
      if (self->conn_dev)
        g_signal_handlers_disconnect_by_data (self->conn_dev, self);
      g_set_object (&self->conn_dev, NM_DEVICE_WIFI (dev));

      g_signal_connect_swapped (self->conn_dev, "notify::active-access-point",
                                G_CALLBACK (on_nm_device_wifi_active_access_point_changed), self);
      on_nm_device_wifi_active_access_point_changed (self, NULL, self->conn_dev);
    }
  }
}


static void
cleanup_connection_device (PhoshWifiManager *self)
{
  if (self->ap) {
    g_signal_handlers_disconnect_by_data (self->ap, self);
    g_clear_object (&self->ap);
    g_clear_pointer (&self->ssid, g_free);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SSID]);
  }

  if (self->conn_dev) {
    /* Since conn_dev and dev point to same instance,
     * disconnecting by data will disconnect all signals. */
    g_signal_handlers_disconnect_by_func (self->conn_dev,
                                          on_nm_device_wifi_active_access_point_changed,
                                          self);
    g_clear_object (&self->conn_dev);
  }
}

static void
on_nm_active_connection_state_changed (PhoshWifiManager             *self,
                                       NMActiveConnectionState       state,
                                       NMActiveConnectionStateReason reason,
                                       NMActiveConnection           *active)
{
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_ACTIVE_CONNECTION (active));

  g_debug ("Active connection state changed %d", state);

  update_properties (self);

  switch (state) {
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATED:
    check_connected_device (self);
    break;
  case NM_ACTIVE_CONNECTION_STATE_ACTIVATING:
  case NM_ACTIVE_CONNECTION_STATE_UNKNOWN:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATING:
  case NM_ACTIVE_CONNECTION_STATE_DEACTIVATED:
  default:
    cleanup_connection_device (self);
  }

  if (self->state != state) {
    self->state = state;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATE]);
  }
}


static void
on_nmclient_wireless_enabled_changed (PhoshWifiManager *self, GParamSpec *pspec, NMClient *nmclient)
{
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_CLIENT (nmclient));

  update_properties (self);

  if (self->ssid != NULL) {
    g_clear_pointer (&self->ssid, g_free);
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SSID]);
  }
}


/*
 * Active connections changed
 *
 * Look if we have a connection using a Wi-Fi device and listen
 * for changes on that connection.
 */
static void
on_nmclient_active_connections_changed (PhoshWifiManager *self,
                                        GParamSpec       *pspec,
                                        NMClient         *nmclient)
{
  const GPtrArray *conns;
  NMActiveConnection *conn;
  gboolean found = FALSE;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_CLIENT (nmclient));

  conns = nm_client_get_active_connections (nmclient);

  for (int i = 0; i < conns->len; i++) {
    const char *type;

    conn = g_ptr_array_index (conns, i);
    type = nm_active_connection_get_connection_type (conn);

    /* We only care about wireless connections */
    if (g_strcmp0 (type, NM_SETTING_WIRELESS_SETTING_NAME))
      continue;

    found = TRUE;
    /* Is this still the same connection? */
    if (conn != self->active) {
      g_debug ("New active connection %p", conn);
      cleanup_connection_device (self);
      if (self->active)
        g_signal_handlers_disconnect_by_data (self->active, self);
      g_set_object (&self->active, conn);
      g_signal_connect_swapped (self->active, "state-changed",
                                G_CALLBACK (on_nm_active_connection_state_changed), self);
      on_nm_active_connection_state_changed (self,
                                             nm_active_connection_get_state (self->active),
                                             nm_active_connection_get_state_reason (self->active),
                                             self->active);
    }
    check_connected_device (self);
    break;
  }

  /* Clean up if there's no active Wi-Fi connection */
  if (!found) {
    if (self->active)
      g_signal_handlers_disconnect_by_data (self->active, self);
    g_clear_object (&self->active);
    cleanup_connection_device (self);
  }
}


static void
setup_wifi_device (PhoshWifiManager *self, NMDeviceWifi *dev)
{
  g_set_object (&self->dev, dev);
  g_signal_connect_swapped (self->dev, "access-point-added",
                            G_CALLBACK (on_nm_access_point_added), self);
  g_signal_connect_swapped (self->dev, "access-point-removed",
                            G_CALLBACK (on_nm_access_point_removed), self);

  refresh_access_points (self);
}


static void
cleanup_wifi_device (PhoshWifiManager *self)
{
  if (self->dev == NULL)
    return;

  g_list_store_remove_all (G_LIST_STORE (self->networks));

  g_signal_handlers_disconnect_by_data (self->dev, self);
  g_clear_object (&self->dev);
}


static void
on_nmclient_devices_changed (PhoshWifiManager *self, GParamSpec *pspec, NMClient *nmclient)
{
  gboolean have_wifi_dev = FALSE, present;
  const GPtrArray *devs;
  NMDevice *dev;
  NMDeviceWifi *wifi_dev = NULL;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_CLIENT (nmclient));

  present = self->present;

  devs = nm_client_get_devices (nmclient);
  if (!devs || !devs->len) {
    update_properties (self);
    cleanup_wifi_device (self);
    self->present = FALSE;
    if (self->present != present)
      g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
    return;
  }

  for (int i = 0; i < devs->len; i++) {
    dev = g_ptr_array_index (devs, i);
    if (NM_IS_DEVICE_WIFI (dev)) {
      g_debug ("Wi-Fi device connected at %d", i);
      have_wifi_dev = TRUE;
      wifi_dev = NM_DEVICE_WIFI (dev);
      break;
    }
  }

  if (self->dev == wifi_dev)
    return;

  cleanup_wifi_device (self);

  if (have_wifi_dev)
    setup_wifi_device (self, wifi_dev);

  self->present = have_wifi_dev;
  if (self->present != present)
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PRESENT]);
  update_properties (self);
}


static void
on_nm_client_ready (GObject *obj, GAsyncResult *res, gpointer data)
{
  g_autoptr (GError) err = NULL;
  PhoshWifiManager *self;
  NMClient *client;

  client = nm_client_new_finish (res, &err);
  if (client == NULL) {
    g_message ("Failed to init NM: %s", err->message);
    return;
  }

  self = PHOSH_WIFI_MANAGER (data);
  self->nmclient = client;

  g_signal_connect_swapped (self->nmclient, "notify::wireless-enabled",
                            G_CALLBACK (on_nmclient_wireless_enabled_changed), self);
  g_signal_connect_swapped (self->nmclient, "notify::active-connections",
                            G_CALLBACK (on_nmclient_active_connections_changed), self);
  g_signal_connect_swapped (self->nmclient, "notify::devices",
                            G_CALLBACK (on_nmclient_devices_changed), self);

  update_properties (self);

  on_nmclient_active_connections_changed (self, NULL, self->nmclient);
  on_nmclient_devices_changed (self, NULL, self->nmclient);

  g_debug ("Wifi manager initialized");
}


static void
phosh_wifi_manager_constructed (GObject *object)
{
  PhoshWifiManager *self = PHOSH_WIFI_MANAGER (object);

  self->networks = g_list_store_new (PHOSH_TYPE_WIFI_NETWORK);

  self->cancel = g_cancellable_new ();
  nm_client_new_async (self->cancel, on_nm_client_ready, self);

  G_OBJECT_CLASS (phosh_wifi_manager_parent_class)->constructed (object);
}


static void
phosh_wifi_manager_dispose (GObject *object)
{
  PhoshWifiManager *self = PHOSH_WIFI_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  if (self->ap) {
    g_signal_handlers_disconnect_by_data (self->ap, self);
    g_clear_object (&self->ap);
  }

  if (self->nmclient) {
    g_signal_handlers_disconnect_by_data (self->nmclient, self);
    g_clear_object (&self->nmclient);
  }

  g_clear_handle_id (&self->scanning_id, g_source_remove);
  cleanup_connection_device (self);
  cleanup_wifi_device (self);

  if (self->active) {
    g_signal_handlers_disconnect_by_data (self->active, self);
    g_clear_object (&self->active);
  }

  g_clear_pointer (&self->ssid, g_free);

  g_clear_object (&self->networks);

  G_OBJECT_CLASS (phosh_wifi_manager_parent_class)->dispose (object);
}


static void
phosh_wifi_manager_class_init (PhoshWifiManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_wifi_manager_constructed;
  object_class->dispose = phosh_wifi_manager_dispose;

  object_class->get_property = phosh_wifi_manager_get_property;

  /**
   * PhoshWifiManager:icon-name:
   *
   * The Wi-Fi icon name
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:ssid:
   *
   * The Wi-Fi ssid, if connected
   */
  props[PROP_SSID] =
    g_param_spec_string ("ssid", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:enabled
   *
   * Whether Wi-Fi is enabled and a Wi-Fi device is available
   */
  props[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:present:
   *
   * Whether Wi-Fi hardware is present
   */
  props[PROP_PRESENT] =
    g_param_spec_boolean ("present", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:hotspot-master:
   *
   * Whether we're a hotspot master at the moment
   */
  props[PROP_IS_HOTSPOT_MASTER] =
    g_param_spec_boolean ("is-hotspot-master", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:networks:
   *
   * List of Wi-Fi networks
   */
  props[PROP_NETWORKS] =
    g_param_spec_object ("networks", "", "",
                         G_TYPE_LIST_STORE,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:state:
   *
   * State of the active connection
   */
  props[PROP_STATE] =
    g_param_spec_enum ("state", "", "",
                       NM_TYPE_ACTIVE_CONNECTION_STATE, NM_ACTIVE_CONNECTION_STATE_UNKNOWN,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWifiManager:scanning:
   *
   * Whether a Wi-Fi scan is ongoing
   */
  props[PROP_SCANNING] =
    g_param_spec_boolean ("scanning", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_wifi_manager_init (PhoshWifiManager *self)
{
  self->icon_name = "network-wireless-disabled-symbolic";
}


PhoshWifiManager *
phosh_wifi_manager_new (void)
{
  return PHOSH_WIFI_MANAGER (g_object_new (PHOSH_TYPE_WIFI_MANAGER, NULL));
}


guint8
phosh_wifi_manager_get_strength (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), 0);

  if (!self->conn_dev || !self->ap)
    return 0;

  return nm_access_point_get_strength (self->ap);
}

const char *
phosh_wifi_manager_get_icon_name (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), NULL);

  return self->icon_name;
}


const char *
phosh_wifi_manager_get_ssid (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), NULL);

  return self->ssid;
}

gboolean
phosh_wifi_manager_get_enabled (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), FALSE);

  return self->enabled;
}


void
phosh_wifi_manager_set_enabled (PhoshWifiManager *self, gboolean enabled)
{
  int DEFAULT_TIMEOUT_MSEC = -1;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));

  if (enabled == self->enabled)
    return;

  g_return_if_fail (NM_IS_CLIENT (self->nmclient));

  nm_client_dbus_set_property (self->nmclient,
                               NM_DBUS_PATH,
                               NM_DBUS_INTERFACE,
                               "WirelessEnabled",
                               g_variant_new_boolean (enabled),
                               DEFAULT_TIMEOUT_MSEC,
                               NULL, NULL, NULL);
}


gboolean
phosh_wifi_manager_get_present (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), FALSE);

  return self->present;
}

/**
 * phosh_wifi_manager_is_hotspot_master:
 *
 * Whether we're currently a hotspot master
 *
 * Returns: %TRUE if hotspot master, %FALSE otherwise
 */
gboolean
phosh_wifi_manager_is_hotspot_master (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), FALSE);

  return self->is_hotspot_master;
}

void
phosh_wifi_manager_set_hotspot_master (PhoshWifiManager *self, gboolean is_hotspot_master)
{
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (self->dev != NULL);

  if (self->is_hotspot_master == is_hotspot_master)
    return;

  if (self->is_hotspot_master)
    stop_hotspot (self);
  else
    start_hotspot (self);
}

/**
 * phosh_wifi_manager_get_networks:
 * @self: The wifi manager
 *
 * Get the list store of known Wi-Fi networks.
 *
 * Returns:(transfer none): The Wi-Fi networks
 */
GListStore *
phosh_wifi_manager_get_networks (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), FALSE);

  return self->networks;
}


void
phosh_wifi_manager_connect_network (PhoshWifiManager *self, PhoshWifiNetwork *network)
{
  NMAccessPoint *ap;
  const GPtrArray *all_cnx;
  g_autoptr (GPtrArray) wifi_cnx = NULL;
  g_autoptr (GPtrArray) ap_cnx = NULL;

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));
  g_return_if_fail (NM_IS_CLIENT (self->nmclient));

  if (!NM_IS_DEVICE_WIFI (self->dev)) {
    g_debug ("Unable to connect to Wi-Fi network %s as Wi-Fi device is unavailable",
             phosh_wifi_network_get_ssid (network));
    return;
  }

  phosh_wifi_network_set_is_connecting (network, TRUE);
  ap = phosh_wifi_network_get_best_access_point (network);

  all_cnx = nm_client_get_connections (self->nmclient);
  wifi_cnx = nm_device_filter_connections (NM_DEVICE (self->dev), all_cnx);
  ap_cnx = nm_access_point_filter_connections (ap, wifi_cnx);

  g_debug ("Found %d connections for %s",
           ap_cnx->len,
           phosh_wifi_network_get_ssid (network));

  if (ap_cnx->len == 0) {
    nm_client_add_and_activate_connection_async (self->nmclient,
                                                 NULL,
                                                 NM_DEVICE (self->dev),
                                                 nm_object_get_path (NM_OBJECT (ap)),
                                                 self->cancel,
                                                 on_wifi_connection_added_and_activated,
                                                 network);
  } else {
    nm_client_activate_connection_async (self->nmclient,
                                         NULL,
                                         NM_DEVICE (self->dev),
                                         nm_object_get_path (NM_OBJECT (ap)),
                                         self->cancel,
                                         on_wifi_connection_activated,
                                         network);
  }
}


void
phosh_wifi_manager_request_scan (PhoshWifiManager *self)
{
  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self));

  if (self->dev == NULL)
    return;

  self->last_scan = nm_device_wifi_get_last_scan (self->dev);
  nm_device_wifi_request_scan_async (self->dev, self->cancel, on_request_scan, self);
}

/**
 * phosh_wifi_manager_get_scanning:
 * @self: The WiFi manager
 *
 * Get whether a WiFi scan is in progress
 *
 * Returns: `TRUE` if scanning, otherwise `FALSE`
 */
gboolean
phosh_wifi_manager_get_scanning (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), FALSE);

  return self->scanning;
}


NMActiveConnectionState
phosh_wifi_manager_get_state (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), NM_ACTIVE_CONNECTION_STATE_UNKNOWN);

  return self->state;
}


NMActiveConnection *
phosh_wifi_manager_get_active_connection (PhoshWifiManager *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_MANAGER (self), NULL);

  return self->active;
}
