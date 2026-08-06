/*
 * Copyright (C) 2019-2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-screen-saver-manager"

#include "screen-saver-manager.h"
#include "session-presence.h"
#include "shell-priv.h"
#include "idle-manager.h"
#include "login1-manager-dbus.h"
#include "login1-session-dbus.h"
#include "lockscreen-manager.h"
#include "session-presence.h"
#include "util.h"

#include <glib/gstdio.h>
#include <gio/gunixfdlist.h>

#define LONG_PRESS_TIMEOUT 1 /* seconds */

/**
 * PhoshScreenSaverManager:
 *
 * Provides the org.gnome.ScreenSaver DBus interface and handles logind's Session
 *
 * This handles the `org.gnome.ScreenSaver` DBus API for screen locking and unlocking.
 * It also handles the login1 session  parts since those are closely related and this
 * keeps #PhoshLockscreenManager free of any session related DBus handling.
 *
 * These settings influence screen blanking and locking:
 *
 * `org.gnome.desktop.session idle-delay`: The session is considered idle after that many
 * seconds of inactivity. This isn't monitored by phosh directly but is done by gnome-session that
 * in turn uses `GnomeIdleMonitor` which then uses `/org/gnome/Mutter/IdleMonitor/Core` on DBus.
 * `/org/gnome/Mutter/IdleMonitor/Core` is implemented by #PhoshIdleManager which in turn gets
 * it from phoc via a Wayland protocol.
 *
 * `org.gnome.desktop.screensaver` `lock-enabled`: Whether the screen should be locked after
 * the screen-saver is activated.
 *
 * `org.gnome.desktop.screensaver` `lock-delay`: How long after screen-saver activation should
 * the screen be locked.
 */

#define SCREEN_SAVER_DBUS_NAME "org.gnome.ScreenSaver"

#define LOGIN_BUS_NAME "org.freedesktop.login1"
#define LOGIN_OBJECT_PATH "/org/freedesktop/login1"

