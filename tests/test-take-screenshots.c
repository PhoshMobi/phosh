/*
 * Copyright (C) 2021 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-config.h"
#include "phosh-gnome-shell-dbus.h"
#include "phosh-screenshot-dbus.h"
#include "phosh-test-resources.h"
#include "portal-dbus.h"
#include "shell-priv.h"

#include "gnome-shell-manager.h"

#include "testlib-full-shell.h"
#include "testlib-calls-mock.h"
#include "testlib-mpris-mock.h"
#include "testlib-emergency-calls.h"
#include "testlib-wait-for-shell-state.h"

#include "phosh-screen-saver-dbus.h"

#define BUS_NAME "org.gnome.Shell.Screenshot"
#define OBJECT_PATH "/org/gnome/Shell/Screenshot"

#define POP_TIMEOUT 50000000
#define WAIT_TIMEOUT 30000


typedef struct _PhoshScreenShotContext {
  GTimer    *timer;
  struct zwp_virtual_keyboard_v1 *keyboard;
  PhoshTestWaitForShellState *waiter;
  GMainLoop *loop;
} PhoshScreenShotContext;


uint num_toplevels;


static void
take_screenshot (const char *what, int num, const char *where)
{
  g_autoptr (GError) err = NULL;
  PhoshDBusScreenshot *proxy = NULL;
  g_autofree char *used_name = NULL;
  g_autofree char *dirname = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *path = NULL;
  gboolean success;

  /* libcall-ui has no idea that we're picking the translations from odd places
   * so help it along */
  bindtextdomain ("call-ui", LOCALEDIR);

  dirname = g_build_filename (TEST_OUTPUT_DIR, "screenshots", what, NULL);
  filename = g_strdup_printf ("screenshot-%.2d-%s.png", num, where);
  path = g_build_filename (dirname, filename, NULL);
  g_assert_cmpint (g_mkdir_with_parents (dirname, 0755), ==, 0);
  if (g_file_test (path, G_FILE_TEST_EXISTS))
    g_assert_false (unlink (path));

  proxy = phosh_dbus_screenshot_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                        BUS_NAME,
                                                        OBJECT_PATH,
                                                        NULL,
                                                        &err);
  g_assert_no_error (err);
  g_assert_true (PHOSH_DBUS_IS_SCREENSHOT_PROXY (proxy));

  success = phosh_dbus_screenshot_call_screenshot_sync (proxy,
                                                        TRUE,
                                                        TRUE,
                                                        path,
                                                        &success,
                                                        &used_name,
                                                        NULL,
                                                        &err);

  g_assert_no_error (err);
  g_assert_true (success);
  g_assert_cmpstr (used_name, ==, path);
  g_test_message ("Screenshot at %s", used_name);
  g_assert_true (g_file_test (used_name, G_FILE_TEST_EXISTS));
  g_assert_finalize_object (proxy);
}


static gboolean
on_waited (gpointer data)
{
  GMainLoop *loop = data;

  g_assert_nonnull (data);
  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}


static void
wait_a_bit (GMainLoop *loop, int msecs)
{
  g_autoptr (GSource) source = g_timeout_source_new (msecs);

  g_source_set_name (source, "[TestTakeScreenshot] wait");
  g_source_set_callback (source, on_waited, loop, NULL);

  g_source_attach (source, g_main_loop_get_context (loop));
  g_main_loop_run (loop);
}


static void
toggle_overview (PhoshScreenShotContext *ctx)
{
  phosh_test_keyboard_press_modifiers (ctx->keyboard, KEY_LEFTMETA);
  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_A, NULL);
  phosh_test_keyboard_release_modifiers (ctx->keyboard);
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_OVERVIEW, TRUE, WAIT_TIMEOUT);
  /* Give animation time to finish */
  wait_a_bit (ctx->loop, 500);
}


static void
toggle_settings (PhoshScreenShotContext *ctx, gboolean should_be_visible)
{
  phosh_test_keyboard_press_modifiers (ctx->keyboard, KEY_LEFTMETA);
  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_M, NULL);
  phosh_test_keyboard_release_modifiers (ctx->keyboard);
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_SETTINGS, should_be_visible,
                                        WAIT_TIMEOUT);
  /* Give animation time to finish */
  wait_a_bit (ctx->loop, 500);
}


static void
activate_lockscreen_plugins (PhoshScreenShotContext *ctx, gboolean activate)

