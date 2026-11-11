/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-config.h"
#include "testlib.h"
#include "testlib-head-stub.h"

#include "layersurface.h"
#include <gdk/gdkwayland.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>


#define STARTUP_TIMEOUT 10

static PhoshTestCompositorState *_state;
#ifndef PHOSH_USES_ASAN
/**
 * SIBABRT: raised on failed g_assert*
 * SIGTRAP: raised with G_DEBUG=fatal-{warnings,criticals}
 * SIGSEGV: the usual suspect
 */
static int handled_signals[] = { SIGABRT, SIGSEGV, SIGTRAP };
#endif

typedef struct _PhocOutputWatch {
  char      *socket;
  GMainLoop *loop;
} PhocOutputWatch;

static gboolean
phoc_stdout_watch (GIOChannel      *source,
                   GIOCondition     condition,
                   PhocOutputWatch *watch)
{
  gboolean finished = FALSE;

  if (condition & G_IO_IN) {
    GIOStatus status;
    g_autoptr (GError) err = NULL;
    g_autofree char *line = NULL;

    status = g_io_channel_read_line (source, &line, NULL, NULL, &err);

    switch (status) {
    case G_IO_STATUS_NORMAL:
    {
      g_autoptr (GRegex) socket_re = NULL;
      g_autoptr (GMatchInfo) socket_mi = NULL;

      g_test_message ("Phoc startup msg: %s", line);

      socket_re = g_regex_new ("on wayland display '(wayland-[0-9]+)'",
                               0, 0, &err);
      g_assert_nonnull (socket_re);
      g_assert_no_error (err);

      if (g_regex_match (socket_re, line, 0, &socket_mi)) {
        watch->socket = g_match_info_fetch (socket_mi, 1);
        g_assert_nonnull (watch->socket);
        g_test_message ("Found wayland socket %s", watch->socket);
        finished = TRUE;
        /* we're done */
        g_main_loop_quit (watch->loop);
      }
    }
    break;
    case G_IO_STATUS_EOF:
      finished = TRUE;
      break;
    case G_IO_STATUS_ERROR:
      finished = TRUE;
      g_warning ("Error reading from phoc: %s\n", err->message);
      return FALSE;
    case G_IO_STATUS_AGAIN:
    default:
      break;
    }
  } else if (condition & G_IO_HUP) {
    finished = TRUE;
  }

  return !finished;
}

G_NORETURN static void
on_phoc_startup_timeout (gpointer unused)
{
  g_assert_not_reached ();
}

static void
on_phoc_exit (GPid     pid,
              int      status,
              gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  if (status == 0 || status == SIGTERM)
    return;
  g_spawn_check_wait_status (status, &err);
  g_assert_no_error (err);
}


static void
install_keymap (struct zwp_virtual_keyboard_v1 *keyboard)
{
  int fd;
  void *ptr;
  size_t size;

  fd = open (TEST_DATA_DIR  "/keymap.txt", O_RDONLY);
  g_assert_cmpint (fd, >=, 0);
  size = lseek(fd, 0, SEEK_END);

  ptr = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  g_assert_true (ptr != (void *)-1);

  zwp_virtual_keyboard_v1_keymap(keyboard,
                                 WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                 fd, size);
  close (fd);
}

/*
 * Get a #PhoshMonitor for layer-surface tests
 * @state: The fixture state
 *
 * Returns:(transfer none): A monitor
 */
PhoshMonitor *
phosh_test_get_monitor (PhoshTestCompositorState *state)
{
  PhoshWayland *wl = phosh_wayland_get_default ();
  GHashTable *outputs;
  GHashTableIter iter;
  gpointer wl_output;

  if (state->monitor == NULL) {
    g_assert_true (PHOSH_IS_WAYLAND (wl));
    outputs = phosh_wayland_get_wl_outputs (wl);

    g_hash_table_iter_init (&iter, outputs);
    g_hash_table_iter_next (&iter, NULL, &wl_output);

    state->monitor = phosh_monitor_new_from_wl_output (wl_output);
    g_assert_true (PHOSH_IS_MONITOR (state->monitor));
  }

  return state->monitor;
}


#ifndef PHOSH_USES_ASAN
static void
abrt_handler (int signal)
{
  const struct sigaction act = { .sa_handler = SIG_DFL };

  if (_state->pid)
    kill (_state->pid, SIGTERM);

  /* Make sure the default handler runs */
  sigaction (signal, &act, NULL);
}
#endif


