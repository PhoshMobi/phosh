/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-style-manager"

#include "phosh-config.h"

#include "style-manager.h"
#include "util.h"

#include <gtk/gtk.h>
#include <gdesktop-enums.h>

#define IF_KEY_ACCENT_COLOR     "accent-color"
#define IF_SCHEMA_NAME          "org.gnome.desktop.interface"

/* Accent colors from gnome-shell src/st/st-theme-context.c */
#define ACCENT_COLOR_BLUE       "#3584e4"
#define ACCENT_COLOR_TEAL       "#2190a4"
#define ACCENT_COLOR_GREEN      "#3a944a"
#define ACCENT_COLOR_YELLOW     "#c88800"
#define ACCENT_COLOR_ORANGE     "#ed5b00"
#define ACCENT_COLOR_RED        "#e62d42"
#define ACCENT_COLOR_PINK       "#d56199"
#define ACCENT_COLOR_PURPLE     "#9141ac"
#define ACCENT_COLOR_SLATE      "#6f8396"
#define ACCENT_COLOR_FOREGROUND "#ffffff"

/**
 * PhoshStyleManager:
 *
 * The style manager is responsible for picking style sheets and
 * themes and notifying other parts of the shell about changes.
 */

enum {
  PROP_0,
  PROP_THEME_NAME,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];


struct _PhoshStyleManager {
  GObject         parent;

  char           *theme_name;
  GtkCssProvider *css_provider;
  GtkCssProvider *accent_css_provider;

  GSettings      *interface_settings;
};
G_DEFINE_TYPE (PhoshStyleManager, phosh_style_manager, G_TYPE_OBJECT)


static void
on_accent_color_changed (PhoshStyleManager *self)
{
  const char *color;
  g_autofree char *css  = NULL;
  g_autoptr (GtkCssProvider) provider = gtk_css_provider_new ();

  if (self->accent_css_provider) {
    gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                  GTK_STYLE_PROVIDER (self->accent_css_provider));
  }

  /* Only enable accent colors on Adwaita */
  if (g_strcmp0 (self->theme_name, "Adwaita") != 0)
    return;

  switch (g_settings_get_enum (self->interface_settings, IF_KEY_ACCENT_COLOR)) {
  case G_DESKTOP_ACCENT_COLOR_TEAL:
    color = ACCENT_COLOR_TEAL;
    break;
  case G_DESKTOP_ACCENT_COLOR_GREEN:
    color = ACCENT_COLOR_GREEN;
    break;
  case G_DESKTOP_ACCENT_COLOR_YELLOW:
    color = ACCENT_COLOR_YELLOW;
    break;
  case G_DESKTOP_ACCENT_COLOR_ORANGE:
    color = ACCENT_COLOR_ORANGE;
    break;
  case G_DESKTOP_ACCENT_COLOR_RED:
    color = ACCENT_COLOR_RED;
    break;
  case G_DESKTOP_ACCENT_COLOR_PINK:
    color = ACCENT_COLOR_PINK;
    break;
  case G_DESKTOP_ACCENT_COLOR_PURPLE:
    color = ACCENT_COLOR_PURPLE;
    break;
  case G_DESKTOP_ACCENT_COLOR_SLATE:
    color = ACCENT_COLOR_SLATE;
    break;
  case G_DESKTOP_ACCENT_COLOR_BLUE:
  default:
    color = ACCENT_COLOR_BLUE;
  }

  g_debug ("Setting accent bg color to %s, accent fg color to %s",
           color, ACCENT_COLOR_FOREGROUND);

  css = g_strdup_printf ("@define-color theme_selected_bg_color %s;\n"
                         "@define-color theme_selected_fg_color %s;",
                         color, ACCENT_COLOR_FOREGROUND);
  gtk_css_provider_load_from_data (provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  g_set_object (&self->accent_css_provider, provider);
}