enum {
  PROP_0,
  PROP_LOCKSCREEN_MANAGER,
  PROP_LOCK_ENABLED,
  PROP_LOCK_DELAY,
  PROP_ACTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  PB_LONG_PRESS,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

static void phosh_screen_saver_manager_screen_saver_iface_init (PhoshDBusScreenSaverIface *iface);

typedef struct _PhoshScreenSaverManager
{
  PhoshDBusScreenSaverSkeleton parent;

  int idle_id;
  int dbus_name_id;
  PhoshLockscreenManager *lockscreen_manager;
  PhoshSessionPresence *presence;  /* gnome-session's presence interface */
  gboolean active;

  /* Power button */
  guint    long_press_id;

  GSettings *settings;
  gboolean lock_enabled;
  gboolean lock_delay;
  guint    lock_delay_timer_id;
  int      inhibit_pwr_btn_fd;
  int      inhibit_suspend_fd;
  PhoshMonitor *primary_monitor;

  PhoshDBusLoginSession *logind_session_proxy;
  PhoshDBusLoginManager *logind_manager_proxy;

  GCancellable          *cancel;
} PhoshScreenSaverManager;

G_DEFINE_TYPE_WITH_CODE (PhoshScreenSaverManager,
                         phosh_screen_saver_manager,
                         PHOSH_DBUS_TYPE_SCREEN_SAVER_SKELETON,
                         G_IMPLEMENT_INTERFACE (
                           PHOSH_DBUS_TYPE_SCREEN_SAVER,
                           phosh_screen_saver_manager_screen_saver_iface_init));


static void
on_lock_delay_timer_expired (gpointer data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (data);

  phosh_lockscreen_manager_set_locked (self->lockscreen_manager, TRUE);

  self->lock_delay_timer_id = 0;
}


static void
unarm_lock_delay_timer (PhoshScreenSaverManager *self, const char *reason)
{
  g_debug ("Unarming lock delay timer on %s", reason);
  g_clear_handle_id (&self->lock_delay_timer_id, g_source_remove);
}


static void
arm_lock_delay_timer (PhoshScreenSaverManager *self, gboolean active, gboolean lock)
{
  if (!active || !lock) {
    unarm_lock_delay_timer (self, "arm");
    return;
  }

  /* Already locked, no locking to do */
  if (phosh_lockscreen_manager_get_locked (self->lockscreen_manager))
    return;

  /* no delay, lock right away */
  if (self->lock_delay == 0) {
    unarm_lock_delay_timer (self, "arm");
    phosh_lockscreen_manager_set_locked (self->lockscreen_manager, TRUE);
    return;
  }

  /* Timer already ticking, don't rearm */
  if (self->lock_delay_timer_id)
    return;

  g_debug ("Arming lock delay timer for %d seconds", self->lock_delay);
  self->lock_delay_timer_id = g_timeout_add_seconds_once (self->lock_delay,
                                                          on_lock_delay_timer_expired,
                                                          self);
  g_source_set_name_by_id (self->lock_delay_timer_id, "[phosh] lock_delay_timer");
}


/* Activate or deactivate screen blank based on `active`. If `lock` is %TRUE locking
   is also enabled (honoring the configured delay) */
static void
screen_saver_set_active (PhoshScreenSaverManager *self, gboolean active, gboolean lock)
{
  if (self->active == active)
    return;

  g_debug ("Activating screen saver: %d, lock: %d, lock_delay: %d", active, lock,
    self->lock_delay);

  /* on_primary_monitor_power_mode_changed will update self->active once the power mode is set  */
  phosh_shell_enable_power_save (phosh_shell_get_default (), active);

  /* Don't wait for on_primary_monitor_power_mode_changed to activate the lock delay timer in
     case that fails on the compositor side - we don't want to miss the screen lock */
  arm_lock_delay_timer (self, active, lock);
}


static gboolean
on_long_press (gpointer data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (data);

  g_debug ("Power button long press detected");

  phosh_trigger_feedback ("button-pressed", FALSE);
  g_signal_emit (self, signals[PB_LONG_PRESS], 0);

  self->long_press_id = 0;
  return G_SOURCE_REMOVE;
}


static void
on_power_button_pressed (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (data);
  static gboolean state_on_press;
  gboolean press = g_variant_get_boolean (param);

  /* We only detect long press when screen is active */
  if (press && self->active == FALSE) {
    if (self->long_press_id) {
      g_warning ("Long press timer already active");
      g_clear_handle_id (&self->long_press_id, g_source_remove);
    }
    self->long_press_id = g_timeout_add_seconds (LONG_PRESS_TIMEOUT,
                                                 on_long_press,
                                                 self);
    g_source_set_name_by_id (self->long_press_id, "[PhoshScreensaverManager] long press");
  }

  /* Press already unblanks since presence status changes due to key press so nothing to do here */
  if (press) {
    state_on_press = self->active;
    return;
  }

  /* If state changed during press, we're done (unblank) */
  if (self->active != state_on_press)
    return;

  /* screen already blanked, no need to do anything on key release */
  if (self->active)
    return;

  /* The long press triggered so don't change screen saver state */
  if (self->long_press_id == 0)
    return;

  g_debug ("Power button released, activating screensaver");
  screen_saver_set_active (self, TRUE, self->lock_enabled);

  /* Disable long press timer */
  g_clear_handle_id (&self->long_press_id, g_source_remove);
}


static void
phosh_screen_saver_manager_set_property (GObject *object,
                                         guint property_id,
                                         const GValue *value,
                                         GParamSpec *pspec)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (object);

  switch (property_id) {
  case PROP_LOCKSCREEN_MANAGER:
    self->lockscreen_manager = g_value_dup_object (value);
    break;
  case PROP_LOCK_ENABLED:
    self->lock_enabled = g_value_get_boolean (value);
    break;
  case PROP_LOCK_DELAY:
    self->lock_delay = g_value_get_int (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_screen_saver_manager_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (object);

  switch (property_id) {
  case PROP_LOCKSCREEN_MANAGER:
    g_value_set_object (value, self->lockscreen_manager);
    break;
  case PROP_LOCK_ENABLED:
    g_value_set_boolean (value, self->lock_enabled);
    break;
  case PROP_LOCK_DELAY:
    g_value_set_int (value, self->lock_delay);
    break;
  case PROP_ACTIVE:
    self->active = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static gboolean
handle_get_active (PhoshDBusScreenSaver  *skeleton,
                   GDBusMethodInvocation *invocation)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (skeleton);

  g_return_val_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self), FALSE);
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self->lockscreen_manager), FALSE);

  g_debug ("DBus call GetActive: %d", self->active);

  phosh_dbus_screen_saver_complete_get_active (skeleton, invocation, self->active);

  return TRUE;
}


