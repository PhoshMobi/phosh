/*
 * Copyright (C) 2022 Purism SPC
 *               2023-2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-ambient"

#include "phosh-config.h"
#include "animation.h"
#include "fader.h"
#include "ambient.h"
#include "shell-priv.h"
#include "sensor-proxy-manager.h"
#include "util.h"

#define INTERFACE_SCHEMA        "org.gnome.desktop.interface"
#define HIGH_CONTRAST_THEME     "HighContrast"
#define KEY_GTK_THEME           "gtk-theme"
#define KEY_ICON_THEME          "icon-theme"

#define PHOSH_SCHEMA            "sm.puri.phosh"
#define KEY_AUTOMATIC_HC        "automatic-high-contrast"
#define KEY_AUTOMATIC_HC_THRESHOLD  "automatic-high-contrast-threshold"

#define POWER_SCHEMA "org.gnome.settings-daemon.plugins.power"
#define KEY_AMBIENT_ENABLED "ambient-enabled"

#define NUM_VALUES              3

/**
 * PhoshAmbient:
 *
 * Ambient light sensor handling
 *
 * #PhoshAmbient handles enabling and disabling the ambient detection
 * based and toggles related actions.
 */

enum {
  PROP_0,
  PROP_SENSOR_PROXY_MANAGER,
  PROP_AUTO_BRIGHTNESS_ENABLED,
  PROP_LIGHT_LEVEL,
  PROP_HAS_SENSOR,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];


typedef struct _PhoshAmbient {
  GObject       parent;

  int           claimed;
  PhoshSensorProxyManager *sensor_proxy_manager;
  GCancellable *cancel;

  GSettings    *phosh_settings;
  GSettings    *interface_settings;
  GSettings    *power_settings;
  gboolean      auto_hc;
  gboolean      use_hc;
  gboolean      auto_brightness;
  double        light_level;
  gboolean      blanked;
  gboolean      has_sensor;

  guint         sample_id;
  GArray       *values;

  PhoshFader   *fader;
  guint         fader_id;
} PhoshAmbient;

G_DEFINE_TYPE (PhoshAmbient, phosh_ambient, G_TYPE_OBJECT);


static void
phosh_ambient_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  PhoshAmbient *self = PHOSH_AMBIENT (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    /* construct only */
    self->sensor_proxy_manager = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_ambient_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PhoshAmbient *self = PHOSH_AMBIENT (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    g_value_set_object (value, self->sensor_proxy_manager);
    break;
  case PROP_AUTO_BRIGHTNESS_ENABLED:
    g_value_set_boolean (value, self->auto_brightness);
    break;
  case PROP_LIGHT_LEVEL:
    g_value_set_double (value, self->light_level);
    break;
  case PROP_HAS_SENSOR:
    g_value_set_boolean (value, self->has_sensor);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static gboolean
on_fade_in_done (PhoshAmbient *self)
{
  if (self->use_hc) {
    g_settings_set_string (self->interface_settings, KEY_GTK_THEME, HIGH_CONTRAST_THEME);
  } else {
    g_settings_reset (self->interface_settings, KEY_GTK_THEME);
    g_settings_reset (self->interface_settings, KEY_ICON_THEME);
  }

  phosh_fader_hide (self->fader);
  return G_SOURCE_REMOVE;
}


static void
switch_theme (PhoshAmbient *self, gboolean use_hc)
{
  const char *style_class;

  if (use_hc == self->use_hc)
    return;

  style_class = use_hc ? "phosh-fader-theme-to-hc" : "phosh-fader-theme-from-hc";
  self->fader = g_object_new (PHOSH_TYPE_FADER,
                              "style-class", style_class,
                              "fade-out-time", 1500,
                              "fade-out-type", PHOSH_ANIMATION_TYPE_EASE_IN_QUINTIC,
                              NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->fader), TRUE);

  self->fader_id = g_timeout_add (100 * PHOSH_ANIMATION_SLOWDOWN,
                                  G_SOURCE_FUNC (on_fade_in_done),
                                  self);
  g_source_set_name_by_id (self->fader_id, "[phosh] ambient fader");

  self->use_hc = use_hc;
}


static gboolean
on_ambient_sample_for_hc (gpointer data)
{
  PhoshAmbient *self = PHOSH_AMBIENT (data);
  double level, threshold;
  double avg = 0.0;
  gboolean use_hc;

  level = phosh_dbus_sensor_proxy_get_light_level (PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));
  g_array_append_val (self->values, level);

  if (self->values->len < NUM_VALUES)
    return G_SOURCE_CONTINUE;

  for (int i = 0; i < self->values->len; i++)
    avg += g_array_index (self->values, double, i);

  avg /= self->values->len;
  threshold = g_settings_get_uint (self->phosh_settings, KEY_AUTOMATIC_HC_THRESHOLD);
  use_hc = avg > threshold;

  g_debug ("Avg: %f Switching theme to hc: %d", avg, use_hc);
  switch_theme (self, use_hc);

  g_array_set_size (self->values, 0);
  self->sample_id = 0;
  return G_SOURCE_REMOVE;
}