{
  guint key = activate ? KEY_LEFT : KEY_RIGHT;

  phosh_test_keyboard_press_modifiers (ctx->keyboard, KEY_LEFTCTRL);
  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, key, NULL);
  phosh_test_keyboard_release_modifiers (ctx->keyboard);
  /* Give animation time to finish */
  wait_a_bit (ctx->loop, 500);
}


static void
show_run_command_dialog (PhoshScreenShotContext *ctx, gboolean show)
{
  if (show) {
    phosh_test_keyboard_press_modifiers (ctx->keyboard, KEY_LEFTALT);
    phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_F2, NULL);
    phosh_test_keyboard_release_modifiers (ctx->keyboard);
  } else {
    phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_ESC, NULL);
  }
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_MODAL_SYSTEM_PROMPT, show,
                                        WAIT_TIMEOUT);
  /* Give animation time to finish */
  wait_a_bit (ctx->loop, 500);
  /* Even more time for powerbar fade animation to finish */
  wait_a_bit (ctx->loop, 500);
}


static void
do_settings (void)
{
  g_autoptr (GSettings) settings = g_settings_new ("org.gnome.desktop.interface");

  g_settings_set_boolean (settings, "clock-show-date", FALSE);
  g_settings_set_boolean (settings, "clock-show-weekday", FALSE);

  /* Enable emergency-calls until it's on by default */
  g_clear_object (&settings);
  settings = g_settings_new ("sm.puri.phosh.emergency-calls");
  g_settings_set_boolean (settings, "enabled", TRUE);

  /* Enable emergency-calls until it's on by default */
  g_clear_object (&settings);
  settings = g_settings_new ("sm.puri.phosh.plugins");
  g_settings_set_strv (settings, "lock-screen",
                       (const char *const[]) { "emergency-info", "launcher-box", NULL });

  /* Enable quick setting plugins */
  g_settings_set_strv (settings, "quick-settings",
                       (const char *const[]) { "caffeine-quick-setting",
                                               "simple-custom-quick-setting",
                                               NULL });

  g_clear_object (&settings);
  settings = g_settings_new ("mobi.phosh.shell.plugins");
  /* Enable status-icon plugins */
  g_settings_set_strv (settings, "status-icons",
                       (const char *const[]) { "simple-custom-status-icon",
                                               NULL });
}


static GVariant *
get_portal_access_options (const char *icon)
{
  GVariantDict dict;

  g_variant_dict_init (&dict, NULL);
  g_variant_dict_insert_value (&dict, "icon", g_variant_new_string(icon));
  return g_variant_dict_end (&dict);
}


static void
on_portal_access_dialog (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  guint out;

  PhoshDBusImplPortalAccess *proxy = PHOSH_DBUS_IMPL_PORTAL_ACCESS (source_object);
  success = phosh_dbus_impl_portal_access_call_access_dialog_finish (proxy, &out, NULL, res, &err);
  g_assert_true (success);
  g_assert_no_error (err);
  *(gboolean*)user_data = success;
}


static void
on_osd_finish (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;

  PhoshDBusGnomeShell *proxy = PHOSH_DBUS_GNOME_SHELL (source_object);
  success = phosh_dbus_gnome_shell_call_show_osd_finish (proxy, res, &err);
  g_assert_true (success);
  g_assert_no_error (err);
}


static GPid
run_plugin_prefs (const char *arg)
{
  g_autoptr (GError) err = NULL;
  const char *argv[] = { TEST_TOOLS "/plugin-prefs", arg, NULL };
  gboolean ret;
  GPid pid;

  ret = g_spawn_async (NULL, (char**) argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &pid, &err);
  g_assert_no_error (err);
  g_assert_true (ret);
  g_assert_true (pid);

  return pid;
}


static gboolean
wait_for_num_toplevels (GMainLoop  *loop,
                        uint        n,
                        uint        timeout,
                        GSourceFunc callback,
                        gpointer    userdata)
{
  gboolean found = FALSE;
  uint sleep = 100;

  while (timeout > 0) {
    if (callback)
      (callback) (userdata);

    if (num_toplevels == n) {
      found = TRUE;
      break;
    }
    wait_a_bit (loop, sleep);
    timeout -= sleep;
  }

  return found;
}


typedef struct {
  struct zwp_virtual_keyboard_v1 *keyboard;
  uint modifiers;
  uint key;
} PhoshTestKbdShortcut;