static gboolean
handle_get_active_time (PhoshDBusScreenSaver  *skeleton,
                        GDBusMethodInvocation *invocation)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (skeleton);
  guint delta = 0; /* in seconds */
  guint64 active;

  g_return_val_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self), FALSE);
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self->lockscreen_manager), FALSE);

  active = phosh_lockscreen_manager_get_active_time (self->lockscreen_manager);
  if (active)
    delta = (g_get_monotonic_time () - active) / 1000000;

  g_debug ("DBus GetActiveTime: %u", delta);
  phosh_dbus_screen_saver_complete_get_active_time (skeleton, invocation, delta);

  return TRUE;
}

static gboolean
handle_lock (PhoshDBusScreenSaver  *skeleton,
             GDBusMethodInvocation *invocation)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (skeleton);

  g_return_val_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self), FALSE);
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self->lockscreen_manager), FALSE);

  g_debug ("DBus call lock");
  phosh_lockscreen_manager_set_locked (self->lockscreen_manager, TRUE);
  /* TODO: wait a little before blanking */
  screen_saver_set_active (self, TRUE, TRUE);

  phosh_dbus_screen_saver_complete_lock (skeleton, invocation);

  return TRUE;
}


static gboolean
handle_set_active (PhoshDBusScreenSaver  *skeleton,
                   GDBusMethodInvocation *invocation,
                   gboolean               active)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (skeleton);

  g_return_val_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self), FALSE);
  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self->lockscreen_manager), FALSE);

  g_debug ("DBus call SetActive: %d, lock-enabled: %d", active, self->lock_enabled);
  screen_saver_set_active (self, active, self->lock_enabled);

  phosh_dbus_screen_saver_complete_set_active (skeleton, invocation);

  return TRUE;
}


static void
phosh_screen_saver_manager_screen_saver_iface_init (PhoshDBusScreenSaverIface *iface)
{
  iface->handle_get_active = handle_get_active;
  iface->handle_get_active_time = handle_get_active_time;
  iface->handle_lock = handle_lock;
  iface->handle_set_active = handle_set_active;
}

static void
notify_active_changed (PhoshScreenSaverManager *self)
{
  GDBusInterfaceSkeleton *skeleton;

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  skeleton = G_DBUS_INTERFACE_SKELETON (self);

  g_debug ("Signaling ActiveChanged: %d", self->active);
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (skeleton),
                                 NULL,
                                 g_dbus_interface_skeleton_get_object_path (skeleton),
                                 SCREEN_SAVER_DBUS_NAME,
                                 "ActiveChanged",
                                 g_variant_new ("(b)", self->active),
                                 NULL);
  if (self->active) {
    g_debug ("Uninhibited logind suspend handling");
    phosh_clear_fd (&self->inhibit_suspend_fd, NULL);
  }
}

static void
on_inhibit_suspend_finished (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer     user_data)
{
  gboolean success;
  PhoshScreenSaverManager *self;
  PhoshDBusLoginManager *proxy;
  g_autoptr (GError) err = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GVariant) out_pipe_fd = NULL;
  int idx;

  proxy = PHOSH_DBUS_LOGIN_MANAGER (source_object);
  success = phosh_dbus_login_manager_call_inhibit_finish (proxy,
                                                          &out_pipe_fd,
                                                          &fd_list,
                                                          res,
                                                          &err);
  if (!success) {
    phosh_async_error_warn (err, "Failed to inhibit suspend");
    return;
  }

  g_return_if_fail (fd_list && g_unix_fd_list_get_length (fd_list) == 1);

  self = PHOSH_SCREEN_SAVER_MANAGER (user_data);
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  g_variant_get (out_pipe_fd, "h", &idx);
  self->inhibit_suspend_fd = g_unix_fd_list_get (fd_list, idx, &err);
  if (self->inhibit_suspend_fd < 0) {
    g_warning ("Failed to get suspend inhibit fd: %s", err->message);
    return;
  }

  g_debug ("Inhibited logind suspend handling");
}