PhoshTestCompositorState *
phosh_test_compositor_new (gboolean heads_stub)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GPtrArray) argv = NULL;
  g_autoptr (GIOChannel) channel = NULL;
  g_autoptr (GMainLoop) mainloop = NULL;
  g_autofree char *wl_display_path = NULL;
  g_auto (GStrv) env = NULL;
  PhoshTestCompositorState *state;
  GSpawnFlags flags = G_SPAWN_DO_NOT_REAP_CHILD;
  const char *comp, *phoc_ini;
  gboolean ret;
  int outfd, timeout_id;
  PhocOutputWatch watch;
  struct wl_display *wl_display;

  comp = g_getenv ("PHOSH_TEST_PHOC_BINARY");
  if (!comp) {
    flags |= G_SPAWN_SEARCH_PATH;
    comp = "phoc";
  }

  phoc_ini = g_getenv ("PHOSH_TEST_PHOC_INI");
  if (phoc_ini == NULL)
    phoc_ini = TEST_PHOC_INI;

  g_test_message ("Using phoc.ini %s", phoc_ini);
  g_assert_true (g_file_test (phoc_ini, G_FILE_TEST_EXISTS));

  state = g_new0 (PhoshTestCompositorState, 1);

  argv = g_ptr_array_new ();
  g_ptr_array_add (argv, (char*)comp);
  g_ptr_array_add (argv, "-C");
  g_ptr_array_add (argv, (char*)phoc_ini);
  g_ptr_array_add (argv, NULL);

  env = g_get_environ ();
  /* Prevent abort on warning, criticals, etc, we don't test
   * the compositor */
  env = g_environ_unsetenv (env, "G_DEBUG");
  env = g_environ_setenv (env, "WLR_BACKENDS", "headless", TRUE);

  ret = g_spawn_async_with_pipes (
    NULL, /* cwd */
    (char **)argv->pdata,
    env,
    flags,
    NULL, NULL,
    &state->pid,
    NULL, &outfd, NULL,
    &err);

  g_assert_no_error (err);
  g_assert_true (ret);

  g_test_message ("Spawned compositor %s with pid %d", comp, state->pid);

  mainloop = g_main_loop_new (NULL, FALSE);
  watch.loop = mainloop;
  watch.socket = NULL;
  channel = g_io_channel_unix_new (outfd);
  g_io_channel_set_close_on_unref (channel, TRUE);
  g_io_channel_set_flags (channel,
                          g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                          NULL);
  g_io_add_watch (channel,
                  G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                  (GIOFunc)phoc_stdout_watch,
                  &watch);
  g_child_watch_add (state->pid, on_phoc_exit, NULL);

  timeout_id = g_timeout_add_seconds_once (STARTUP_TIMEOUT, on_phoc_startup_timeout, NULL);
  g_main_loop_run (mainloop);
  g_assert_true (g_source_remove (timeout_id));

  /* I/O watch in main should have gotten the socket name */
  g_assert_nonnull (watch.socket);
  wl_display_path = g_strdup_printf ("%s/%s", g_getenv ("XDG_RUNTIME_DIR"), watch.socket);
  g_test_message ("Using Wayland socket at: '%s'", wl_display_path);
  g_setenv ("WAYLAND_DISPLAY", wl_display_path, TRUE);

  /* Check if we can connect to the display */
  wl_display = wl_display_connect (wl_display_path);
  g_assert_nonnull (wl_display);
  wl_display_disconnect (wl_display);

  /* Set up wayland protocol */
  gdk_set_allowed_backends ("wayland");
  /*
   * Don't let GDK decide whether to open a display, we want the one
   * from the freshly spawned compositor
   */
  g_test_message ("Opening GDK wayland display at %s", wl_display_path);
  state->gdk_display = gdk_display_open (wl_display_path);
  g_assert_nonnull (state->gdk_display);

  state->wl = phosh_wayland_get_default ();
  g_test_message ("Connected phosh-wayland to Wayland socket %s", wl_display_path);
  g_free (watch.socket);

  if (heads_stub)
    phosh_test_head_stub_init (state->wl);

  g_assert_null (_state);
  _state = state;

#ifndef PHOSH_USES_ASAN
  /* Install a ABRT handler that terminates the compositor quickly as we otherwise have
     to wait for a timeout */
  for (int i = 0; i < G_N_ELEMENTS (handled_signals); i++) {
    int signal = handled_signals[i];
    const struct sigaction act = { .sa_handler = abrt_handler };
    struct sigaction old_act;

    sigaction (signal, NULL, &old_act);
    g_assert_true (old_act.sa_handler == SIG_DFL);

    sigaction (signal, &act, NULL);
  }
#endif

  return state;
}

void
phosh_test_compositor_free (PhoshTestCompositorState *state)
{
  if (state == NULL)
    return;

  g_assert_true (state == _state);

  if (state->monitor)
    g_assert_finalize_object (state->monitor);

  phosh_test_head_stub_destroy ();

  g_assert_finalize_object (state->wl);

  gdk_display_close (state->gdk_display);
  kill (state->pid, SIGTERM);
  g_spawn_close_pid (state->pid);

#ifndef PHOSH_USES_ASAN
  for (int i = 0; i < G_N_ELEMENTS (handled_signals); i++) {
    struct sigaction act = { .sa_handler = SIG_DFL };
    int signal = handled_signals[i];

    sigaction (signal, &act, NULL);
  }
#endif

  g_clear_pointer (&_state, g_free);
}