static gboolean
send_kbd_shortcut_cb (gpointer user_data)
{
  PhoshTestKbdShortcut *shortcut = user_data;
  g_autoptr (GTimer) timer = g_timer_new ();

  if (shortcut->modifiers)
    phosh_test_keyboard_press_modifiers (shortcut->keyboard, shortcut->modifiers);

  phosh_test_keyboard_press_keys (shortcut->keyboard, timer, shortcut->key, NULL);

  if (shortcut->modifiers)
    phosh_test_keyboard_release_modifiers (shortcut->keyboard);

  return TRUE;
}


static void
screenshot_plugin_pref (PhoshScreenShotContext *ctx,
                        const char             *what,
                        const char             *where,
                        int                     num,
                        guint                   key)
{
  g_test_message ("Screenshotting '%s'", what);
  g_debug ("Waiting for prefs app…");
  g_assert (wait_for_num_toplevels (ctx->loop, 1, 5000, NULL, NULL));

  g_debug ("Opening prefs…");
  g_assert (wait_for_num_toplevels (ctx->loop, 2, 5000, send_kbd_shortcut_cb,
                                    &(PhoshTestKbdShortcut){ ctx->keyboard, KEY_LEFTCTRL, key }));
  g_debug ("Closing prefs…");
  take_screenshot (what, num++, where);

  g_assert (wait_for_num_toplevels (ctx->loop, 1, 5000, send_kbd_shortcut_cb,
                                    &(PhoshTestKbdShortcut){ ctx->keyboard, 0, KEY_ESC }));
}


static int
screenshot_lockscreen_plugin_prefs (PhoshScreenShotContext     *ctx,
                                    const char                 *what,
                                    int                         num,
                                    PhoshTestWaitForShellState *waiter)
{
  GPid pid;

  pid = run_plugin_prefs ("-l");
  /* Wait for overview to close */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_OVERVIEW, FALSE, WAIT_TIMEOUT);

  screenshot_plugin_pref (ctx, what, "plugin-prefs-ticket-box", num++, KEY_T);
  screenshot_plugin_pref (ctx, what, "plugin-prefs-emergency-info", num++, KEY_E);
  screenshot_plugin_pref (ctx, what, "plugin-prefs-upcoming-events", num++, KEY_U);

  g_assert_no_errno (kill (pid, SIGTERM));
  g_spawn_close_pid (pid);

  /* wait for app to quit and overview to be visible again */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_OVERVIEW, TRUE, WAIT_TIMEOUT);

  return num;
}


static int
screenshot_quick_setting_plugin_prefs (PhoshScreenShotContext     *ctx,
                                       const char                 *what,
                                       int                         num,
                                       PhoshTestWaitForShellState *waiter)
{
  GPid pid;

  pid = run_plugin_prefs ("-q");
  /* Wait for overview to close */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_OVERVIEW, FALSE, WAIT_TIMEOUT);

  screenshot_plugin_pref (ctx, what, "plugin-prefs-caffeine", num++, KEY_C);
  screenshot_plugin_pref (ctx, what, "plugin-prefs-pomodoro", num++, KEY_P);

  g_assert_no_errno (kill (pid, SIGTERM));
  g_spawn_close_pid (pid);

  /* wait for app to quit and overview to be visible again */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_OVERVIEW, TRUE, WAIT_TIMEOUT);

  return num;
}


static void
on_end_session_dialog_open_finish (GObject      *source_object,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;

  g_assert (PHOSH_DBUS_IS_END_SESSION_DIALOG (source_object));

  success = phosh_dbus_end_session_dialog_call_open_finish (
    PHOSH_DBUS_END_SESSION_DIALOG (source_object),
    res,
    &err);

  g_assert_no_error (err);
  g_assert_true (success);
}


static int
screenshot_end_session_dialog (PhoshScreenShotContext *ctx, const char *what, int num)
{
  g_autoptr (PhoshDBusEndSessionDialog) proxy = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (GPtrArray) inhibitors = g_ptr_array_new_with_free_func (g_free);

  proxy = phosh_dbus_end_session_dialog_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    "org.gnome.Shell",
    "/org/gnome/SessionManager/EndSessionDialog",
    NULL,
    &err);
  g_assert_no_error (err);

  for (int i = 0; i < 10; i++) {
    char *sym = g_strdup_printf ("/org/example/foo%d", i);
    g_ptr_array_add (inhibitors, sym);
  }
  g_ptr_array_add (inhibitors, NULL);

  phosh_dbus_end_session_dialog_call_open (proxy,
                                           0,
                                           0,
                                           30,
                                           (const char * const*)inhibitors->pdata,
                                           NULL,
                                           on_end_session_dialog_open_finish,
                                           NULL);
  wait_a_bit (ctx->loop, 500);
  take_screenshot (what, num++, "end-session-dialog");

  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_ESC, NULL);
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_MODAL_SYSTEM_PROMPT, FALSE,
                                        WAIT_TIMEOUT);
  wait_a_bit (ctx->loop, 500);

  return num;
}