static void
phosh_screen_saver_manager_inhibit_suspend (PhoshScreenSaverManager *self)
{
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  phosh_dbus_login_manager_call_inhibit (self->logind_manager_proxy,
                                         "sleep",
                                         g_get_user_name (),
                                         "Phosh handling suspend",
                                         "delay",
                                         NULL,
                                         self->cancel,
                                         on_inhibit_suspend_finished,
                                         self);
}

static void
phosh_screen_saver_manager_wakeup_screen (PhoshScreenSaverManager *self)
{
  GDBusInterfaceSkeleton *skeleton;

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  skeleton = G_DBUS_INTERFACE_SKELETON (self);
  g_debug ("Signaling WakeUpScreen");
  g_dbus_connection_emit_signal (g_dbus_interface_skeleton_get_connection (skeleton),
                                 NULL,
                                 g_dbus_interface_skeleton_get_object_path (skeleton),
                                 SCREEN_SAVER_DBUS_NAME,
                                 "WakeUpScreen",
                                 NULL,
                                 NULL);
}


static void
on_wakeup_screen_activated (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (data);

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));
  phosh_screen_saver_manager_wakeup_screen (self);
}


static void
on_lockscreen_manager_wakeup_outputs (PhoshScreenSaverManager *self,
                                      PhoshLockscreenManager *lockscreen_manager)
{
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (lockscreen_manager));

  phosh_screen_saver_manager_wakeup_screen (self);
}


static void
on_locked_hint_set (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusLoginSession *proxy = PHOSH_DBUS_LOGIN_SESSION (source_object);
  g_autoptr (GError) err = NULL;
  gboolean success;

  success = phosh_dbus_login_session_call_set_locked_hint_finish (proxy,
                                                                  res,
                                                                  &err);
  if (!success)
    g_warning ("Failed to send locked hint: %s", err->message);
}


static void
on_lockscreen_manager_locked_changed (PhoshScreenSaverManager *self)
{
  gboolean locked;

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  locked = phosh_lockscreen_manager_get_locked (self->lockscreen_manager);
  if (self->logind_session_proxy) {
    phosh_dbus_login_session_call_set_locked_hint (self->logind_session_proxy,
                                                   locked,
                                                   self->cancel,
                                                   on_locked_hint_set,
                                                   NULL);
  }

  if (locked == TRUE)
    return;

  unarm_lock_delay_timer (self, "unlock");
}


static void
on_logind_lock (PhoshScreenSaverManager *self, PhoshDBusLoginSession *proxy)
{
  g_debug ("Locking request via logind1");
  phosh_lockscreen_manager_set_locked  (self->lockscreen_manager, TRUE);
}


static void
on_logind_unlock (PhoshScreenSaverManager *self, PhoshDBusLoginSession *proxy)
{
  g_debug ("Unlocking request via logind1");
  phosh_lockscreen_manager_set_locked  (self->lockscreen_manager, FALSE);
}


static void
on_logind_prepare_for_sleep (PhoshScreenSaverManager *self,
                             gboolean                 suspending,
                             PhoshDBusLoginManager   *proxy)
{
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  g_debug ("Got PrepareForSleep signal: %s", suspending ? "suspend" : "resume");
  if (suspending) {
    phosh_lockscreen_manager_set_locked (self->lockscreen_manager, TRUE);
  } else {
    PhoshIdleManager *idle_manager;

    phosh_screen_saver_manager_wakeup_screen (self);

    idle_manager = phosh_idle_manager_get_default ();
    phosh_idle_manager_reset_timers (idle_manager);
    phosh_screen_saver_manager_inhibit_suspend (self);
  }
}


