/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-splash-manager"

#include "phosh-config.h"

#include "shell-priv.h"
#include "splash.h"
#include "splash-manager.h"

#include <gtk/gtk.h>

/**
 * PhoshSplashManager:
 *
 * Handles splash screens
 *
 * Spawns, keeps track and closes splash screens.
 */

enum {
  PROP_0,
  PROP_APP_TRACKER,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshSplashManager {
  GObject          parent;

  PhoshAppTracker *app_tracker;
  GHashTable      *splashes;

  GSettings       *interface_settings;
  gboolean         prefer_dark;
};
G_DEFINE_TYPE (PhoshSplashManager, phosh_splash_manager, G_TYPE_OBJECT)


static void
phosh_splash_manager_set_property (GObject      *object,
                                   guint         property_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PhoshSplashManager *self = PHOSH_SPLASH_MANAGER (object);

  switch (property_id) {
  case PROP_APP_TRACKER:
    self->app_tracker = g_value_dup_object (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_splash_manager_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhoshSplashManager *self = PHOSH_SPLASH_MANAGER (object);

  switch (property_id) {
  case PROP_APP_TRACKER:
    g_value_set_object (value, self->app_tracker);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
on_splash_closed (PhoshSplashManager *self, GtkWidget *splash)
{
  const char *key;

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));
  g_return_if_fail (PHOSH_IS_SPLASH (splash));

  key = g_object_get_data (G_OBJECT (splash), "startup-id");

  g_return_if_fail (g_hash_table_remove (self->splashes, key));
}


static void
on_app_ready (PhoshSplashManager *self,
              GDesktopAppInfo    *info,
              const char         *startup_id,
              PhoshAppTracker    *tracker)
{
  PhoshSplash *splash;

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));
  g_return_if_fail (G_IS_DESKTOP_APP_INFO (info));
  g_return_if_fail (startup_id);
  g_debug ("Removing splash for %s, startup_id %s", g_app_info_get_id (G_APP_INFO (info)),
           startup_id);

  splash = g_hash_table_lookup (self->splashes, startup_id);
  /* E.g. firefox sends the same startup id for multiple windows */
  if (!splash) {
    g_debug ("No splash for startup_id %s", startup_id);
    return;
  }

  g_hash_table_remove (self->splashes, startup_id);
}


static void
on_app_failed (PhoshSplashManager *self,
               GDesktopAppInfo    *info,
               const char         *startup_id,
               PhoshAppTracker    *tracker)
{
  GtkWidget *splash;

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));
  g_return_if_fail (G_IS_DESKTOP_APP_INFO (info));
  g_return_if_fail (startup_id);
  g_debug ("Removing splash for failed %s, startup_id %s",
           g_app_info_get_id (G_APP_INFO (info)),
           startup_id);

  splash = g_hash_table_lookup (self->splashes, startup_id);
  /* E.g. firefox sends the same startup id for multiple windows */
  if (!splash) {
    g_debug ("No splash for startup_id %s", startup_id);
    return;
  }

  /* TODO: show failed splash once we have designs */
  g_hash_table_remove (self->splashes, startup_id);
}


static void
on_app_launch_started (PhoshSplashManager *self,
                       GDesktopAppInfo    *info,
                       const char         *startup_id,
                       PhoshAppTracker    *tracker)
{
  GtkWidget *splash;
  char *key;
  PhoshShell *shell = phosh_shell_get_default ();

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));
  g_return_if_fail (G_IS_DESKTOP_APP_INFO (info));
  g_return_if_fail (startup_id);
  g_return_if_fail (!g_hash_table_contains (self->splashes, startup_id));

  if (!phosh_shell_get_show_splash (shell))
    return;

  g_debug ("Adding splash for %s, startup_id %s", g_app_info_get_id (G_APP_INFO (info)),
           startup_id);
  splash = phosh_splash_new (info, self->prefer_dark);
  key = g_strdup (startup_id);
  g_hash_table_insert (self->splashes, key, splash);
  g_signal_connect_object (splash, "closed", G_CALLBACK (on_splash_closed),
                           self, G_CONNECT_SWAPPED);
  /* Keep startup-id for close triggered by splash itself */
  g_object_set_data (G_OBJECT (splash), "startup-id", key);
  gtk_window_present (GTK_WINDOW (splash));
}


