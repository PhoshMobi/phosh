/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "plugin-shell.h"
#include "status-page-placeholder.h"
#include "wifi-hotspot-status-page.h"

#include <qrcodegen.h>

#define QR_CODE_SIZE 128

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_t, cairo_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_surface_t, cairo_surface_destroy)

/**
 * PhoshWifiHotspotStatusPage:
 *
 * A status-page to show password of Wi-Fi hotspot.
 *
 * The code to display QR is taken from GNOME Control Center.
 */

struct _PhoshWifiHotspotStatusPage {
  PhoshStatusPage   parent;

  GtkEntry         *entry;
  GtkImage         *image;
  PhoshStatusPagePlaceholder *placeholder;
  GtkLabel         *ssid;
  GtkStack         *stack;
  GtkButton        *turn_on_btn;

  GCancellable     *cancel;
  PhoshWifiManager *wifi;
};

G_DEFINE_TYPE (PhoshWifiHotspotStatusPage, phosh_wifi_hotspot_status_page, PHOSH_TYPE_STATUS_PAGE);


static cairo_surface_t *
qr_from_text (const char *text, int size, int scale)
{
  uint8_t qr_code[qrcodegen_BUFFER_LEN_FOR_VERSION (qrcodegen_VERSION_MAX)];
  uint8_t temp_buf[qrcodegen_BUFFER_LEN_FOR_VERSION (qrcodegen_VERSION_MAX)];
  g_autoptr (cairo_t) cr = NULL;
  cairo_surface_t *surface;
  int pixel_size, qr_size, padding;
  gboolean success = FALSE;

  g_return_val_if_fail (size > 0, NULL);
  g_return_val_if_fail (scale > 0, NULL);

  success = qrcodegen_encodeText (text,
                                  temp_buf,
                                  qr_code,
                                  qrcodegen_Ecc_LOW,
                                  qrcodegen_VERSION_MIN,
                                  qrcodegen_VERSION_MAX,
                                  qrcodegen_Mask_AUTO,
                                  FALSE);

  if (!success)
    return NULL;

  surface = cairo_image_surface_create (CAIRO_FORMAT_RGB24, size * scale, size * scale);
  cairo_surface_set_device_scale (surface, scale, scale);
  cr = cairo_create (surface);
  cairo_set_antialias (cr, CAIRO_ANTIALIAS_NONE);

  /* Draw white background */
  cairo_set_source_rgba (cr, 1, 1, 1, 1);
  cairo_rectangle (cr, 0, 0, size * scale, size * scale);
  cairo_fill (cr);

  qr_size = qrcodegen_getSize (qr_code);
  pixel_size = MAX (1, size / (qr_size));
  padding = (size - qr_size * pixel_size) / 2;

  /* If subpixel size is big and margin is pretty small,
   * increase the margin */
  if (pixel_size > 4 && padding < 12) {
    pixel_size--;
    padding = (size - qr_size * pixel_size) / 2;
  }

  /* Now draw the black QR code pixels */
  cairo_set_source_rgba (cr, 0, 0, 0, 1);
  for (int row = 0; row < qr_size; row++) {
    for (int column = 0; column < qr_size; column++) {
      if (qrcodegen_getModule (qr_code, row, column)) {
        cairo_rectangle (cr,
                         column * pixel_size + padding,
                         row * pixel_size + padding,
                         pixel_size, pixel_size);
        cairo_fill (cr);
      }
    }
  }

  return surface;
}


static char *
escape_string (const char *str, gboolean quote)
{
  GString *string;
  const char *next;

  if (!str)
    return NULL;

  string = g_string_new ("");
  if (quote)
    g_string_append_c (string, '"');

  while ((next = strpbrk (str, "\\;,:\""))) {
    g_string_append_len (string, str, next - str);
    g_string_append_c (string, '\\');
    g_string_append_c (string, *next);
    str = next + 1;
  }

  g_string_append (string, str);
  if (quote)
    g_string_append_c (string, '"');

  return g_string_free (string, FALSE);
}


static const char *
get_connection_security_type (NMConnection *c)
{
  NMSettingWirelessSecurity *setting;
  const char *key_mgmt;

  g_return_val_if_fail (c, "nopass");

  setting = nm_connection_get_setting_wireless_security (c);

  if (!setting)
    return "nopass";

  key_mgmt = nm_setting_wireless_security_get_key_mgmt (setting);

  /* No IEEE 802.1x */
  if (g_strcmp0 (key_mgmt, "none") == 0)
    return "WEP";

  if (g_strcmp0 (key_mgmt, "wpa-psk") == 0)
    return "WPA";

  if (g_strcmp0 (key_mgmt, "sae") == 0)
    return "SAE";

  return "nopass";
}


