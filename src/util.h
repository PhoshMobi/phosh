/*
 * Copyright (C) 2018 Purism SPC
 *               2024-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>

G_BEGIN_DECLS

#define phosh_async_error_warn(err, ...) \
  phosh_error_warnv (G_LOG_DOMAIN, err, G_IO_ERROR, G_IO_ERROR_CANCELLED, __VA_ARGS__)

#define phosh_dbus_service_error_warn(err, ...)                   \
  phosh_error_warnv2 (G_LOG_DOMAIN, err, G_IO_ERROR,              \
                      G_IO_ERROR_NOT_FOUND, G_IO_ERROR_CANCELLED, \
                      __VA_ARGS__)

/**
 * PHOSH_UTIL_BUILD_KEYBINDING:
 * @actions: (array)(element-type GActionEntry)(inout): The action array to build
 * @builder: A `GStrvBuilder` for the action names
 * @settings: The settings
 * @key: The settings key
 * @callback: The callback to invoke when the keybinding is pressed
 *
 * Helper to construct keybindings easily
 *
 * Append the actions for the keybindings found in the settings and add the keybindings
 * strings to builder.
 */
#define PHOSH_UTIL_BUILD_KEYBINDING(actions, builder, settings, key, callback) \
  G_STMT_START {                                                        \
    GStrv _bindings = g_settings_get_strv (settings, key);              \
    for (int i = 0; _bindings[i]; i++) {                                \
      GActionEntry _entry = { .name = _bindings[i], .activate = callback }; \
      g_array_append_val (actions, _entry);                             \
      g_strv_builder_take (builder, _bindings[i]);                      \
    }                                                                   \
    /* Free container but keep individual strings for action_names */   \
    g_free (_bindings);                                                 \
  } G_STMT_END

void             phosh_cp_widget_destroy (void *widget);
GDesktopAppInfo *phosh_get_desktop_app_info_for_app_id (const char *app_id);
char            *phosh_munge_app_id (const char *app_id);
char            *phosh_strip_suffix_from_app_id (const char *app_id);
gboolean         phosh_find_systemd_session (char **session_id);
gboolean         phosh_error_warnv (const char *log_domain,
                                    GError     *err,
                                    GQuark      domain,
                                    int         code,
                                    const char *fmt,
                                    ...) G_GNUC_PRINTF(5, 6);
gboolean         phosh_error_warnv2 (const char  *log_domain,
                                     GError      *err,
                                     GQuark       domain,
                                     int          code1,
                                     int          code2,
                                     const gchar *fmt,
                                     ...) G_GNUC_PRINTF(6, 7);
int              phosh_create_shm_file (off_t size);
char            *phosh_util_escape_markup (const char *markup, gboolean allow_markup);
gboolean         phosh_util_gesture_is_touch (GtkGestureSingle *gesture);
gboolean         phosh_util_have_gnome_software (gboolean scan);
void             phosh_util_toggle_style_class (GtkWidget *widget, const char *style_class, gboolean toggle);
gboolean         phosh_clear_fd (int *fd, GError **err);
const char      *phosh_util_get_icon_by_wifi_strength (guint strength, gboolean is_connecting);
gboolean         phosh_util_file_equal (GFile *file1, GFile *file2);
GdkPixbuf       *phosh_util_data_uri_to_pixbuf (const char *uri, GError **error);
GdkPixbuf *      phosh_utils_pixbuf_scale_to_min (GdkPixbuf *src, int min_width, int min_height);
gboolean         phosh_util_matches_app_info (GAppInfo *info, const char *search);
GStrv            phosh_util_append_to_strv (GStrv array, const char *element);
GStrv            phosh_util_remove_from_strv (GStrv array, const char *element);
void             phosh_util_open_settings_panel (const char         *panel,
                                                 gboolean            mobile,
                                                 GCancellable       *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            user_data);
gboolean         phosh_util_open_settings_panel_finish (GAsyncResult *res, GError **err);
float *          phosh_util_calculate_supported_mode_scales (guint32   width,
                                                             guint32   height,
                                                             int      *n_supported_scales,
                                                             gboolean  fractional);
GVariant *       phosh_util_get_platform_data (GAppInfo *info);


G_END_DECLS