static void
on_presence_status_changed (PhoshScreenSaverManager *self, guint32 status, gpointer *data)
{
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  g_debug ("Presence status changed: %d", status);

  if (status == PHOSH_SESSION_PRESENCE_STATUS_IDLE)
    screen_saver_set_active (self, TRUE, self->lock_enabled);
}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (user_data);

  g_debug ("Acquired name %s", name);
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  /* Connect to lockscreen manager once we have a valid connection so we don't
     end up sending out signals early */
  g_object_connect (
    self->lockscreen_manager,
    "swapped-object-signal::wakeup-outputs", G_CALLBACK (on_lockscreen_manager_wakeup_outputs), self,
    "swapped-object-signal::notify::locked", G_CALLBACK (on_lockscreen_manager_locked_changed), self,
    NULL);
}


static void
on_name_lost (GDBusConnection *connection,
              const char      *name,
              gpointer         user_data)
{
  g_debug ("Lost or failed to acquire name %s", name);
}


static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name,
                 gpointer         user_data)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (user_data);

  g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                    connection,
                                    "/org/gnome/ScreenSaver",
                                    NULL);
}


static void
phosh_screen_saver_manager_dispose (GObject *object)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  unarm_lock_delay_timer (self, "dispose");
  g_clear_handle_id (&self->idle_id, g_source_remove);
  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  phosh_clear_fd (&self->inhibit_pwr_btn_fd, NULL);
  phosh_clear_fd (&self->inhibit_suspend_fd, NULL);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  g_clear_object (&self->settings);
  g_clear_object (&self->lockscreen_manager);
  g_clear_object (&self->logind_session_proxy);
  g_clear_object (&self->logind_manager_proxy);
  g_clear_object (&self->primary_monitor);

  g_clear_object (&self->presence);
  g_clear_handle_id (&self->long_press_id, g_source_remove);

  G_OBJECT_CLASS (phosh_screen_saver_manager_parent_class)->dispose (object);
}


static void
on_logind_get_session_proxy_finish  (GObject                 *object,
                                     GAsyncResult            *res,
                                     PhoshScreenSaverManager *self)
{
  g_autoptr (GError) err = NULL;

  self->logind_session_proxy = phosh_dbus_login_session_proxy_new_for_bus_finish (
    res, &err);
  if (!self->logind_session_proxy) {
    phosh_dbus_service_error_warn (err, "Failed to get login1 session proxy");
    return;
  }

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  /* finally register signals */
  g_object_connect (
    self->logind_session_proxy,
    "swapped-object-signal::lock", G_CALLBACK (on_logind_lock), self,
    "swapped-object-signal::unlock", G_CALLBACK (on_logind_unlock), self,
    NULL);
}


static void
on_logind_manager_get_session_finished (PhoshDBusLoginManager   *object,
                                        GAsyncResult            *res,
                                        PhoshScreenSaverManager *self)
{
  g_autofree char *object_path = NULL;
  g_autoptr (GError) err = NULL;

  if (!phosh_dbus_login_manager_call_get_session_finish (
        object, &object_path, res, &err)) {
    phosh_async_error_warn (err, "Failed to get session");
    return;
  }

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  /* Register a proxy for this session */
  phosh_dbus_login_session_proxy_new_for_bus (
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    LOGIN_BUS_NAME,
    object_path,
    self->cancel,
    (GAsyncReadyCallback)on_logind_get_session_proxy_finish,
    self);
}


static void
on_inhibit_pwr_button_finished (GObject      *source_object,
                                GAsyncResult *res,
                                gpointer     user_data)
{
  PhoshScreenSaverManager *self;
  gboolean success;
  PhoshDBusLoginManager *proxy;
  g_autoptr (GError) err = NULL;
  g_autoptr (GUnixFDList) fd_list = NULL;
  g_autoptr (GVariant) out_pipe_fd = NULL;
  int idx;

  proxy = PHOSH_DBUS_LOGIN_MANAGER (source_object);
  success = phosh_dbus_login_manager_call_inhibit_finish (proxy,
                                                          &out_pipe_fd,
                                                          &fd_list,
                                                          res,
                                                          &err);
  if (!success) {
    phosh_async_error_warn (err, "Failed to inhibit power button");
    return;
  }

  g_return_if_fail (fd_list && g_unix_fd_list_get_length (fd_list) == 1);

  self = PHOSH_SCREEN_SAVER_MANAGER (user_data);
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  g_variant_get (out_pipe_fd, "h", &idx);
  self->inhibit_pwr_btn_fd = g_unix_fd_list_get (fd_list, idx, &err);
  if (self->inhibit_pwr_btn_fd < 0) {
    g_warning ("Failed to get power button inhibit fd: %s", err->message);
    return;
  }

  g_debug ("Inhibited logind power button handling");
}


