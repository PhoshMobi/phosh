/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-rotation-manager"

#include "phosh-config.h"
#include "rotation-manager.h"
#include "shell-priv.h"
#include "sensor-proxy-manager.h"
#include "util.h"

#define ORIENTATION_LOCK_SCHEMA_ID "org.gnome.settings-daemon.peripherals.touchscreen"
#define ORIENTATION_LOCK_KEY       "orientation-lock"

/**
 * PhoshRotationManager:
 *
 * The Rotation Manager
 *
 * #PhoshRotationManager is responsible for managing the rotation of
 * a given #PhoshMonitor. Depending on the #PhoshRotationManagerMode
 * this can happen by interfacing with a #PhoshSensorProxyManager or
 * by setting the #PhoshMonitorTransform explicitly.
 * It also takes the #PhoshLockscreenManager:locked status into account
 * to ensure the lockscreen is rotated accordingly on small phones.
 */

enum {
  PROP_0,
  PROP_SENSOR_PROXY_MANAGER,
  PROP_LOCKSCREEN_MANAGER,
  PROP_ORIENTATION_LOCKED,
  PROP_MONITOR,
  PROP_MODE,
  PROP_TRANSFORM,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

typedef struct _PhoshRotationManager {
  GObject                  parent;

  gboolean                 claimed;
  GCancellable            *cancel;
  PhoshSensorProxyManager *sensor_proxy_manager;
  PhoshLockscreenManager  *lockscreen_manager;
  PhoshMonitor            *monitor;
  PhoshMonitorTransform    transform;
  PhoshMonitorTransform    prelock_transform;
  gboolean                 blanked;

  GSettings               *settings;
  gboolean                 orientation_locked;

  PhoshRotationManagerMode mode;
} PhoshRotationManager;

G_DEFINE_TYPE (PhoshRotationManager, phosh_rotation_manager, G_TYPE_OBJECT);


static void
apply_transform (PhoshRotationManager *self, PhoshMonitorTransform transform)
{
  PhoshMonitorTransform current;
  PhoshMonitorManager *monitor_manager = phosh_shell_get_monitor_manager (phosh_shell_get_default());

  g_return_if_fail (PHOSH_IS_MONITOR_MANAGER (monitor_manager));

  if (!self->monitor)
    return;

  current = phosh_monitor_get_transform (self->monitor);
  if (current == transform)
    return;

  g_debug ("Rotating %s to %d", self->monitor->name, transform);
  phosh_monitor_manager_set_monitor_transform (monitor_manager,
                                               self->monitor,
                                               transform);
  phosh_monitor_manager_apply_monitor_config (monitor_manager);
}

/**
 * match_orientation:
 * @self: The #PhoshRotationManager
 *
 * Match the screen orientation to the sensor output.
 * Do nothing if orientation lock is on or there's no
 * sensor claimed.
 *
 * Returns: %TRUE if the orientation was matched, otherwise %FALSE.
 */
static gboolean
match_orientation (PhoshRotationManager *self)
{
  const char *orient;
  PhoshMonitorTransform transform;

  if (self->orientation_locked || !self->claimed ||
      phosh_lockscreen_manager_get_locked (self->lockscreen_manager) ||
      self->mode == PHOSH_ROTATION_MANAGER_MODE_OFF)
    return FALSE;

  orient = phosh_dbus_sensor_proxy_get_accelerometer_orientation (
    PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));

  g_debug ("Orientation changed: %s", orient);

  if (!g_strcmp0 ("normal", orient)) {
    transform = PHOSH_MONITOR_TRANSFORM_NORMAL;
  } else if (!g_strcmp0 ("right-up", orient)) {
    transform = PHOSH_MONITOR_TRANSFORM_270;
  } else if (!g_strcmp0 ("bottom-up", orient)) {
    transform = PHOSH_MONITOR_TRANSFORM_180;
  } else if (!g_strcmp0 ("left-up", orient)) {
    transform = PHOSH_MONITOR_TRANSFORM_90;
  } else if (!g_strcmp0 ("undefined", orient)) {
    return FALSE; /* just leave as is */
  } else {
    g_warning ("Unknown orientation '%s'", orient);
    return FALSE;
  }

  apply_transform (self, transform);
  return TRUE;
}

