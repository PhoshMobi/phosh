/*
 * Copyright (C) 2018 Purism SPC
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-main"

#include "phosh-config.h"

#include "audio-manager.h"
#include "shell-priv.h"
#include "phosh-wayland.h"
#include "wall-clock.h"
#include "fake-clock.h"
#include "background-cache.h"
#include "metainfo-cache.h"

#include <handy.h>
#include <libfeedback.h>

#include <glib/gi18n.h>
#include <glib-unix.h>

#include <systemd/sd-daemon.h>

static gboolean
quit (gpointer unused)
{
  g_debug ("Cleaning up");
  gtk_main_quit ();

  return G_SOURCE_REMOVE;
}


static gboolean
on_shutdown_signal (gpointer unused)
{
  guint id;

  phosh_shell_fade_out (phosh_shell_get_default (), 0);
  id = g_timeout_add_seconds (2, (GSourceFunc)quit, NULL);
  g_source_set_name_by_id (id, "[PhoshMain] quit");

  return FALSE;
}


G_NORETURN static void
print_version (void)
{
  printf ("Phosh %s - A Wayland shell for mobile devices\n", PHOSH_VERSION);
  exit (0);
}


static void
on_shell_ready (PhoshShell *shell, GTimer *timer)
{
  g_timer_stop (timer);
  g_message ("Phosh ready after %.2fs", g_timer_elapsed (timer, NULL));

  /* Inform systemd we're up */
  sd_notify (0, "READY=1");

  g_signal_handlers_disconnect_by_data (shell, timer);
}


static PhoshWallClock *
get_clock (void)
{
  PhoshWallClock *wall_clock = NULL;
  g_autoptr (GDateTime) dt = NULL;
  const char *clock_str = g_getenv ("PHOSH_FAKE_CLOCK");

  if (clock_str) {
    if (g_str_equal (clock_str, "now")) {
      dt = g_date_time_new_now_local ();
    } else {
      dt = g_date_time_new_from_iso8601 (clock_str, NULL);
      if (!dt)
        g_critical ("Failed to create fake clock from '%s'", clock_str);
    }
  }

  if (dt)
    wall_clock = PHOSH_WALL_CLOCK (phosh_fake_clock_new (dt));

  if (!wall_clock)
    wall_clock = phosh_wall_clock_new ();

  return wall_clock;
}


int
main (int argc, char *argv[])
{
  g_autoptr (GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  gboolean unlocked = FALSE, locked = FALSE, version = FALSE;
  g_autoptr (PhoshWayland) wl = NULL;
  g_autoptr (PhoshShell) shell = NULL;
  g_autoptr (PhoshBackgroundCache) background_cache = NULL;
  g_autoptr (PhoshMetainfoCache) metainfo_cache = NULL;
  g_autoptr (GTimer) timer = g_timer_new ();
  g_autoptr (PhoshWallClock) wall_clock = NULL;
  g_autoptr (PhoshAudioManager) audio_manager = NULL;

  const GOptionEntry options [] = {
    {"unlocked", 'U', 0, G_OPTION_ARG_NONE, &unlocked,
     "Don't start with screen locked", NULL},
    {"locked", 'L', 0, G_OPTION_ARG_NONE, &locked,
     "Start with screen locked, no matter what", NULL},
    {"version", 0, 0, G_OPTION_ARG_NONE, &version,
     "Show version information", NULL},
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };

  opt_context = g_option_context_new ("- A phone graphical shell");
  g_option_context_add_main_entries (opt_context, options, NULL);
  g_option_context_add_group (opt_context, gtk_get_option_group (FALSE));
  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_warning ("%s", err->message);
    return 1;
  }

  if (version)
    print_version ();

  textdomain (GETTEXT_PACKAGE);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  gtk_init (&argc, &argv);
  hdy_init ();
  lfb_init (PHOSH_APP_ID, NULL);

  g_unix_signal_add (SIGTERM, on_shutdown_signal, NULL);
  g_unix_signal_add (SIGINT, on_shutdown_signal, NULL);

  wall_clock = get_clock ();
  phosh_wall_clock_set_default (wall_clock);
  wl = phosh_wayland_get_default ();
  background_cache = phosh_background_cache_get_default ();
  metainfo_cache = phosh_metainfo_cache_get_default ();
  audio_manager = phosh_audio_manager_get_default ();
  shell = phosh_shell_new ();
  phosh_shell_set_default (shell);

  g_signal_connect (shell, "ready", G_CALLBACK (on_shell_ready), timer);

  if (!(unlocked || phosh_shell_started_by_display_manager (shell)) || locked)
    phosh_shell_lock (shell);

  gtk_main ();

  return EXIT_SUCCESS;
}