static int
screenshot_osd_and_check_keybinding (PhoshScreenShotContext *ctx, const char *what, int num)
{
  g_autoptr (PhoshDBusGnomeShell) proxy = NULL;
  g_autoptr (GError) err = NULL;
  GVariantBuilder builder;
  gboolean success;
  uint action;

  proxy = phosh_dbus_gnome_shell_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                         G_DBUS_PROXY_FLAGS_NONE,
                                                         "org.gnome.Shell",
                                                         "/org/gnome/Shell",
                                                         NULL,
                                                         &err);
  g_assert_no_error (err);
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_add (&builder, "{sv}", "connector",
                         g_variant_new_string ("DSI-1"));
  g_variant_builder_add (&builder, "{sv}", "label",
                         g_variant_new_string ("HDMI / DisplayPort"));
  g_variant_builder_add (&builder, "{sv}", "icon",
                         g_variant_new_string ("audio-volume-medium-symbolic"));
  g_variant_builder_add (&builder, "{sv}", "level",
                         g_variant_new_double (0.5));
  phosh_dbus_gnome_shell_call_show_osd (proxy,
                                        g_variant_builder_end (&builder),
                                        NULL,
                                        on_osd_finish,
                                        NULL);
  g_assert_no_error (err);
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_MODAL_SYSTEM_PROMPT, TRUE,
                                        WAIT_TIMEOUT);
  take_screenshot (what, num++, "osd");

  /* wait for OSD to clear */
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_MODAL_SYSTEM_PROMPT, FALSE,
                                        WAIT_TIMEOUT);
  wait_a_bit (ctx->loop, 500);

  /* Check keybinding registration as we have the proxy */
  success =
    phosh_dbus_gnome_shell_call_grab_accelerator_sync (proxy,
                                                       "XF86AudioMute",
                                                       PHOSH_SHELL_ACTION_MODE_ALL,
                                                       PHOSH_SHELL_KEY_BINDING_IGNORE_AUTOREPEAT,
                                                       &action,
                                                       NULL,
                                                       &err);
  g_assert_no_error (err);
  g_assert_true (success);
  g_assert_cmpint (action, >, 0);

  success = phosh_dbus_gnome_shell_call_ungrab_accelerator_sync (proxy,
                                                                 action,
                                                                 NULL,
                                                                 NULL,
                                                                 &err);
  g_assert_no_error (err);
  g_assert_true (success);

  return num;
}


static int
screenshot_portal_access (PhoshScreenShotContext *ctx, const char *what, int num)
{
  g_autoptr (PhoshDBusImplPortalAccess) proxy = NULL;
  g_autoptr (GVariant) options = NULL;
  g_autoptr (GError) err = NULL;
  gboolean success = FALSE;

  proxy = phosh_dbus_impl_portal_access_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                G_DBUS_PROXY_FLAGS_NONE,
                                                                "mobi.phosh.Shell.Portal",
                                                                "/org/freedesktop/portal/desktop",
                                                                NULL,
                                                                &err);
  g_assert_no_error (err);
  options = get_portal_access_options ("audio-input-microphone-symbolic");
  phosh_dbus_impl_portal_access_call_access_dialog (
    proxy,
    "/mobi/phosh/Shell/Access",
    "mobi.phosh.Shell",
    "",
    "Give FooBar Microphone and Storage Access?",
    "FooBar wants to use your microphone and storage.",
    "Access can be changed at any time from the privacy settings.",
    g_variant_ref_sink (options),
    NULL,
    on_portal_access_dialog,
    &success);
  wait_a_bit (ctx->loop, 500);
  take_screenshot (what, num++, "portal-access");
  /* Close dialog */
  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_ENTER, NULL);
  phosh_test_wait_for_shell_state_wait (ctx->waiter, PHOSH_STATE_MODAL_SYSTEM_PROMPT, FALSE,
                                        WAIT_TIMEOUT);
  wait_a_bit (ctx->loop, 500);
  g_assert_true (success);

  return num;
}