static void
on_accelerometer_claimed (PhoshSensorProxyManager *sensor_proxy_manager,
                          GAsyncResult            *res,
                          PhoshRotationManager    *self)
{
  g_autoptr (GError) err = NULL;
  gboolean success;

  g_return_if_fail (PHOSH_IS_SENSOR_PROXY_MANAGER (sensor_proxy_manager));

  success = phosh_dbus_sensor_proxy_call_claim_accelerometer_finish (
    PHOSH_DBUS_SENSOR_PROXY (sensor_proxy_manager),
    res, &err);

  if (!success) {
    phosh_async_error_warn (err, "Failed to claim accelerometer");
    return;
  }

  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (sensor_proxy_manager == self->sensor_proxy_manager);

  g_debug ("Claimed accelerometer");
  self->claimed = TRUE;
  match_orientation (self);
}

static void
on_accelerometer_released (PhoshSensorProxyManager *sensor_proxy_manager,
                           GAsyncResult            *res,
                           PhoshRotationManager    *self)
{
  g_autoptr (GError) err = NULL;
  gboolean success;

  g_return_if_fail (PHOSH_IS_SENSOR_PROXY_MANAGER (sensor_proxy_manager));

  success = phosh_dbus_sensor_proxy_call_release_accelerometer_finish (
    PHOSH_DBUS_SENSOR_PROXY (sensor_proxy_manager),
    res, &err);

  if (!success) {
    phosh_async_error_warn (err, "Failed to release accelerometer cleanly");
    return;
  }

  g_debug ("Released accelerometer");
}

static void
phosh_rotation_manager_claim_accelerometer (PhoshRotationManager *self, gboolean claim)
{
  if (claim == self->claimed)
    return;

  if (!self->sensor_proxy_manager)
    return;

  g_debug ("Claiming accelerometer: %d", claim);
  if (claim) {
    phosh_dbus_sensor_proxy_call_claim_accelerometer (
      PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager),
      self->cancel,
      (GAsyncReadyCallback)on_accelerometer_claimed,
      self);
  } else {
    phosh_dbus_sensor_proxy_call_release_accelerometer (
      PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager),
      self->cancel,
      (GAsyncReadyCallback)on_accelerometer_released,
      self);
    /* Immediately mark the sensor as released */
    self->claimed = FALSE;
  }
}

static void
on_has_accelerometer_changed (PhoshRotationManager    *self,
                              GParamSpec              *pspec,
                              PhoshSensorProxyManager *proxy)
{
  gboolean has_accel;
  PhoshRotationManagerMode mode;

  has_accel = phosh_dbus_sensor_proxy_get_has_accelerometer (
    PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));

  g_debug ("Found %s accelerometer", has_accel ? "a" : "no");

  mode = has_accel ? PHOSH_ROTATION_MANAGER_MODE_SENSOR : PHOSH_ROTATION_MANAGER_MODE_OFF;
  phosh_rotation_manager_set_mode (self, mode);
}

/**
 * fixup_lockscreen_orientation:
 * @self: The PhoshRotationManager
 * @lock: %True if the screen is being locked, %FALSE for unlock
 *
 * On phones the lock screen doesn't work in landscape so fix that up
 * by rotating to portrait on lock and back to the old orientation on
 * unlock.
 *
 * See https://source.puri.sm/Librem5/phosh/-/issues/388
 *
 * Keep all of this local to this function.
 */
static void
fixup_lockscreen_orientation (PhoshRotationManager *self, gboolean lock)
{
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshModeManager *mode_manager = phosh_shell_get_mode_manager(shell);
  PhoshMonitorTransform transform;

  g_return_if_fail (PHOSH_IS_MODE_MANAGER (mode_manager));

  if (!self->monitor)
    return;

  /* Only bother on phones */
  if (phosh_mode_manager_get_device_type(mode_manager) != PHOSH_MODE_DEVICE_TYPE_PHONE &&
      phosh_mode_manager_get_device_type(mode_manager) != PHOSH_MODE_DEVICE_TYPE_UNKNOWN)
    return;

  /* Don't mess with transforms on external screens either */
  if (!phosh_monitor_is_builtin (self->monitor))
    return;

  if (lock) {
    if (self->prelock_transform == -1) {
      self->prelock_transform = phosh_monitor_get_transform (self->monitor);
      g_debug ("Saving prelock transform %d", self->prelock_transform);
    }

    /* Use prelock transform if portrait, else use normal */
    transform = (self->prelock_transform % 2) == 0 ? self->prelock_transform :
      PHOSH_MONITOR_TRANSFORM_NORMAL;
    g_debug ("Forcing portrait transform: %d", transform);
  } else {
    if (self->prelock_transform == -1) {
      g_warning ("Prelock transform invalid");
      self->prelock_transform = PHOSH_MONITOR_TRANSFORM_NORMAL;
    }
    g_debug ("Restoring transform %d", self->prelock_transform);
    transform = self->prelock_transform;
    self->prelock_transform = -1;
  }

  apply_transform (self, transform);
}


