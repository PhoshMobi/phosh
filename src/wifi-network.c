/*
 * Copyright (C) 2023 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arunmani@peartree.to>
 */

#define G_LOG_DOMAIN "phosh-wifi-network"

#include "wifi-network.h"

/**
 * PhoshWifiNetwork:
 *
 * An object that represents a Wi-Fi network.
 *
 * A network is identified by it's SSID and encryption type and mode
 */

enum {
  PROP_0,
  PROP_SSID,
  PROP_SECURED,
  PROP_MODE,
  PROP_STRENGTH,
  PROP_ACTIVE,
  PROP_IS_CONNECTING,
  PROP_BEST_ACCESS_POINT,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshWifiNetwork {
  GObject        parent;
  char          *ssid;
  gboolean       secured;
  NM80211Mode    mode;
  guint          strength;
  gboolean       active;
  gboolean       is_connecting;
  GPtrArray     *access_points;
  NMAccessPoint *best_ap;
};

G_DEFINE_TYPE (PhoshWifiNetwork, phosh_wifi_network, G_TYPE_OBJECT);

static void
phosh_wifi_network_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  PhoshWifiNetwork *self = PHOSH_WIFI_NETWORK (object);

  switch (property_id)
  {
  case PROP_SSID:
    self->ssid = g_strdup (g_value_get_string (value));
    break;
  case PROP_SECURED:
    self->secured = g_value_get_boolean (value);
    break;
  case PROP_MODE:
    self->mode = g_value_get_enum (value);
    break;
  case PROP_IS_CONNECTING:
    phosh_wifi_network_set_is_connecting (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
phosh_wifi_network_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshWifiNetwork *self = PHOSH_WIFI_NETWORK (object);

  switch (property_id)
  {
  case PROP_SSID:
    g_value_set_string (value, phosh_wifi_network_get_ssid (self));
    break;
  case PROP_SECURED:
    g_value_set_boolean (value, phosh_wifi_network_get_secured (self));
    break;
  case PROP_MODE:
    g_value_set_enum (value, phosh_wifi_network_get_mode (self));
    break;
  case PROP_STRENGTH:
    g_value_set_uint (value, phosh_wifi_network_get_strength (self));
    break;
  case PROP_ACTIVE:
    g_value_set_boolean (value, phosh_wifi_network_get_active (self));
    break;
  case PROP_IS_CONNECTING:
    g_value_set_boolean (value, phosh_wifi_network_get_is_connecting (self));
    break;
  case PROP_BEST_ACCESS_POINT:
    g_value_set_object (value, phosh_wifi_network_get_best_access_point (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }

}

static void
update_active (PhoshWifiNetwork *self, gboolean active)
{
  if (self->active == active)
    return;

  self->active = active;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
}


static char *
get_access_point_ssid (NMAccessPoint *ap)
{
  GBytes *ssid_bytes;
  char *ssid;

  ssid_bytes = nm_access_point_get_ssid (ap);
  g_return_val_if_fail (ssid_bytes != NULL && g_bytes_get_size (ssid_bytes), NULL);
  ssid = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid_bytes, NULL),
                                g_bytes_get_size (ssid_bytes));
  return ssid;

}

static void
find_set_best_access_point (PhoshWifiNetwork *self)
{
  guint new_strength = 0, strength;
  NMAccessPoint *best_ap = NULL, *ap;

  for (int i = 0; i < self->access_points->len; i++) {
    ap = g_ptr_array_index (self->access_points, i);
    strength = nm_access_point_get_strength (ap);
    if (strength > new_strength) {
      new_strength = strength;
      best_ap = ap;
    }
  }

  self->best_ap = best_ap;

  if (new_strength == self->strength)
    return;

  self->strength = new_strength;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STRENGTH]);
}

static void
update_best_access_point (PhoshWifiNetwork *self, GParamSpec *pspec, NMAccessPoint *ap)
{
  guint strength = nm_access_point_get_strength (ap);

  if (strength <= self->strength)
    return;

  self->strength = strength;
  self->best_ap = ap;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STRENGTH]);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_BEST_ACCESS_POINT]);
}

static void
phosh_wifi_network_dispose (GObject *object)
{
  PhoshWifiNetwork *self = PHOSH_WIFI_NETWORK (object);
  NMAccessPoint *ap;

  for (int i = 0; i < self->access_points->len; i++) {
    ap = g_ptr_array_index (self->access_points, i);
    g_signal_handlers_disconnect_by_data (ap, self);
  }

  G_OBJECT_CLASS (phosh_wifi_network_parent_class)->dispose (object);
}

static void
phosh_wifi_network_finalize (GObject *object)
{
  PhoshWifiNetwork *self = PHOSH_WIFI_NETWORK (object);

  g_free (self->ssid);
  g_ptr_array_free (self->access_points, TRUE);

  G_OBJECT_CLASS (phosh_wifi_network_parent_class)->finalize (object);
}