static void
on_gtk_theme_name_changed (PhoshStyleManager *self, GParamSpec *pspec, GtkSettings *settings)
{
  const char *style;
  g_autofree char *name = NULL;
  g_autoptr (GtkCssProvider) provider = gtk_css_provider_new ();

  g_object_get (settings, "gtk-theme-name", &name, NULL);

  if (g_strcmp0 (self->theme_name, name) == 0)
    return;

  self->theme_name = g_steal_pointer (&name);
  g_debug ("GTK theme: %s", self->theme_name);

  if (self->css_provider) {
    gtk_style_context_remove_provider_for_screen (gdk_screen_get_default (),
                                                  GTK_STYLE_PROVIDER (self->css_provider));
  }

  style = phosh_style_manager_get_stylesheet (self->theme_name);

  gtk_css_provider_load_from_resource (provider, style);
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_set_object (&self->css_provider, provider);

  /* Refresh accent color */
  on_accent_color_changed (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_THEME_NAME]);
}


static void
phosh_style_manager_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  PhoshStyleManager *self = PHOSH_STYLE_MANAGER (object);

  switch (property_id) {
  case PROP_THEME_NAME:
    g_value_set_string (value, self->theme_name);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_style_manager_dispose (GObject *object)
{
  PhoshStyleManager *self = PHOSH_STYLE_MANAGER (object);

  g_clear_pointer (&self->theme_name, g_free);
  g_clear_object (&self->css_provider);
  g_clear_object (&self->accent_css_provider);

  g_clear_object (&self->interface_settings);

  G_OBJECT_CLASS (phosh_style_manager_parent_class)->dispose (object);
}


static void
phosh_style_manager_class_init (PhoshStyleManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_style_manager_get_property;
  object_class->dispose = phosh_style_manager_dispose;

  props[PROP_THEME_NAME] =
    g_param_spec_string ("theme-name", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_style_manager_init (PhoshStyleManager *self)
{
  GtkSettings *gtk_settings = gtk_settings_get_default ();

  g_object_set (G_OBJECT (gtk_settings), "gtk-application-prefer-dark-theme", TRUE, NULL);

  self->interface_settings = g_settings_new (IF_SCHEMA_NAME);

  g_signal_connect_swapped (self->interface_settings,
                            "changed::" IF_KEY_ACCENT_COLOR,
                            G_CALLBACK (on_accent_color_changed),
                            self);
  g_signal_connect_swapped (gtk_settings,
                            "notify::gtk-theme-name",
                            G_CALLBACK (on_gtk_theme_name_changed),
                            self);
  on_gtk_theme_name_changed (self, NULL, gtk_settings);
}


PhoshStyleManager *
phosh_style_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_STYLE_MANAGER, NULL);
}

/**
 * phosh_style_manager_get_stylesheet:
 * @theme_name: A theme name
 *
 * Get the proper style sheet based on the given theme name
 */
const char *
phosh_style_manager_get_stylesheet (const char *theme_name)
{
  const char *style;

  if (g_strcmp0 (theme_name, "HighContrast") == 0)
    style = "/mobi/phosh/stylesheet/adwaita-hc-light.css";
  else
    style = "/mobi/phosh/stylesheet/adwaita-dark.css";

  return style;
}

/**
 * phosh_style_manager_get_theme_name:
 * @self; The style manager
 *
 * Get the current theme name
 *
 * Returns: The theme name
 */
const char *
phosh_style_manager_get_theme_name (PhoshStyleManager *self)
{
  g_return_val_if_fail (PHOSH_IS_STYLE_MANAGER (self), NULL);

  return self->theme_name;
}

/**
 * phosh_style_manager_is_high_contrast:
 * @self; The style manager
 *
 * Get whether the current theme is a high contrast theme
 *
 * Returns: `True` if current theme is high contrast
 */
gboolean
phosh_style_manager_is_high_contrast (PhoshStyleManager *self)
{
  g_return_val_if_fail (PHOSH_IS_STYLE_MANAGER (self), FALSE);

  return g_strcmp0 (self->theme_name, "HighContrast") == 0;
}