static void
on_ask_question_finish (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  g_autoptr (GVariant) details = NULL;
  guint response;

  g_assert (PHOSH_DBUS_IS_MOUNT_OPERATION_HANDLER (source_object));

  success = phosh_dbus_mount_operation_handler_call_ask_question_finish (
    PHOSH_DBUS_MOUNT_OPERATION_HANDLER (source_object),
    &response,
    &details,
    res,
    &err);

  g_assert_no_error (err);
  g_assert_true (success);
  g_assert_cmpint (response, ==, 0);
}



static int
screenshot_mount_prompt (PhoshScreenShotContext *ctx, const char *what, int num)
{
  g_autoptr (PhoshDBusMountOperationHandler) proxy = NULL;
  g_autoptr (GError) err = NULL;
  const char *choices[] = { "Yes", "Maybe", NULL };

  proxy = phosh_dbus_mount_operation_handler_proxy_new_for_bus_sync (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_NONE,
    "org.gtk.MountOperationHandler",
    "/org/gtk/MountOperationHandler",
    NULL,
    &err);
  g_assert_no_error (err);

  phosh_dbus_mount_operation_handler_call_ask_question (
    proxy,
    "OpId0q",
    "What do you want to do?\nThere's so many questions.",
    "drive-harddisk",
    choices,
    NULL,
    on_ask_question_finish,
    NULL);

  wait_a_bit (ctx->loop, 500);
  take_screenshot (what, num++, "mount-prompt");

  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_ENTER, NULL);
  wait_a_bit (ctx->loop, 500);

  return num;
}

static int
screenshot_emergency_calls (PhoshScreenShotContext *ctx, const char *locale, int num)
{
  g_autoptr (PhoshTestEmergencyCallsMock) emergency_calls_mock = NULL;

  emergency_calls_mock = phosh_test_emergency_calls_mock_new ();
  phosh_test_emergency_calls_mock_export (emergency_calls_mock);

  phosh_test_keyboard_press_timeout (ctx->keyboard, ctx->timer, KEY_POWER, 3000);
  wait_a_bit (ctx->loop, 500);
  take_screenshot (locale, num++, "power-menu");

  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_TAB, KEY_TAB, KEY_ENTER, NULL);
  phosh_test_keyboard_release_modifiers (ctx->keyboard);
  wait_a_bit (ctx->loop, 500);
  take_screenshot (locale, num++, "emergency-dialpad");

  phosh_test_keyboard_press_modifiers (ctx->keyboard, KEY_LEFTALT);
  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_C, NULL);
  phosh_test_keyboard_release_modifiers (ctx->keyboard);
  wait_a_bit (ctx->loop, 1000);
  take_screenshot (locale, num++, "emergency-contacts");

  phosh_test_keyboard_press_keys (ctx->keyboard, ctx->timer, KEY_ESC, NULL);
  wait_a_bit (ctx->loop, 500);

  return num;
}


static void
on_num_toplevels_changed (PhoshToplevelManager *toplevel_manager)
{
  num_toplevels = phosh_toplevel_manager_get_num_toplevels (toplevel_manager);
  g_debug ("Num toplevels: %d", num_toplevels);
}