static void
stop_high_contrast_sampling (PhoshAmbient *self)
{
  g_clear_handle_id (&self->sample_id, g_source_remove);
  g_array_set_size (self->values, 0);
}


static void
check_high_contrast (PhoshAmbient *self, double level)
{
  double hyst, threshold;
  gboolean wants_hc;

  if (!self->auto_hc)
    return;

  /* Currently sampling, ignoring changes */
  if (self->sample_id)
    return;

  threshold = g_settings_get_uint (self->phosh_settings, KEY_AUTOMATIC_HC_THRESHOLD);
  /* Use a bit of hysteresis to not switch too often around the threshold */
  hyst = self->use_hc ? 0.9 : 1.1;
  threshold *= hyst;

  wants_hc = level > threshold;
  /* New value wouldn't change anything, nothing to do */
  if (wants_hc == self->use_hc)
    return;

  /* new value would change hc mode, sample to see if it should stick */
  g_return_if_fail (self->sample_id == 0);
  g_return_if_fail (self->values->len == 0);
  g_array_append_val (self->values, level);
  self->sample_id = g_timeout_add_seconds (1, on_ambient_sample_for_hc, self);
  g_source_set_name_by_id (self->sample_id, "[phosh] ambient_sample_for_hc");
}


static void
on_ambient_light_level_changed (PhoshAmbient            *self,
                                GParamSpec              *pspec,
                                PhoshSensorProxyManager *sensor)
{
  double level;
  const char *unit;
  PhoshDBusSensorProxy *proxy;

  if (!self->claimed)
    return;

  proxy = PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager);
  level = phosh_dbus_sensor_proxy_get_light_level (proxy);
  unit = phosh_dbus_sensor_proxy_get_light_level_unit (proxy);
  if (!unit || g_ascii_strcasecmp (unit, "lux") != 0) {
    /* For vendor values we don't know if small or large values mean bright or dark so be conservative */
    g_warning_once ("Unknown light level unit %s", unit);
    return;
  }

  g_debug ("Ambient light changed: %.2f %s", level, unit);
  if (!G_APPROX_VALUE (self->light_level, level, FLT_EPSILON)) {
    self->light_level = level;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_LIGHT_LEVEL]);
  }

  check_high_contrast (self, level);
}


static void
update_auto_brightness_enabled (PhoshAmbient *self)
{
  gboolean auto_brightness;

  g_return_if_fail (self->claimed >= 0);

  auto_brightness = (self->claimed &&
                     g_settings_get_boolean (self->power_settings, KEY_AMBIENT_ENABLED));

  if (self->auto_brightness == auto_brightness)
    return;

  self->auto_brightness = auto_brightness;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_AUTO_BRIGHTNESS_ENABLED]);
}


static void
on_ambient_claimed (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusSensorProxy *proxy = PHOSH_DBUS_SENSOR_PROXY (source_object);
  PhoshAmbient *self = PHOSH_AMBIENT (user_data);
  g_autoptr (GError) err = NULL;
  gboolean success;

  g_return_if_fail (PHOSH_IS_SENSOR_PROXY_MANAGER (proxy));
  g_return_if_fail (proxy == PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));

  success = phosh_dbus_sensor_proxy_call_claim_light_finish (proxy, res, &err);
  if (!success) {
    g_warning ("Failed to claim ambient sensor: %s", err->message);
    return;
  }

  g_debug ("Claimed ambient sensor");
  self->claimed++;

  update_auto_brightness_enabled (self);
  on_ambient_light_level_changed (self, NULL, self->sensor_proxy_manager);
}