static void
gsettings_color_scheme_changed_cb (PhoshSplashManager *self)
{
  self->prefer_dark = (g_settings_get_enum (self->interface_settings, "color-scheme") ==
                       G_DESKTOP_COLOR_SCHEME_PREFER_DARK);
}


static void
phosh_splash_manager_constructed (GObject *object)
{
  PhoshSplashManager *self = PHOSH_SPLASH_MANAGER (object);

  G_OBJECT_CLASS (phosh_splash_manager_parent_class)->constructed (object);

  g_object_connect (self->app_tracker,
                    "swapped-signal::app-launch-started", G_CALLBACK (on_app_launch_started), self,
                    "swapped-signal::app-failed", G_CALLBACK (on_app_failed), self,
                    "swapped-signal::app-ready", G_CALLBACK (on_app_ready), self,
                    NULL);
}


static void
phosh_splash_manager_dispose (GObject *object)
{
  PhoshSplashManager *self = PHOSH_SPLASH_MANAGER (object);

  g_clear_object (&self->interface_settings);
  g_clear_object (&self->app_tracker);

  G_OBJECT_CLASS (phosh_splash_manager_parent_class)->dispose (object);
}


static void
phosh_splash_manager_finalize (GObject *object)
{
  PhoshSplashManager *self = PHOSH_SPLASH_MANAGER (object);

  g_clear_pointer (&self->splashes, g_hash_table_destroy);

  G_OBJECT_CLASS (phosh_splash_manager_parent_class)->finalize (object);
}


static void
phosh_splash_manager_class_init (PhoshSplashManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_splash_manager_get_property;
  object_class->set_property = phosh_splash_manager_set_property;
  object_class->constructed = phosh_splash_manager_constructed;
  object_class->dispose = phosh_splash_manager_dispose;
  object_class->finalize = phosh_splash_manager_finalize;

  props[PROP_APP_TRACKER] =
    g_param_spec_object ("app-tracker", "", "",
                         PHOSH_TYPE_APP_TRACKER,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_splash_manager_init (PhoshSplashManager *self)
{
  self->splashes = g_hash_table_new_full (g_str_hash,
                                          g_str_equal,
                                          g_free,
                                          (GDestroyNotify) phosh_splash_hide);

  self->interface_settings = g_settings_new ("org.gnome.desktop.interface");
  g_signal_connect_swapped (self->interface_settings,
                            "changed::color-scheme",
                            G_CALLBACK (gsettings_color_scheme_changed_cb),
                            self);
  gsettings_color_scheme_changed_cb (self);
}


PhoshSplashManager *
phosh_splash_manager_new (PhoshAppTracker *app_tracker)
{
  return PHOSH_SPLASH_MANAGER (g_object_new (PHOSH_TYPE_SPLASH_MANAGER,
                                             "app-tracker", app_tracker,
                                             NULL));
}


void
phosh_splash_manager_lower_all (PhoshSplashManager *self)
{
  GHashTableIter iter;
  gpointer key, value;

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));

  g_hash_table_iter_init (&iter, self->splashes);

  while (g_hash_table_iter_next (&iter, &key, &value)) {
    PhoshSplash *splash = PHOSH_SPLASH (value);

    phosh_splash_lower (splash);
  }
}


void
phosh_splash_manager_raise (PhoshSplashManager *self, const char *startup_id)
{
  PhoshSplash *splash;

  g_return_if_fail (PHOSH_IS_SPLASH_MANAGER (self));

  splash = g_hash_table_lookup (self->splashes, startup_id);
  if (splash == NULL) {
    g_warning ("Invalid startup-id %s", startup_id);
    return;
  }

  phosh_splash_raise (splash);
}


gboolean
phosh_splash_manager_get_prefer_dark (PhoshSplashManager *self)
{
  g_return_val_if_fail (PHOSH_IS_SPLASH_MANAGER (self), FALSE);

  return self->prefer_dark;
}