static void
claim_or_release_accelerometer (PhoshRotationManager *self)
{
  gboolean claim = TRUE;

  /* No need for accel on screen blank, saves power */
  if (phosh_shell_get_state (phosh_shell_get_default ()) & PHOSH_STATE_BLANKED)
    claim = FALSE;

  /* No need for accel on orientation lock, saves power */
  if (self->orientation_locked)
    claim = FALSE;

  /* No need for accel when automatic rotation is not requested or possible */
  if (self->mode == PHOSH_ROTATION_MANAGER_MODE_OFF)
    claim = FALSE;

  if (claim == self->claimed)
    return;

  phosh_rotation_manager_claim_accelerometer (self, claim);
}


static void
on_shell_state_changed (PhoshRotationManager  *self,
                        GParamSpec            *pspec,
                        PhoshShell            *shell)
{
  PhoshShellStateFlags state;
  gboolean blanked;

  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (PHOSH_IS_SHELL (shell));

  state = phosh_shell_get_state (shell);
  g_debug ("Shell state changed: %d", state);

  blanked = !!(phosh_shell_get_state (phosh_shell_get_default ()) & PHOSH_STATE_BLANKED);

  /* We're only interested in blank state changed */
  if (blanked == self->blanked)
    return;
  self->blanked = blanked;

  /* Claim/unclaim sensor if blank state changed */
  claim_or_release_accelerometer (self);

  if (blanked)
    return;

  /* Fixup lockscreen orientation on unblank */
  if (!blanked && phosh_lockscreen_manager_get_locked (self->lockscreen_manager)) {
      fixup_lockscreen_orientation (self, TRUE);
  }
}


static void
on_lockscreen_manager_locked (PhoshRotationManager *self, GParamSpec *pspec,
                              PhoshLockscreenManager *lockscreen_manager)
{
  gboolean locked;

  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (lockscreen_manager));

  locked = phosh_lockscreen_manager_get_locked (self->lockscreen_manager);

  if (locked) {
    fixup_lockscreen_orientation (self, TRUE);
  } else {
    gboolean matched = match_orientation (self);

    /* If we couldn't match the orientation (either because it was inhibited or it failed)
       use the last known transform */
    if (!matched)
      fixup_lockscreen_orientation (self, FALSE);
  }
}


static void
on_accelerometer_orientation_changed (PhoshRotationManager    *self,
                                      GParamSpec              *pspec,
                                      PhoshSensorProxyManager *sensor)
{
  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (self->sensor_proxy_manager == sensor);

  match_orientation (self);
}


static void
on_monitor_configured (PhoshRotationManager   *self,
                       PhoshMonitor           *monitor)
{
  PhoshMonitorTransform transform;

  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));

  transform = phosh_monitor_get_transform (monitor);
  if (transform == self->transform)
    return;

  self->transform = transform;
  g_debug ("Rotation-manager transform %d", transform);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TRANSFORM]);
}


