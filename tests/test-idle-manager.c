/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-idle-dbus.h"
#include "shell-priv.h"

#include "testlib-full-shell.h"

#define BUS_NAME "org.gnome.Mutter.IdleMonitor"
#define PATH "/org/gnome/Mutter/IdleMonitor"
#define OBJECT_PATH "/org/gnome/Mutter/IdleMonitor/Core"

#define POP_TIMEOUT 50000000

static guint watch_id;
static GMainLoop *loop;
int fire = 1000; /* fire after 1 second */


static void
watch_fired_cb (PhoshIdleDBusIdleMonitor *proxy,
                guint                     id,
                gpointer                 *data)
{
  *(gboolean*)data = TRUE;
  g_assert_nonnull (loop);
  g_assert_cmpint (watch_id, ==, id);
  g_main_loop_quit (loop);
}


static gboolean
timeout_cb  (gpointer data)
{
  g_error ("Watch did not fire");
  return FALSE;
}


static void
test_phosh_idle_watch_fired (PhoshTestFullShellFixture *fixture, gconstpointer unused)
{
  int timeout_id;
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshIdleDBusIdleMonitor) proxy = NULL;
  g_autoptr (PhoshIdleDBusObjectManagerClient) client = NULL;
  g_autoptr (GDBusObject) object = NULL;
  gboolean fired = FALSE;

  /* Wait until comp/shell are up */
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->queue, POP_TIMEOUT));

  client = PHOSH_IDLE_DBUS_OBJECT_MANAGER_CLIENT (
    phosh_idle_dbus_object_manager_client_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
      BUS_NAME,
      PATH,
      NULL,
      &err));
  g_assert_no_error (err);
  g_assert_true (PHOSH_IDLE_DBUS_IS_OBJECT_MANAGER_CLIENT (client));

  object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (client),
                                             OBJECT_PATH);
  g_assert_true (PHOSH_IDLE_DBUS_IS_OBJECT (object));

  proxy = phosh_idle_dbus_object_get_idle_monitor (PHOSH_IDLE_DBUS_OBJECT (object));
  g_assert_true (G_IS_DBUS_PROXY (proxy));

  /* Connect the watch */
  g_signal_connect (proxy, "watch-fired", G_CALLBACK (watch_fired_cb), &fired);
  g_assert_true (phosh_idle_dbus_idle_monitor_call_add_idle_watch_sync (
                   proxy, fire, &watch_id, NULL, NULL));
  g_assert_true (watch_id);

  /* Aborts in case watch doesn't fire */
  timeout_id = g_timeout_add_seconds (fire * 2 / 1000, timeout_cb, NULL);

  /* Run main loop */
  loop = g_main_loop_new (NULL, TRUE);
  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  /* Remove watch that fired */
  g_assert_true (fired);
  g_source_remove (timeout_id);
  g_assert_true (phosh_idle_dbus_idle_monitor_call_remove_watch_sync (
                   proxy, watch_id, NULL, NULL));

}


static void
test_phosh_idle_watch_unfired (PhoshTestFullShellFixture *fixture, gconstpointer unused)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshIdleDBusIdleMonitor) proxy = NULL;
  g_autoptr (PhoshIdleDBusObjectManagerClient) client = NULL;
  g_autoptr (GDBusObject) object = NULL;
  gboolean fired = FALSE;

  /* Wait until comp/shell are up */
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->queue, POP_TIMEOUT));

  client = PHOSH_IDLE_DBUS_OBJECT_MANAGER_CLIENT (
    phosh_idle_dbus_object_manager_client_new_for_bus_sync (
      G_BUS_TYPE_SESSION,
      G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
      BUS_NAME,
      PATH,
      NULL,
      &err));
  g_assert_no_error (err);
  g_assert_true (PHOSH_IDLE_DBUS_IS_OBJECT_MANAGER_CLIENT (client));

  object = g_dbus_object_manager_get_object (G_DBUS_OBJECT_MANAGER (client),
                                             OBJECT_PATH);
  g_assert_true (PHOSH_IDLE_DBUS_IS_OBJECT (object));

  proxy = phosh_idle_dbus_object_get_idle_monitor (PHOSH_IDLE_DBUS_OBJECT (object));
  g_assert_true (G_IS_DBUS_PROXY (proxy));

  /* Connect the watch */
  g_assert_true (phosh_idle_dbus_idle_monitor_call_add_idle_watch_sync (
                   proxy, fire, &watch_id, NULL, NULL));
  g_assert_true (watch_id);

  /* Remove watch that did not yet fire */
  g_assert_true (phosh_idle_dbus_idle_monitor_call_remove_watch_sync (
                   proxy, watch_id, NULL, NULL));
  g_assert_false (fired);
}


int
main (int   argc, char *argv[])
{
  g_autoptr (PhoshTestFullShellFixtureCfg) cfg = NULL;

  g_test_init (&argc, &argv, NULL);

  cfg = phosh_test_full_shell_fixture_cfg_new ("all");

  PHOSH_FULL_SHELL_TEST_ADD ("/phosh/dbus/idle-manager/fired", cfg, test_phosh_idle_watch_fired);
  PHOSH_FULL_SHELL_TEST_ADD ("/phosh/dbus/idle-manager/unfired", cfg, test_phosh_idle_watch_unfired);

  return g_test_run ();
}