static char *
get_wifi_password (NMConnection *c)
{
  NMSettingWirelessSecurity *setting;
  const char *sec_type, *password;
  int wep_index;

  sec_type = get_connection_security_type (c);
  setting = nm_connection_get_setting_wireless_security (c);

  if (g_str_equal (sec_type, "nopass"))
    return NULL;

  if (g_str_equal (sec_type, "WEP")) {
    wep_index = nm_setting_wireless_security_get_wep_tx_keyidx (setting);
    password = nm_setting_wireless_security_get_wep_key (setting, wep_index);
  } else {
    password = nm_setting_wireless_security_get_psk (setting);
  }

  return g_strdup (password);
}


/* Generate a string representing the connection
 * An example generated text:
 *     WIFI:S:ssid;T:WPA;P:my-valid-pass;H:true;
 * Where,
 *   S = ssid, T = security, P = password, H = hidden (Optional)
 *
 * See https://github.com/zxing/zxing/wiki/Barcode-Contents#wi-fi-network-config-android-ios-11
 */
static char *
get_qr_string_for_connection (NMConnection *c)
{
  NMSettingWireless *setting;
  g_autofree char *ssid_text = NULL;
  g_autofree char *escaped_ssid = NULL;
  g_autofree char *password_str = NULL;
  g_autofree char *escaped_password = NULL;
  GString *string;
  GBytes *ssid;
  gboolean hidden;

  setting = nm_connection_get_setting_wireless (c);
  ssid = nm_setting_wireless_get_ssid (setting);

  if (!ssid)
    return NULL;

  string = g_string_new ("WIFI:S:");

  /* SSID */
  ssid_text = nm_utils_ssid_to_utf8 (g_bytes_get_data (ssid, NULL),
                                     g_bytes_get_size (ssid));
  escaped_ssid = escape_string (ssid_text, FALSE);
  g_string_append (string, escaped_ssid);
  g_string_append_c (string, ';');

  /* Security type */
  g_string_append (string, "T:");
  g_string_append (string, get_connection_security_type (c));
  g_string_append_c (string, ';');

  /* Password */
  g_string_append (string, "P:");
  password_str = get_wifi_password (c);
  escaped_password = escape_string (password_str, FALSE);
  if (escaped_password)
    g_string_append (string, escaped_password);
  g_string_append_c (string, ';');

  /* WiFi Hidden */
  hidden = nm_setting_wireless_get_hidden (setting);
  if (hidden)
    g_string_append (string, "H:true");
  g_string_append_c (string, ';');

  return g_string_free (string, FALSE);
}


static void
on_secrets_ready (GObject *object, GAsyncResult *result, gpointer data)
{
  PhoshWifiHotspotStatusPage *self = data;
  NMConnection *conn = NM_CONNECTION (object);
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) variant = NULL;
  g_autofree char *cnx_str = NULL;
  g_autofree char *password = NULL;
  g_autoptr (cairo_surface_t) surface = NULL;
  int scale;

  variant = nm_remote_connection_get_secrets_finish (NM_REMOTE_CONNECTION (conn), result, &error);
  if (variant == NULL) {
    g_warning ("Unable to fetch secrets: %s", error->message);
    gtk_image_set_from_icon_name (self->image, "face-sad-symbolic", -1);
    gtk_entry_set_text (self->entry, "");
    return;
  }
  if (!nm_connection_update_secrets (conn,
                                     NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
                                     variant,
                                     &error)) {
    g_warning ("Unable to set secrets: %s", error->message);
    gtk_image_set_from_icon_name (self->image, "face-sad-symbolic", -1);
    gtk_entry_set_text (self->entry, "");
    return;
  }

  cnx_str = get_qr_string_for_connection (conn);
  scale = gtk_widget_get_scale_factor (GTK_WIDGET (self->image));
  surface = qr_from_text (cnx_str, QR_CODE_SIZE, scale);
  password = get_wifi_password (conn);
  gtk_image_set_from_surface (self->image, surface);
  gtk_entry_set_text (self->entry, password);
  nm_connection_clear_secrets (conn);
}