static void
phosh_rotation_manager_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  PhoshRotationManager *self = PHOSH_ROTATION_MANAGER (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    /* construct only */
    self->sensor_proxy_manager = g_value_dup_object (value);
    break;
  case PROP_LOCKSCREEN_MANAGER:
    /* construct only */
    self->lockscreen_manager = g_value_dup_object (value);
    break;
  case PROP_MONITOR:
    phosh_rotation_manager_set_monitor (self, g_value_get_object (value));
    break;
  case PROP_ORIENTATION_LOCKED:
    phosh_rotation_manager_set_orientation_locked (self,
                                                   g_value_get_boolean (value));
    break;
  case PROP_MODE:
    phosh_rotation_manager_set_mode (self, g_value_get_enum (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phosh_rotation_manager_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  PhoshRotationManager *self = PHOSH_ROTATION_MANAGER (object);

  switch (property_id) {
  case PROP_SENSOR_PROXY_MANAGER:
    g_value_set_object (value, self->sensor_proxy_manager);
    break;
  case PROP_LOCKSCREEN_MANAGER:
    g_value_set_object (value, self->lockscreen_manager);
    break;
  case PROP_ORIENTATION_LOCKED:
    g_value_set_boolean (value, self->orientation_locked);
    break;
  case PROP_MODE:
    g_value_set_enum (value, self->mode);
    break;
  case PROP_TRANSFORM:
    g_value_set_enum (value, self->transform);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
phosh_rotation_manager_constructed (GObject *object)
{
  PhoshRotationManager *self = PHOSH_ROTATION_MANAGER (object);

  G_OBJECT_CLASS (phosh_rotation_manager_parent_class)->constructed (object);

  self->settings = g_settings_new (ORIENTATION_LOCK_SCHEMA_ID);

  g_settings_bind (self->settings, ORIENTATION_LOCK_KEY,
                   self, "orientation-locked",
                   G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  g_signal_connect_swapped (self->lockscreen_manager,
                            "notify::locked",
                            (GCallback) on_lockscreen_manager_locked,
                            self);
  on_lockscreen_manager_locked (self, NULL, self->lockscreen_manager);

  self->blanked = !!(phosh_shell_get_state (phosh_shell_get_default ()) & PHOSH_STATE_BLANKED);
  g_signal_connect_object (phosh_shell_get_default (),
                           "notify::shell-state",
                           G_CALLBACK (on_shell_state_changed),
                           self,
                           G_CONNECT_SWAPPED);

  if (!self->sensor_proxy_manager) {
    g_message ("Got no sensor-proxy, no automatic rotation");
    return;
  }

  g_signal_connect_swapped (self->sensor_proxy_manager,
                            "notify::accelerometer-orientation",
                            (GCallback) on_accelerometer_orientation_changed,
                            self);

  g_signal_connect_swapped (self->sensor_proxy_manager,
                            "notify::has-accelerometer",
                            (GCallback) on_has_accelerometer_changed,
                            self);
  on_has_accelerometer_changed (self, NULL, self->sensor_proxy_manager);
}


static void
phosh_rotation_manager_dispose (GObject *object)
{
  PhoshRotationManager *self = PHOSH_ROTATION_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  g_clear_object (&self->settings);

  if (self->sensor_proxy_manager) {
    g_signal_handlers_disconnect_by_data (self->sensor_proxy_manager,
                                          self);
    /* Sync call since we're going away */
    phosh_dbus_sensor_proxy_call_release_accelerometer_sync (
      PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager), NULL, NULL);
    g_clear_object (&self->sensor_proxy_manager);
  }

  if (self->lockscreen_manager) {
    g_signal_handlers_disconnect_by_data (self->lockscreen_manager,
                                          self);
    g_clear_object (&self->lockscreen_manager);
  }

  if (self->monitor) {
    g_signal_handlers_disconnect_by_data (self->monitor,
                                          self);
    g_clear_object (&self->monitor);
  }

  G_OBJECT_CLASS (phosh_rotation_manager_parent_class)->dispose (object);
}

static void
phosh_rotation_manager_class_init (PhoshRotationManagerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;

  object_class->constructed = phosh_rotation_manager_constructed;
  object_class->dispose = phosh_rotation_manager_dispose;

  object_class->set_property = phosh_rotation_manager_set_property;
  object_class->get_property = phosh_rotation_manager_get_property;

  props[PROP_SENSOR_PROXY_MANAGER] =
    g_param_spec_object (
      "sensor-proxy-manager",
      "Sensor proxy manager",
      "The object inerfacing with iio-sensor-proxy",
      PHOSH_TYPE_SENSOR_PROXY_MANAGER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_LOCKSCREEN_MANAGER] =
    g_param_spec_object (
      "lockscreen-manager",
      "Lockscren manager",
      "The object managing the lock screen",
      PHOSH_TYPE_LOCKSCREEN_MANAGER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_MONITOR] =
    g_param_spec_object (
      "monitor",
      "Monitor",
      "The monitor to rotate",
      PHOSH_TYPE_MONITOR,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_ORIENTATION_LOCKED] =
    g_param_spec_boolean (
      "orientation-locked",
      "Screen orientation locked",
      "Whether the screen orientation is locked",
      TRUE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_MODE] =
    g_param_spec_enum (
      "mode",
      "Rotation mode",
      "The current rotation mode",
      PHOSH_TYPE_ROTATION_MANAGER_MODE,
      PHOSH_ROTATION_MANAGER_MODE_OFF,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  props[PROP_TRANSFORM] =
    g_param_spec_enum ("transform",
                       "Transform",
                       "Monitor transform of the rotation monitor",
                       PHOSH_TYPE_MONITOR_TRANSFORM,
                       PHOSH_MONITOR_TRANSFORM_NORMAL,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}

static void
phosh_rotation_manager_init (PhoshRotationManager *self)
{
  self->cancel = g_cancellable_new ();
}


PhoshRotationManager *
phosh_rotation_manager_new (PhoshSensorProxyManager *sensor_proxy_manager,
                            PhoshLockscreenManager  *lockscreen_manager,
                            PhoshMonitor *monitor)
{
  return g_object_new (PHOSH_TYPE_ROTATION_MANAGER,
                       "sensor-proxy-manager", sensor_proxy_manager,
                       "lockscreen-manager", lockscreen_manager,
                       "monitor", monitor,
                       NULL);
}

void
phosh_rotation_manager_set_orientation_locked (PhoshRotationManager *self, gboolean locked)
{
  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));

  if (locked == self->orientation_locked)
    return;

  self->orientation_locked = locked;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ORIENTATION_LOCKED]);

  /* We don't need accel if orientation locked and vice versa */
  if (self->claimed == self->orientation_locked)
    claim_or_release_accelerometer (self);
  else
    match_orientation (self);
}

gboolean
phosh_rotation_manager_get_orientation_locked (PhoshRotationManager *self)
{
  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (self), TRUE);

  return self->orientation_locked;
}

PhoshRotationManagerMode
phosh_rotation_manager_get_mode (PhoshRotationManager *self)
{
  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (self), PHOSH_ROTATION_MANAGER_MODE_OFF);

  return self->mode;
}