static void
test_take_screenshots (PhoshTestFullShellFixture *fixture, gconstpointer unused)
{
  const char *what = g_getenv ("PHOSH_TEST_TYPE");
  g_autoptr (GTimer) timer = g_timer_new ();
  g_autoptr (GMainContext) context = g_main_context_new ();
  g_autoptr (GMainLoop) loop = NULL;
  g_autoptr (PhoshDBusDisplayConfig) dc_proxy = NULL;
  g_autoptr (PhoshDBusScreenSaver) ss_proxy = NULL;
  g_autoptr (PhoshTestCallsMock) calls_mock = NULL;
  g_autoptr (PhoshTestMprisMock) mpris_mock = NULL;
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshTestWaitForShellState) waiter = NULL;
  PhoshScreenShotContext ctx = { .timer = timer };
  PhoshShell *shell;
  PhoshToplevelManager *toplevel_manager;
  const char *argv[] = { TEST_TOOLS "/app-buttons", NULL };
  GPid pid;
  int i = 1;

  g_assert_nonnull (what);
  /* Wait until compositor and shell are up */
  g_assert_nonnull (g_async_queue_timeout_pop (fixture->queue, POP_TIMEOUT));

  ctx.loop = g_main_loop_new (context, FALSE);
  shell = phosh_shell_get_default ();
  waiter = phosh_test_wait_for_shell_state_new (shell);
  ctx.waiter = waiter;

  toplevel_manager = phosh_shell_get_toplevel_manager (shell);
  g_signal_connect (toplevel_manager,
                    "notify::num-toplevels",
                    G_CALLBACK (on_num_toplevels_changed),
                    &num_toplevels);

  /* Give overview animation time to finish */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_SETTINGS, FALSE, WAIT_TIMEOUT);
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "overview-empty");

  ctx.keyboard = phosh_test_keyboard_new (phosh_wayland_get_default ());

  /* Give overview animation some time to finish */
  wait_a_bit (ctx.loop, 500);
  /* Typing will focus search */
  phosh_test_keyboard_press_keys (ctx.keyboard, ctx.timer, KEY_M, KEY_E, KEY_D, NULL);
  /* Give search time to finish */
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "search");

  g_spawn_async (NULL, (char**) argv, NULL, G_SPAWN_DEFAULT, NULL, NULL, &pid, &err);
  g_assert_no_error (err);
  g_assert_true (pid);
  /* Wait for overview to close */
  phosh_test_wait_for_shell_state_wait (waiter, PHOSH_STATE_OVERVIEW, FALSE, WAIT_TIMEOUT);
  /* Give app time to start */
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "running-app");

  toggle_overview (&ctx);
  take_screenshot (what, i++, "overview-app");
  kill (pid, SIGTERM);
  g_spawn_close_pid (pid);

  i = screenshot_lockscreen_plugin_prefs (&ctx, what, i, waiter);
  i = screenshot_quick_setting_plugin_prefs (&ctx, what, i, waiter);

  show_run_command_dialog (&ctx, TRUE);
  take_screenshot (what, i++, "run-command");
  show_run_command_dialog (&ctx, FALSE);

  toggle_settings (&ctx, TRUE);
  take_screenshot (what, i++, "settings");
  toggle_settings (&ctx, FALSE);

  i = screenshot_osd_and_check_keybinding (&ctx, what, i);
  i = screenshot_portal_access (&ctx, what, i);
  i = screenshot_end_session_dialog (&ctx, what, i);
  i = screenshot_mount_prompt (&ctx, what, i);

  ss_proxy = phosh_dbus_screen_saver_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                             G_DBUS_PROXY_FLAGS_NONE,
                                                             "org.gnome.ScreenSaver",
                                                             "/org/gnome/ScreenSaver",
                                                             NULL,
                                                             &err);
  g_assert_no_error (err);
  phosh_dbus_screen_saver_call_lock_sync (ss_proxy, NULL, &err);
  g_assert_no_error (err);

  dc_proxy = phosh_dbus_display_config_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                               G_DBUS_PROXY_FLAGS_NONE,
                                                               "org.gnome.Mutter.DisplayConfig",
                                                               "/org/gnome/Mutter/DisplayConfig",
                                                               NULL,
                                                               &err);
  wait_a_bit (ctx.loop, 1000);
  phosh_dbus_display_config_set_power_save_mode (dc_proxy, 0);
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "lockscreen-status");

  activate_lockscreen_plugins (&ctx, TRUE);
  take_screenshot (what, i++, "lockscreen-plugins");
  activate_lockscreen_plugins (&ctx, FALSE);

  mpris_mock = phosh_test_mpris_mock_new ();
  phosh_mpris_mock_export (mpris_mock);
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "lockscreen-media-player");

  phosh_test_keyboard_press_keys (ctx.keyboard, ctx.timer, KEY_SPACE, NULL);
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "lockscreen-keypad");

  i = screenshot_emergency_calls (&ctx, what, i);

  calls_mock = phosh_test_calls_mock_new ();
  phosh_calls_mock_export (calls_mock);
  wait_a_bit (ctx.loop, 500);
  take_screenshot (what, i++, "lockscreen-call");

  zwp_virtual_keyboard_v1_destroy (ctx.keyboard);
}


int
main (int argc, char *argv[])
{
  g_autoptr (PhoshTestFullShellFixtureCfg) cfg = NULL;

  g_test_init (&argc, &argv, NULL);

  textdomain (GETTEXT_PACKAGE);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  phosh_test_register_resource ();

  do_settings ();

  cfg = phosh_test_full_shell_fixture_cfg_new ("phosh-keyboard-events,phosh-media-player");

  g_test_add ("/phosh/tests/take-screenshots", PhoshTestFullShellFixture, cfg,
              phosh_test_full_shell_setup, test_take_screenshots, phosh_test_full_shell_teardown);

  return g_test_run ();
}