static void
on_ambient_released (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusSensorProxy *proxy = PHOSH_DBUS_SENSOR_PROXY (source_object);
  PhoshAmbient *self = PHOSH_AMBIENT (user_data);
  g_autoptr (GError) err = NULL;
  gboolean success;

  g_return_if_fail (self->claimed > 0);
  g_return_if_fail (PHOSH_IS_SENSOR_PROXY_MANAGER (proxy));
  g_return_if_fail (proxy == PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));

  success = phosh_dbus_sensor_proxy_call_release_light_finish (proxy, res, &err);
  if (success) {
    g_debug ("Released ambient light sensor");
  } else {
    if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      return;
    g_warning ("Failed to release ambient sensor: %s", err->message);
  }

  self->claimed--;
  update_auto_brightness_enabled (self);
  stop_high_contrast_sampling (self);
}


static void
phosh_ambient_claim_light (PhoshAmbient *self, gboolean claim)
{
  PhoshDBusSensorProxy *proxy = PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager);

  if (claim == !!self->claimed)
    return;

  g_debug ("Claiming sensor: %d", claim);
  if (claim) {
    phosh_dbus_sensor_proxy_call_claim_light (proxy, self->cancel, on_ambient_claimed, self);
  } else {
    phosh_dbus_sensor_proxy_call_release_light (proxy, self->cancel, on_ambient_released, self);
  }
}


static void
maybe_claim (PhoshAmbient *self)
{
  gboolean auto_brightness, auto_hc, claim;

  g_return_if_fail (self->claimed >= 0);

  auto_brightness = g_settings_get_boolean (self->power_settings, KEY_AMBIENT_ENABLED);
  auto_hc = g_settings_get_boolean (self->phosh_settings, KEY_AUTOMATIC_HC);
  claim = auto_hc || auto_brightness;

  g_debug ("Auto brightness enabled: %d, Auto HC enabled: %d, claim: %d",
           auto_brightness, auto_hc, claim);

  if (self->auto_hc == auto_hc &&
      self->auto_brightness == auto_brightness &&
      !!self->claimed == claim) {
    return;
  }

  self->auto_hc = auto_hc;
  update_auto_brightness_enabled (self);

  if (claim) {
    if (self->claimed) {
      g_debug ("Already claimed, triggering update");
      on_ambient_light_level_changed (self, NULL, self->sensor_proxy_manager);
    } else {
      phosh_ambient_claim_light (self, TRUE);
    }
  } else {
    phosh_ambient_claim_light (self, FALSE);
    /* Switch back to normal theme */
    if (!self->auto_hc)
      switch_theme (self, FALSE);
  }
}


static void
on_settings_changed (PhoshAmbient *self)
{
  maybe_claim (self);
}


static void
on_has_ambient_light_changed (PhoshAmbient         *self,
                              GParamSpec           *pspec,
                              PhoshDBusSensorProxy *proxy)
{
  gboolean has_sensor;

  g_return_if_fail (self->claimed >= 0);

  has_sensor = phosh_dbus_sensor_proxy_get_has_ambient_light (proxy);

  if (has_sensor != self->has_sensor) {
    g_debug ("Ambient light sensor %sappeared", has_sensor ? "" : "dis");
    self->has_sensor = has_sensor;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_HAS_SENSOR]);
  }

  if (has_sensor) {
    g_debug ("Ambient sensor appeared");
    maybe_claim (self);
    return;
  }

  if (!self->claimed)
    return;

  g_debug ("Ambient sensor disappeared, marking unclaimed");
  self->claimed--;
  update_auto_brightness_enabled (self);
  stop_high_contrast_sampling (self);
}


static void
on_shell_state_changed (PhoshAmbient *self, GParamSpec *pspec, PhoshShell *shell)
{
  gboolean blanked;
  PhoshShellStateFlags state;

  g_return_if_fail (PHOSH_IS_AMBIENT (self));
  g_return_if_fail (PHOSH_IS_SHELL (shell));

  state = phosh_shell_get_state (shell);
  blanked = !!(state & PHOSH_STATE_BLANKED);

  if (self->blanked == blanked)
    return;
  self->blanked = blanked;

  g_debug ("Shell blanked: %d", self->blanked);
  /* Claim / unclaim the sensor on screen unblank / blank */
  if (blanked)
    phosh_ambient_claim_light (self, FALSE);
  else
    maybe_claim (self);
}