/**
 * phosh_test_keyboard_new:
 * @wl: A #PhoshWayland object to get the necessary interfaces from
 *
 * Set up a virtual keyboard and add a keymap.
 */
struct zwp_virtual_keyboard_v1 *
phosh_test_keyboard_new (PhoshWayland *wl)
{
  struct zwp_virtual_keyboard_v1 *keyboard;

  keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard (
    phosh_wayland_get_zwp_virtual_keyboard_manager_v1 (wl),
    phosh_wayland_get_wl_seat (wl));
  install_keymap (keyboard);

  return keyboard;
}

/**
 * phosh_test_press_key:
 * @keyboard: A virtual keyboard
 * @timer: A #GTimer to get timestamps from.
 *
 * Emits keyboard events based on input event codes.
 */
void
phosh_test_keyboard_press_keys (struct zwp_virtual_keyboard_v1 *keyboard, GTimer *timer, ...)
{
  va_list args;
  guint key;
  guint time;

  va_start (args, timer);
  do {
    key = va_arg (args, guint);
    if (key == 0)
      break;

    time = (guint)g_timer_elapsed (timer, NULL) * 1000;
    zwp_virtual_keyboard_v1_key (keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    time = (guint)g_timer_elapsed (timer, NULL) * 1000;
    zwp_virtual_keyboard_v1_key (keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
  } while (TRUE);
  va_end (args);
}

/**
 * phosh_test_press_key_timeout:
 * @keyboard: A virtual keyboard
 * @timer: A #GTimer to get timestamps from.
 *
 * Emits keyboard events based on input event codes.
 */
void
phosh_test_keyboard_press_timeout (struct zwp_virtual_keyboard_v1 *keyboard,
                                   GTimer                         *timer,
                                   guint                           key,
                                   guint                           sleep)
{
  guint time;

  time = (guint)g_timer_elapsed (timer, NULL) * 1000;
  zwp_virtual_keyboard_v1_key (keyboard, time, key, WL_KEYBOARD_KEY_STATE_PRESSED);
  usleep (sleep * 1000);
  time = (guint)g_timer_elapsed (timer, NULL) * 1000;
  zwp_virtual_keyboard_v1_key (keyboard, time, key, WL_KEYBOARD_KEY_STATE_RELEASED);
}


/**
 * phosh_test_press_modifiers
 * @keyboard: A virtual keyboard
 * @modifiers: The modifiers
 *
 * Latch (press) the given modifiers
 */
void
phosh_test_keyboard_press_modifiers (struct zwp_virtual_keyboard_v1 *keyboard,
                                     guint                           keys)

{
  guint modifiers = 0;

  /* Modifiers passed to the virtual_keyboard protocol as used by wl_keyboard */
  switch (keys) {
  case KEY_LEFTSHIFT:
  case KEY_RIGHTSHIFT:
    modifiers |= (1 << 0);
    break;
  case KEY_LEFTCTRL:
  case KEY_RIGHTCTRL:
    modifiers |= (1 << 2);
    break;
  case KEY_LEFTALT:
  case KEY_RIGHTALT:
    modifiers |= (1 << 3);
    break;
  case KEY_LEFTMETA:
  case KEY_RIGHTMETA:
    modifiers |= (1 << 6);
    break;
  default:
    g_error ("Unknown modifier key '%d'", keys);
  }

  zwp_virtual_keyboard_v1_modifiers (keyboard, modifiers, 0, 0, 0);
}


/**
 * phosh_test_release_modifiers:
 * @keyboard: A virtual keyboard
 *
 * Release all modifiers
 */
void
phosh_test_keyboard_release_modifiers (struct zwp_virtual_keyboard_v1 *keyboard)
{
  zwp_virtual_keyboard_v1_modifiers (keyboard, 0, 0, 0, 0);
}

/**
 * phosh_test_remove_tree:
 * file: The tree to remove
 *
 * Removes the filesystem tree at `file`
 */
void
phosh_test_remove_tree (GFile *file)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;

  enumerator = g_file_enumerate_children (file, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          NULL, NULL);

  while (enumerator != NULL) {
    GFile *child;
    gboolean ret;

    ret = g_file_enumerator_iterate (enumerator, NULL, &child, NULL, &err);
    g_assert_no_error (err);
    g_assert_true (ret);

    if (child == NULL)
      break;

    phosh_test_remove_tree (child);
  }

  g_assert_true (g_file_delete (file, NULL, &err));
  g_assert_no_error (err);
}