static void
phosh_wifi_network_class_init (PhoshWifiNetworkClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_wifi_network_dispose;
  object_class->finalize = phosh_wifi_network_finalize;
  object_class->set_property = phosh_wifi_network_set_property;
  object_class->get_property = phosh_wifi_network_get_property;

  /**
   * PhoshWifiNetwork:ssid:
   *
   * SSID of the network
   */
  props[PROP_SSID] =
    g_param_spec_string ("ssid", "", "",
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:secured:
   *
   * Whether the network is secured
   */
  props[PROP_SECURED] =
    g_param_spec_boolean ("secured", "", "",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:mode:
   *
   * Mode of the network
   */
  props[PROP_MODE] =
    g_param_spec_enum ("mode", "", "",
                       NM_TYPE_802_11_MODE,
                       NM_802_11_MODE_UNKNOWN,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:strength:
   *
   * Strength of the best access point of the network
   */
  props[PROP_STRENGTH] =
    g_param_spec_uint ("strength", "", "",
                       0, 100, 0,
                       G_PARAM_READABLE |
                       G_PARAM_EXPLICIT_NOTIFY |
                       G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:active:
   *
   * Whether the activated access point belongs to the network
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:is-connecting:
   *
   * Whether we are trying to connect to the network
   */
  props[PROP_IS_CONNECTING] =
    g_param_spec_boolean ("is-connecting", "", "",
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS);

  /**
   * PhoshWifiNetwork:best-access-point:
   *
   * The best access point of the network
   */
  props[PROP_BEST_ACCESS_POINT] =
    g_param_spec_object ("best-access-point", "", "",
                         NM_TYPE_ACCESS_POINT,
                         G_PARAM_READABLE |
                         G_PARAM_EXPLICIT_NOTIFY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}

static void
phosh_wifi_network_init (PhoshWifiNetwork *self)
{
  self->access_points = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

PhoshWifiNetwork *
phosh_wifi_network_new_from_access_point (NMAccessPoint *ap, gboolean active)
{
  g_autofree char *ssid = get_access_point_ssid (ap);
  gboolean secured = nm_access_point_get_flags (ap) & NM_802_11_AP_FLAGS_PRIVACY;
  NM80211Mode mode = nm_access_point_get_mode (ap);
  PhoshWifiNetwork *self = g_object_new (PHOSH_TYPE_WIFI_NETWORK,
                                         "ssid", ssid,
                                         "secured", secured,
                                         "mode", mode,
                                         NULL);

  phosh_wifi_network_add_access_point (self, ap, active);

  return self;
}

gboolean
phosh_wifi_network_matches_access_point (PhoshWifiNetwork *self, NMAccessPoint *ap)
{
  g_autofree char *ssid = NULL;
  NM80211Mode mode;
  NM80211ApFlags flags;
  gboolean ssid_matches, mode_matches, security_matches;

  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), FALSE);
  g_return_val_if_fail (NM_IS_ACCESS_POINT (ap), FALSE);

  ssid = get_access_point_ssid (ap);
  mode = nm_access_point_get_mode (ap);
  flags = nm_access_point_get_flags (ap);

  ssid_matches = (g_strcmp0 (ssid, self->ssid) == 0);
  mode_matches = mode == self->mode;
  security_matches = !!((flags & NM_802_11_AP_FLAGS_PRIVACY) == self->secured);

  if (ssid_matches && mode_matches && security_matches)
    return TRUE;
  return FALSE;
}

void
phosh_wifi_network_add_access_point (PhoshWifiNetwork *self, NMAccessPoint *ap, gboolean active)
{
  g_ptr_array_add (self->access_points, g_object_ref (ap));
  if (active)
    update_active (self, TRUE);
  g_signal_connect_swapped (ap, "notify::strength", G_CALLBACK (update_best_access_point), self);
  update_best_access_point (self, NULL, ap);
}

gboolean
phosh_wifi_network_remove_access_point (PhoshWifiNetwork *self, NMAccessPoint *ap)
{
  g_signal_handlers_disconnect_by_data (ap, self);
  g_ptr_array_remove (self->access_points, ap);
  find_set_best_access_point (self);
  return self->access_points->len == 0;
}

const char *
phosh_wifi_network_get_ssid (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), NULL);
  return self->ssid;
}

guint
phosh_wifi_network_get_strength (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), 0);
  return self->strength;
}

NM80211Mode
phosh_wifi_network_get_mode (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), 0);
  return self->mode;
}

gboolean
phosh_wifi_network_get_secured (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), FALSE);
  return self->secured;
}

gboolean
phosh_wifi_network_get_active (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), FALSE);
  return self->active;
}

void
phosh_wifi_network_update_active (PhoshWifiNetwork *self, NMAccessPoint *active_ap)
{
  gboolean active = FALSE;
  NMAccessPoint *ap;
  g_return_if_fail (PHOSH_IS_WIFI_NETWORK (self));
  g_return_if_fail (active_ap == NULL || NM_IS_ACCESS_POINT (active_ap));

  for (int i = 0; i < self->access_points->len; i++) {
    ap = g_ptr_array_index (self->access_points, i);
    if (ap == active_ap) {
      active = TRUE;
      break;
    }
  }

  update_active (self, active);
}

void
phosh_wifi_network_set_is_connecting (PhoshWifiNetwork *self, gboolean is_connecting)
{
  g_return_if_fail (PHOSH_IS_WIFI_NETWORK (self));

  if (self->is_connecting == is_connecting)
    return;

  self->is_connecting = is_connecting;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_CONNECTING]);
}

gboolean
phosh_wifi_network_get_is_connecting (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), FALSE);
  return self->is_connecting;
}

/**
 * phosh_wifi_network_get_best_access_point:
 * @self: A wifi network
 *
 * Get the AP with the greatest signal strength in the givein Wi-Fi network.
 *
 * Returns:(transfer none): The best access point
 */
NMAccessPoint *
phosh_wifi_network_get_best_access_point (PhoshWifiNetwork *self)
{
  g_return_val_if_fail (PHOSH_IS_WIFI_NETWORK (self), NULL);
  return self->best_ap;
}