/**
 * phosh_rotation_manager_set_mode:
 * @self: The #PhoshRotationManager
 * @mode: The #PhoshRotationManagerMode to set
 *
 * Sets the given mode.
 * Returns: %TRUE if setting the mode was possible, otherwise %FALSE (e.g. when trying
 * to set %PHOSH_ROTATION_MANAGER_MODE_SENSOR without having a sensor.
 */
gboolean
phosh_rotation_manager_set_mode (PhoshRotationManager *self, PhoshRotationManagerMode mode)
{
  gboolean has_accel;

  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (self), FALSE);

  if (mode == self->mode)
    return TRUE;

  has_accel = phosh_dbus_sensor_proxy_get_has_accelerometer (
    PHOSH_DBUS_SENSOR_PROXY (self->sensor_proxy_manager));

  if (mode == PHOSH_ROTATION_MANAGER_MODE_SENSOR && !has_accel)
    return FALSE;

  self->mode = mode;

  g_debug ("Setting mode: %d", mode);
  claim_or_release_accelerometer (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODE]);
  return TRUE;
}


void
phosh_rotation_manager_set_transform (PhoshRotationManager   *self,
                                      PhoshMonitorTransform  transform)
{
  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (self->mode == PHOSH_ROTATION_MANAGER_MODE_OFF);

  apply_transform (self, transform);
}

PhoshMonitorTransform
phosh_rotation_manager_get_transform (PhoshRotationManager *self)
{
  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (self), PHOSH_MONITOR_TRANSFORM_NORMAL);
  g_return_val_if_fail (PHOSH_IS_MONITOR (self->monitor), PHOSH_MONITOR_TRANSFORM_NORMAL);

  return self->monitor->transform;
}

/**
 * phosh_rotation_manager_get_monitor:
 * @self: The PhoshRotationManager
 *
 * Get the monitor this manager currently acts on
 *
 * Returns:(transfer none): The current monitor
 */
PhoshMonitor *
phosh_rotation_manager_get_monitor (PhoshRotationManager *self)
{
  g_return_val_if_fail (PHOSH_IS_ROTATION_MANAGER (self), NULL);

  return self->monitor;
}


void
phosh_rotation_manager_set_monitor (PhoshRotationManager *self, PhoshMonitor *monitor)
{
  g_return_if_fail (PHOSH_IS_ROTATION_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor) || monitor == NULL);

  g_debug ("Using monitor %p", monitor);

  if (self->monitor == monitor)
    return;

  if (self->monitor) {
    g_signal_handlers_disconnect_by_data (self->monitor, self);
    g_clear_object (&self->monitor);
  }

  if (monitor == NULL) {
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MONITOR]);
    return;
  }

  self->monitor = g_object_ref (monitor);
  g_signal_connect_swapped (self->monitor,
                            "configured",
                            G_CALLBACK (on_monitor_configured),
                            self);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MONITOR]);
}