static void
phosh_ambient_constructed (GObject *object)
{
  PhoshAmbient *self = PHOSH_AMBIENT (object);

  G_OBJECT_CLASS (phosh_ambient_parent_class)->constructed (object);

  g_object_connect (self->sensor_proxy_manager,
                    "swapped-signal::notify::light-level",
                    on_ambient_light_level_changed,
                    self,
                    "swapped-signal::notify::has-ambient-light",
                    on_has_ambient_light_changed,
                    self,
                    NULL);

  g_object_connect (self->phosh_settings,
                    "swapped-signal::changed::" KEY_AUTOMATIC_HC,
                    G_CALLBACK (on_settings_changed),
                    self,
                    "swapped-signal::changed::" KEY_AUTOMATIC_HC_THRESHOLD,
                    G_CALLBACK (on_settings_changed),
                    self,
                    NULL);

  g_signal_connect_object (phosh_shell_get_default (),
                           "notify::shell-state",
                           G_CALLBACK (on_shell_state_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_swapped (self->power_settings,
                            "changed::" KEY_AMBIENT_ENABLED,
                            G_CALLBACK (on_settings_changed),
                            self);

  on_has_ambient_light_changed (self, NULL, PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));
}


static void
phosh_ambient_dispose (GObject *object)
{
  PhoshAmbient *self = PHOSH_AMBIENT (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  g_clear_handle_id (&self->sample_id, g_source_remove);
  g_clear_pointer (&self->values, g_array_unref);

  if (self->sensor_proxy_manager) {
    g_signal_handlers_disconnect_by_data (self->sensor_proxy_manager, self);
    phosh_dbus_sensor_proxy_call_release_light_sync (
      PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager), NULL, NULL);
    g_clear_object (&self->sensor_proxy_manager);
  }

  g_clear_object (&self->phosh_settings);
  g_clear_object (&self->interface_settings);

  g_clear_handle_id (&self->fader_id, g_source_remove);
  g_clear_pointer (&self->fader, phosh_cp_widget_destroy);

  g_clear_object (&self->power_settings);

  G_OBJECT_CLASS (phosh_ambient_parent_class)->dispose (object);
}


static void
phosh_ambient_class_init (PhoshAmbientClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phosh_ambient_constructed;
  object_class->dispose = phosh_ambient_dispose;

  object_class->set_property = phosh_ambient_set_property;
  object_class->get_property = phosh_ambient_get_property;

  props[PROP_SENSOR_PROXY_MANAGER] =
    g_param_spec_object ("sensor-proxy-manager", "", "",
                         PHOSH_TYPE_SENSOR_PROXY_MANAGER,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshAmbient:auto-brightness-enabled:
   *
   * If `TRUE` the display brightness should currently be adjusted to
   * ambient light levels
   */
  props[PROP_AUTO_BRIGHTNESS_ENABLED] =
    g_param_spec_boolean ("auto-brightness-enabled", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshAmbient:light-level:
   *
   * The last light level reading of the ambient light sensor.
   */
  props[PROP_LIGHT_LEVEL] =
    g_param_spec_double ("light-level", "", "",
                         0.0, G_MAXDOUBLE, 0.0,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshAmbient:has-sensor:
   *
   * Whether a ambient light sensor is available or not.
   */
  props[PROP_HAS_SENSOR] =
    g_param_spec_boolean ("has-sensor", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);


  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_ambient_init (PhoshAmbient *self)
{
  g_autofree char *theme_name = NULL;

  /* Ensure initial sync */
  self->light_level = -1.0;
  self->blanked = -1;
  self->cancel = g_cancellable_new ();

  self->values = g_array_new (FALSE, FALSE, sizeof(double));

  self->interface_settings = g_settings_new (INTERFACE_SCHEMA);
  self->phosh_settings = g_settings_new (PHOSH_SCHEMA);
  self->power_settings = g_settings_new (POWER_SCHEMA);

  /* Check whether we're already using the hc theme */
  theme_name = g_settings_get_string (self->interface_settings, KEY_GTK_THEME);
  if (g_strcmp0 (theme_name, HIGH_CONTRAST_THEME) == 0)
    self->use_hc = TRUE;
}


PhoshAmbient *
phosh_ambient_new (PhoshSensorProxyManager *sensor_proxy_manager)
{
  return g_object_new (PHOSH_TYPE_AMBIENT,
                       "sensor-proxy-manager", sensor_proxy_manager,
                       NULL);
}


gboolean
phosh_ambient_get_auto_brightness (PhoshAmbient *self)
{
  g_return_val_if_fail (PHOSH_IS_AMBIENT (self), FALSE);

  return self->auto_brightness;
}


double
phosh_ambient_get_light_level (PhoshAmbient *self)
{
  g_return_val_if_fail (PHOSH_IS_AMBIENT (self), FALSE);

  return self->light_level;
}