static void
on_logind_manager_proxy_new_for_bus_finish (GObject                 *source_object,
                                            GAsyncResult            *res,
                                            PhoshScreenSaverManager *self)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *session_id = NULL;

  self->logind_manager_proxy =
    phosh_dbus_login_manager_proxy_new_for_bus_finish (res, &err);

  if (!self->logind_manager_proxy) {
    phosh_dbus_service_error_warn (err, "Failed to get login1 manager proxy");
    return;
  }

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  g_signal_connect_swapped (self->logind_manager_proxy,
                            "prepare-for-sleep",
                            G_CALLBACK (on_logind_prepare_for_sleep),
                            self);

  /* If we find a session get it object path */
  if (phosh_find_systemd_session (&session_id)) {
    g_debug ("Logind session %s", session_id);

    phosh_dbus_login_manager_call_get_session (
      self->logind_manager_proxy,
      session_id,
      self->cancel,
      (GAsyncReadyCallback)on_logind_manager_get_session_finished,
      self);
  } else {
    g_debug ("No Login session, screen blank/lock will be unreliable");
  }

  g_debug ("Connected to logind's session interface");

  phosh_dbus_login_manager_call_inhibit (self->logind_manager_proxy,
                                         "handle-power-key",
                                         g_get_user_name (),
                                         "Phosh handling power key",
                                         "block",
                                         NULL,
                                         self->cancel,
                                         on_inhibit_pwr_button_finished,
                                         self);

  phosh_screen_saver_manager_inhibit_suspend (self);
}


static void
on_primary_monitor_power_mode_changed (PhoshScreenSaverManager *self,
                                       GParamSpec *pspec,
                                       PhoshMonitor *monitor)
{
  gboolean active;
  PhoshMonitorPowerSaveMode mode;

  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));
  g_return_if_fail (PHOSH_IS_MONITOR (monitor));

  mode = phosh_monitor_get_power_save_mode (monitor);

  active = (mode == PHOSH_MONITOR_POWER_SAVE_MODE_OFF);
  g_debug ("Screensaver marked as %sactive", active ? "" : "in");
  if (active != self->active) {
    self->active = active;
    g_object_notify_by_pspec(G_OBJECT (self), props[PROP_ACTIVE]);
    notify_active_changed (self);
  }

  if (active) {
    arm_lock_delay_timer (self, active, self->lock_enabled);
  } else {
    unarm_lock_delay_timer (self, "power mode change");
  }
}


