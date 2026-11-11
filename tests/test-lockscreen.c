/*
 * Copyright (C) 2024 The Phosh Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "testlib-wait-for-shell-state.h"

#include "testlib-full-shell.h"

#define POP_TIMEOUT 50000000
#define WAIT_TIMEOUT 30000

typedef struct _Fixture {
  PhoshTestFullShellFixture       base;
  struct zwp_virtual_keyboard_v1 *keyboard;
  GTimer                         *timer;
  GtkWidget                      *extra_page;
} Fixture;


static void
fixture_setup (Fixture *fixture, gconstpointer cfg)
{
  phosh_test_full_shell_setup (&fixture->base, cfg);
  fixture->timer = g_timer_new ();
}


static void
fixture_teardown (Fixture *fixture, gconstpointer unused)
{
  phosh_test_full_shell_teardown (&fixture->base, NULL);
  g_clear_pointer (&fixture->timer, g_timer_destroy);
}


static void
on_waited (gpointer data)
{
  GMainLoop *loop = data;

  g_assert_nonnull (data);
  g_main_loop_quit (loop);
}


static void
wait_a_bit (GMainLoop *loop, int msecs)
{
  gint id;

  id = g_timeout_add_once (msecs, on_waited, loop);
  g_source_set_name_by_id (id, "[TestLockscreen] wait");
  g_main_loop_run (loop);
}


static void
lock_shell (gpointer data)
{
  Fixture *fixture = (Fixture*) data;
  phosh_shell_set_locked (phosh_shell_get_default (), TRUE);
  g_async_queue_push (fixture->base.queue, (gpointer) TRUE);
}


static void
add_lockscreen_extra_page (gpointer data)
{
  Fixture *fixture = (Fixture*) data;
  PhoshLockscreen *lockscreen = NULL;
  PhoshShell *shell = NULL;

  fixture->extra_page = gtk_label_new ("Hello, world.");

  shell = phosh_shell_get_default ();
  lockscreen = phosh_lockscreen_manager_get_lockscreen (phosh_shell_get_lockscreen_manager (shell));

  phosh_lockscreen_add_extra_page (lockscreen, fixture->extra_page);
  phosh_lockscreen_set_default_page (lockscreen, PHOSH_LOCKSCREEN_PAGE_EXTRA);

  g_async_queue_push (fixture->base.queue, (gpointer) TRUE);
}


static void
test_phosh_lockscreen_extra_page (Fixture *fixture, gconstpointer unused)
{
  g_autoptr (PhoshTestWaitForShellState) waiter = NULL;
  PhoshShell *shell = NULL;
  PhoshLockscreen *lockscreen = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (GMainContext) context = g_main_context_new ();

  loop = g_main_loop_new (context, FALSE);

  /* Wait until comp/shell are up */
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->base.queue, POP_TIMEOUT));

  fixture->keyboard = phosh_test_keyboard_new (phosh_wayland_get_default ());

  shell = phosh_shell_get_default ();
  waiter = phosh_test_wait_for_shell_state_new (shell);

  /* lock the shell from its main thread */
  g_idle_add_once (lock_shell, fixture);
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->base.queue, POP_TIMEOUT));

  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_LOCKED, TRUE, WAIT_TIMEOUT);

  lockscreen = phosh_lockscreen_manager_get_lockscreen (phosh_shell_get_lockscreen_manager (shell));

  /* insert widget into extra page, from the main Phosh shell thread */
  g_idle_add_once (add_lockscreen_extra_page, fixture);

  /* wait for the extra page insertion to complete */
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->base.queue, POP_TIMEOUT));
  /* this seems to be necessary, though I don't know why and wish it were not so. */
  wait_a_bit (loop, 1000);

  /* send lockscreen back to default page */
  phosh_test_keyboard_press_keys (fixture->keyboard,
                                  fixture->timer,
                                  KEY_ESC, NULL);

  /* wait for lockscreen carousel animation */
  wait_a_bit (loop, 2000);

  g_assert_true (phosh_lockscreen_get_page (lockscreen) == PHOSH_LOCKSCREEN_PAGE_EXTRA);
}


int
main (int argc, char *argv[])
{
  g_autoptr (PhoshTestFullShellFixtureCfg) cfg = NULL;

  g_test_init (&argc, &argv, NULL);

  cfg = phosh_test_full_shell_fixture_cfg_new ("all");

  g_test_add ("/phosh/lockscreen/extra_page",
              Fixture,
              cfg,
              fixture_setup,
              test_phosh_lockscreen_extra_page,
              fixture_teardown);
  return g_test_run ();
}