static void
setup_hotspot_page (PhoshWifiHotspotStatusPage *self)
{
  NMActiveConnection *conn = phosh_wifi_manager_get_active_connection (self->wifi);
  NMRemoteConnection *remote = nm_active_connection_get_connection (conn);

  nm_remote_connection_get_secrets_async (NM_REMOTE_CONNECTION (remote),
                                          NM_SETTING_WIRELESS_SECURITY_SETTING_NAME,
                                          self->cancel,
                                          on_secrets_ready,
                                          self);
}


static void
on_wifi_notify (PhoshWifiHotspotStatusPage *self)
{
  gboolean wifi_absent = !phosh_wifi_manager_get_present (self->wifi);
  gboolean wifi_disabled = !phosh_wifi_manager_get_enabled (self->wifi);
  gboolean hotspot_disabled = !phosh_wifi_manager_is_hotspot_master (self->wifi);
  const char *icon_name;

  if (wifi_absent)
    icon_name = "network-wireless-hardware-disabled-symbolic";
  else if (wifi_disabled)
    icon_name = "network-wireless-disabled-symbolic";
  else
    icon_name = "network-wireless-hotspot-disabled-symbolic";

  phosh_status_page_placeholder_set_icon_name (self->placeholder, icon_name);
  gtk_widget_set_visible (GTK_WIDGET (self->turn_on_btn), !wifi_absent);

  if (hotspot_disabled) {
    gtk_stack_set_visible_child_name (self->stack, "empty_state");
  } else {
    gtk_stack_set_visible_child_name (self->stack, "hotspot_enabled");
    setup_hotspot_page (self);
  }
}


static void
on_icon_press (PhoshWifiHotspotStatusPage *self)
{
  gboolean visibility = gtk_entry_get_visibility (self->entry);
  const char *icon_name;

  if (visibility)
    icon_name = "view-reveal-symbolic";
  else
    icon_name = "view-conceal-symbolic";

  gtk_entry_set_visibility (self->entry, !visibility);
  gtk_entry_set_icon_from_icon_name (self->entry, GTK_ENTRY_ICON_SECONDARY, icon_name);
}


static void
on_turn_on_clicked (PhoshWifiHotspotStatusPage *self)
{
  gboolean wifi_disabled = !phosh_wifi_manager_get_enabled (self->wifi);

  if (wifi_disabled)
    phosh_wifi_manager_set_enabled (self->wifi, TRUE);
  else
    phosh_wifi_manager_set_hotspot_master (self->wifi, TRUE);
}


static void
phosh_wifi_hotspot_status_page_dispose (GObject *object)
{
  PhoshWifiHotspotStatusPage *self = PHOSH_WIFI_HOTSPOT_STATUS_PAGE (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  if (self->wifi)
    g_signal_handlers_disconnect_by_data (self->wifi, self);

  G_OBJECT_CLASS (phosh_wifi_hotspot_status_page_parent_class)->dispose (object);
}


static void
phosh_wifi_hotspot_status_page_class_init (PhoshWifiHotspotStatusPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_wifi_hotspot_status_page_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/wifi-hotspot-quick-setting/status-page.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, image);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, placeholder);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, ssid);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, stack);
  gtk_widget_class_bind_template_child (widget_class, PhoshWifiHotspotStatusPage, turn_on_btn);
  gtk_widget_class_bind_template_callback (widget_class, on_turn_on_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_icon_press);

  gtk_widget_class_set_css_name (widget_class, "phosh-wifi-hotspot-status-page");
}


static void
phosh_wifi_hotspot_status_page_init (PhoshWifiHotspotStatusPage *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->cancel = g_cancellable_new ();
  self->wifi = phosh_shell_get_wifi_manager (phosh_shell_get_default ());

  g_return_if_fail (PHOSH_IS_WIFI_MANAGER (self->wifi));

  g_object_connect (self->wifi,
                    "swapped-object-signal::notify::present",
                    G_CALLBACK (on_wifi_notify), self,
                    "swapped-object-signal::notify::enabled",
                    G_CALLBACK (on_wifi_notify), self,
                    "swapped-object-signal::notify::is-hotspot-master",
                    G_CALLBACK (on_wifi_notify), self,
                    NULL);
  g_object_bind_property (self->wifi, "ssid", self->ssid, "label", G_BINDING_SYNC_CREATE);
}


GtkWidget *
phosh_wifi_hotspot_status_page_new (void)
{
  return g_object_new (PHOSH_TYPE_WIFI_HOTSPOT_STATUS_PAGE, NULL);
}