static void
on_primary_monitor_changed (PhoshScreenSaverManager *self,
                            GParamSpec *psepc,
                            PhoshShell *shell)
{
  g_return_if_fail (PHOSH_IS_SHELL (shell));
  g_return_if_fail (PHOSH_IS_SCREEN_SAVER_MANAGER (self));

  if (self->primary_monitor)
    g_signal_handlers_disconnect_by_data(self->primary_monitor, self);

  g_set_object (&self->primary_monitor, phosh_shell_get_primary_monitor (shell));

  if (self->primary_monitor == NULL) {
    unarm_lock_delay_timer (self, "primary monitor change");
    return;
  }

  g_signal_connect_object (self->primary_monitor,
                           "notify::power-mode",
                           G_CALLBACK (on_primary_monitor_power_mode_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_primary_monitor_power_mode_changed (self, NULL, self->primary_monitor);
}


static gboolean
on_idle (PhoshScreenSaverManager *self)
{
  /* Connect to logind's session manager */
  phosh_dbus_login_manager_proxy_new_for_bus (
    G_BUS_TYPE_SYSTEM,
    G_DBUS_PROXY_FLAGS_NONE,
    LOGIN_BUS_NAME,
    LOGIN_OBJECT_PATH,
    self->cancel,
    (GAsyncReadyCallback) on_logind_manager_proxy_new_for_bus_finish,
    self);

  g_signal_connect_swapped (phosh_shell_get_default (),
                            "notify::primary-monitor",
                            G_CALLBACK (on_primary_monitor_changed),
                            self);
  on_primary_monitor_changed (self, NULL, phosh_shell_get_default ());

  self->idle_id = 0;
  return G_SOURCE_REMOVE;
}


static void
add_keybindings (PhoshScreenSaverManager *self)
{
  GActionEntry entries[] = {
    { "XF86PowerOff", on_power_button_pressed, "b" },
  };

  /* g-s-manager's grab_single_accelerator makes sure g-s-d doesn't bind it */
  phosh_shell_add_global_keyboard_action_entries (phosh_shell_get_default (),
                                                  (GActionEntry*)entries,
                                                  G_N_ELEMENTS (entries),
                                                  self);
}


static void
phosh_screen_saver_manager_constructed (GObject *object)
{
  PhoshScreenSaverManager *self = PHOSH_SCREEN_SAVER_MANAGER (object);

  G_OBJECT_CLASS (phosh_screen_saver_manager_parent_class)->constructed (object);
  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       SCREEN_SAVER_DBUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN_MANAGER (self->lockscreen_manager));

  self->settings = g_settings_new ("org.gnome.desktop.screensaver");
  g_settings_bind (self->settings, "lock-enabled", self, "lock-enabled", G_SETTINGS_BIND_GET);
  g_settings_bind (self->settings, "lock-delay", self, "lock-delay", G_SETTINGS_BIND_GET);

  add_keybindings (self);

  self->presence = phosh_session_presence_get_default_failable ();
  if (self->presence) {
    g_signal_connect_swapped (self->presence,
                              "status-changed",
                              (GCallback) on_presence_status_changed,
                              self);
  }

  /* Perform login1 setup when idle */
  self->idle_id = g_idle_add ((GSourceFunc)on_idle, self);
  g_source_set_name_by_id (self->idle_id, "[PhoshScreenSaverManager] idle");
}


static void
phosh_screen_saver_manager_class_init (PhoshScreenSaverManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_screen_saver_manager_constructed;
  object_class->dispose = phosh_screen_saver_manager_dispose;
  object_class->set_property = phosh_screen_saver_manager_set_property;
  object_class->get_property = phosh_screen_saver_manager_get_property;

  props[PROP_LOCKSCREEN_MANAGER] =
      g_param_spec_object ("lockscreen-manager",
                           "LockscreenManager",
                           "The lockscreen manager",
                           PHOSH_TYPE_LOCKSCREEN_MANAGER,
                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  /**
   * PhoshScreenSaverManager:lock-enabled:
   *
   * Whether activating the screens saver should also lock the screen
   */
  props[PROP_LOCK_ENABLED] =
    g_param_spec_boolean ("lock-enabled", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshScreenSaverManager:lock-delay:
   *
   * Delay in seconds for screen lock after blank
   */
  props[PROP_LOCK_DELAY] =
    g_param_spec_int ("lock-delay", "", "",
                      0,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshScreenSaverManager:active
   *
   * Whether the screen saver is active
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshScreenSaverManager:pb-long-press
   *
   * Power button long press detected
   */
  signals[PB_LONG_PRESS] =
    g_signal_new ("pb-long-press",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                  NULL, G_TYPE_NONE, 0);
}


static void
phosh_screen_saver_manager_init (PhoshScreenSaverManager *self)
{
  const GActionEntry entries[] = {
    { .name = "screensaver.wakeup-screen", .activate = on_wakeup_screen_activated },
  };

  self->cancel = g_cancellable_new ();
  self->inhibit_suspend_fd = -1;
  self->inhibit_pwr_btn_fd = -1;

  g_action_map_add_action_entries (G_ACTION_MAP (phosh_shell_get_default ()),
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
}


PhoshScreenSaverManager *
phosh_screen_saver_manager_new (PhoshLockscreenManager *lockscreen_manager)
{
  return g_object_new (PHOSH_TYPE_SCREEN_SAVER_MANAGER,
                       "lockscreen-manager", lockscreen_manager,
                       NULL);
}
