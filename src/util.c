/*
 * Copyright (C) 2018 Purism SPC
 *               2024-2025 The Phosh Developers
 *               2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-util"

#define _GNU_SOURCE

#include "phosh-config.h"

#include "app-list-model.h"

#include "util.h"
#include <gtk/gtk.h>

#include <locale.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <systemd/sd-login.h>

#include <libsoup/soup.h>

#include <gmobile.h>

#ifdef PHOSH_HAVE_MEMFD_CREATE
#include <linux/memfd.h>
#include <linux/mman.h>
#endif

#include <sys/mman.h>
#include <fcntl.h>

#include <math.h>

static gboolean have_gnome_software = -1;


/* Just wraps gtk_widget_destroy so we can use it with g_clear_pointer */
void
phosh_cp_widget_destroy (void *widget)
{
  gtk_widget_destroy (GTK_WIDGET (widget));
}


/**
 * phosh_get_desktop_app_info_for_app_id:
 * @app_id: the app_id
 *
 * Looks up an app info object for specified application ID.
 * Tries a bunch of transformations in order to maximize compatibility
 * with X11 and non-GTK applications that may not report the exact same
 * string as their app-id and in their desktop file.
 *
 * Returns: (transfer full)(nullable): GDesktopAppInfo for requested app_id
 */
GDesktopAppInfo *
phosh_get_desktop_app_info_for_app_id (const char *app_id)
{
  g_autofree char *desktop_id = NULL;
  g_autofree char *lowercase = NULL;
  GDesktopAppInfo *app_info = NULL;
  char *last_component;
  PhoshAppListModel *model = phosh_app_list_model_get_default ();

  g_assert (app_id);

  desktop_id = g_strdup_printf ("%s.desktop", app_id);
  g_return_val_if_fail (desktop_id, NULL);
  app_info = g_desktop_app_info_new (desktop_id);

  if (app_info)
    return app_info;

  app_info = phosh_app_list_model_lookup_by_startup_wm_class (model, app_id);
  if (app_info)
    return g_object_ref (app_info);

  app_info = phosh_app_list_model_lookup_by_exec (model, app_id);
  if (app_info)
    return g_object_ref (app_info);

  /* try to handle the case where app-id is rev-DNS, but desktop file is not */
  last_component = strrchr (app_id, '.');
  if (last_component) {
    /* Skip past '.' */
    last_component++;
    g_free (desktop_id);
    desktop_id = g_strdup_printf ("%s.desktop", last_component);
    g_return_val_if_fail (desktop_id, NULL);
    app_info = g_desktop_app_info_new (desktop_id);
    if (app_info)
      return app_info;
  }

  /* X11 WM_CLASS is often capitalized, so try in lowercase as well */
  lowercase = g_utf8_strdown (last_component ?: app_id, -1);
  g_clear_pointer (&desktop_id, g_free);

  app_info = phosh_app_list_model_lookup_by_startup_wm_class (model, lowercase);
  if (app_info)
    return g_object_ref (app_info);

  app_info = phosh_app_list_model_lookup_by_exec (model, lowercase);
  if (app_info)
    return g_object_ref (app_info);

  g_message ("Could not find application for app-id '%s'", app_id);
  return NULL;
}

/**
 * phosh_munge_app_id:
 * @app_id: the app_id
 *
 * Munges an app_id according to the rules used by
 * gnome-shell, feedbackd and phoc for gsettings:
 *
 * Returns: The munged_app id
 */
char *
phosh_munge_app_id (const char *app_id)
{
  char *id = g_strdup (app_id);
  int i;

  if (g_str_has_suffix (id, ".desktop")) {
    char *c = g_strrstr (id, ".desktop");
    if (c)
      *c = '\0';
  }

  g_strcanon (id,
              "0123456789"
              "abcdefghijklmnopqrstuvwxyz"
              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
              "-",
              '-');
  for (i = 0; id[i] != '\0'; i++)
    id[i] = g_ascii_tolower (id[i]);

  return id;
}


/**
 * phosh_strip_suffix_from_app_id:
 * @app_id: the app_id
 *
 * Strip the desktop suffix from app_id.
 *
 * Returns: (transfer full): The munged_app id
 */
char *
phosh_strip_suffix_from_app_id (const char *app_id)
{
  char *new_id = g_strdup (app_id);

  if (new_id && g_str_has_suffix (app_id, ".desktop")) {
    *(new_id + strlen (new_id) - 8 /* strlen (".desktop") */) = '\0';
  }

  return new_id;
}


gboolean
phosh_find_systemd_session (char **session_id)
{
  g_autofree char *session = NULL;
  int r;

  g_return_val_if_fail (session_id != NULL && *session_id == NULL, FALSE);

  r = sd_pid_get_session (getpid(), &session);
  if (r == 0) {
    *session_id = g_steal_pointer (&session);
    return TRUE;
  }

  /* Not in a system session, so let logind make a pick */
  r = sd_uid_get_display(getuid(), &session);
  if (r == 0) {
    *session_id = g_steal_pointer (&session);
    return TRUE;
  }

  return FALSE;
}

/**
 * phosh_async_error_warn:
 * @err: (nullable): The error to check and print
 * @...: Format string followed by parameters to insert
 *       into the format string (as with printf())
 *
 * Prints a warning when @err is 'real' error. If it merely represents
 * a canceled operation it just logs a debug message. This is useful
 * to avoid this common pattern in async callbacks.
 *
 * Returns: TRUE if #err is cancellation.
 */

/**
 * phosh_dbus_service_error_warn
 * @err: (nullable): The error to check and print
 * @...: Format string followed by parameters to insert
 *       into the format string (as with printf())
 *
 * Prints a warning when @err is 'real' error. If it merely indicates
 * that the DBus service is not present at all it just logs a debug
 * message.
 *
 * Returns: TRUE if #err is cancellation.
 */

/* Helper since phosh_async_error_warn needs to be a macro to capture log_domain */
gboolean
phosh_error_warnv (const char *log_domain,
                   GError     *err,
                   GQuark      domain,
                   gint        code,
                   const char *fmt, ...)
{
  g_autofree char *msg = NULL;
  gboolean matched = FALSE;
  va_list args;

  if (err == NULL)
    return FALSE;

  va_start (args, fmt);
  msg = g_strdup_vprintf(fmt, args);
  va_end (args);

  if (g_error_matches (err, domain, code))
    matched = TRUE;

  g_log (log_domain,
         matched ? G_LOG_LEVEL_DEBUG : G_LOG_LEVEL_WARNING,
         "%s: %s", msg, err->message);

  return matched;
}


gboolean
phosh_error_warnv2 (const char *log_domain,
                    GError     *err,
                    GQuark      domain,
                    int         code1,
                    int         code2,
                    const char *fmt, ...)
{
  g_autofree char *msg = NULL;
  gboolean matched = FALSE;
  va_list args;

  if (err == NULL)
    return FALSE;

  va_start (args, fmt);
  msg = g_strdup_vprintf(fmt, args);
  va_end (args);

  if (g_error_matches (err, domain, code1) ||
      g_error_matches (err, domain, code2))
    matched = TRUE;

  g_log (log_domain,
         matched ? G_LOG_LEVEL_DEBUG : G_LOG_LEVEL_WARNING,
         "%s: %s", msg, err->message);

  return matched;
}


static void
randname (char *buf)
{
  struct timespec ts;
  long r;
  clock_gettime (CLOCK_REALTIME, &ts);
  r = ts.tv_nsec;
  for (int i = 0; i < 6; ++i) {
    buf[i] = 'A'+(r&15)+(r&16)*2;
    r >>= 5;
  }
}


static int
anonymous_shm_open (void)
{
  char name[] = "/phosh-XXXXXX";
  int fd = -1;

#ifdef PHOSH_HAVE_MEMFD_CREATE
/* For kernel headers before 6.3 */
# ifndef MFD_NOEXEC_SEAL
#  define MFD_NOEXEC_SEAL 0x0008U
# endif
  static unsigned int memfd_create_flags = MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL;
  /* name is only for debugging, collisions don't matter */
  randname (name + strlen (name) - 6);
  fd = memfd_create (name, memfd_create_flags);
  if (fd < 0 && errno == EINVAL) {
    memfd_create_flags &= ~MFD_NOEXEC_SEAL;
    g_warning ("memfd_create failed, retrying without MFD_NOEXEC_SEAL");
    fd = memfd_create (name, memfd_create_flags);
  }
  if (fd >= 0) {
    fcntl (fd, F_ADD_SEALS, F_SEAL_SHRINK);
    return fd;
  }
#else
  int retries = 100;

#ifdef __linux__
  g_warning_once ("Falling back to shm_open for shared memory buffers.");
#endif
  do {
    randname (name + strlen (name) - 6);
    --retries;
    /* shm_open guarantees that O_CLOEXEC is set */
    fd = shm_open (name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink (name);
      return fd;
    }
  } while (retries > 0 && errno == EEXIST);
#endif

  return -1;
}

/**
 * phosh_create_shm_file:
 * @size: The file size
 *
 * Create a shared memory file of the given size.
 *
 * Returns: a file descriptor on success or -1 on error.
 */
int
phosh_create_shm_file (off_t size)
{
  int fd = anonymous_shm_open ();
  if (fd < 0) {
    return fd;
  }

  if (ftruncate (fd, size) < 0) {
    close (fd);
    return -1;
  }

  return fd;
}


/**
 * phosh_util_escape_markup:
 * @markup: The markup to escape
 * @allow_markup: Whether to allow certain markup
 *
 * Escapes the given markup either fully or (when @allow_markup is %TRUE) in a way
 * that is suitable for a notification body
 *
 * Returns: (transfer full): The escaped text
 */
char *
phosh_util_escape_markup (const char *markup, gboolean allow_markup)
{
  if (allow_markup) {
    g_autoptr (GError) err = NULL;
    g_autoptr (GRegex) amp_re = NULL;
    g_autoptr (GRegex) elem_re = NULL;
    g_autofree char *amp_esc = NULL;
    g_autofree char *escaped = NULL;

    /* Escape &whatever; */
    /* Support &amp;, &quot;, &apos;, &lt; and &gt;, escape all other occurrences of '&'. */
    amp_re = g_regex_new ("&(?!amp;|quot;|apos;|lt;|gt;)",
                          G_REGEX_DEFAULT,
                          0,
                          &err);
    if (!amp_re) {
      g_warning ("Failed to compile regex: %s", err->message);
      goto out;
    }

    amp_esc = g_regex_replace_literal (amp_re,
                                       markup, -1, 0,
                                       "&amp;",
                                       0, /* flags */
                                       &err);
    if (err) {
      g_warning ("Failed to escape string: %s", err->message);
      goto out;
    }

    /*
     * "HTML" element escape
     * Support <b>, <i>, and <u>, escape anything else
     * so it displays as raw markup.
     * https://specifications.freedesktop.org/notification-spec/latest/ar01s04.html
     */
    elem_re = g_regex_new ("<(?!/?[biu]>)",
                           G_REGEX_DEFAULT,
                           0,
                           &err);
    if (!elem_re) {
      g_warning ("Failed to compile regex: %s", err->message);
      goto out;
    }

    escaped = g_regex_replace_literal (elem_re,
                                       amp_esc, -1, 0,
                                       "&lt;",
                                       0, /* flags */
                                       &err);
    if (err) {
      g_warning ("Failed to escape string: %s", err->message);
      goto out;
    }

    if (pango_parse_markup(escaped, -1, 0, NULL, NULL, NULL, NULL))
      return g_steal_pointer (&escaped);
  }

 out:
  /*invalid markup or no markup allowed */
  return g_markup_escape_text (markup, -1);
}

/**
 * phosh_util_gesture_is_touch:
 * @gesture: The gesture
 *
 * Allow to check whether a gesture's last event was a touch press or
 * release.  This can be used to distinguish mouse and touchpad clicks
 * from touch press/release.
 *
 * Returns: %TRUE if this is a touch press or release event, otherwise %FALSE
 */
gboolean
phosh_util_gesture_is_touch (GtkGestureSingle *gesture)
{
  GdkEventSequence *seq;
  const GdkEvent *event;
  GdkDevice *device;

#define PHOSH_BUTTON_MASK (GDK_BUTTON_PRESS  |   \
                           GDK_2BUTTON_PRESS |   \
                           GDK_3BUTTON_PRESS |   \
                           GDK_BUTTON_RELEASE)

  g_return_val_if_fail (GTK_IS_GESTURE_SINGLE (gesture), FALSE);

  seq = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (gesture));
  event = gtk_gesture_get_last_event (GTK_GESTURE (gesture), seq);

  if (event == NULL)
    return FALSE;

  if ((event->type & PHOSH_BUTTON_MASK) == 0)
    return FALSE;

  device = gdk_event_get_source_device (event);

  if (device == NULL)
    return FALSE;

  if (gdk_device_get_source (device) != GDK_SOURCE_TOUCHSCREEN)
    return FALSE;

  return TRUE;

#undef PHOSH_BUTTON_MASK
}

/**
 * phosh_util_have_gnome_software:
 * @scan: Whether to scan $PATH again
 *
 * Returns: the (cached) answer if gnome-software can be found in the path.
 */
gboolean
phosh_util_have_gnome_software (gboolean scan)
{
  g_autofree char *path = NULL;

  if (have_gnome_software >= 0 && !scan)
    return have_gnome_software;

  path = g_find_program_in_path ("gnome-software");
  have_gnome_software =  !!path;
  return have_gnome_software;
}

/**
 * phosh_util_toggle_style_class:
 * @widget: Widget to change styling of
 * @style_class: The name of CSS class
 * @toggle: Whether the class should be set or unset
 *
 * Adds or removes the specified style class on the widget.
 */
void
phosh_util_toggle_style_class (GtkWidget *widget, const char *style_class, gboolean toggle)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);

  if (toggle)
    gtk_style_context_add_class (context, style_class);
  else
    gtk_style_context_remove_class (context, style_class);
}


gboolean
phosh_clear_fd (int *fd, GError **err)
{
  g_return_val_if_fail (fd, FALSE);

  return g_clear_fd (fd, err);
}

/**
 * phosh_util_get_icon_by_wifi_strength:
 * @strength: An integer representing the strength of Wi-Fi
 * @is_connecting: If we are trying to connect to Wi-Fi
 *
 * Returns: The name of the icon based on the signal strength. If @is_connecting is @TRUE, then it
 * returns a loading icon.
 */
const char *
phosh_util_get_icon_by_wifi_strength (guint strength, gboolean is_connecting)
{
  const char *icon_name;

  if (is_connecting)
    icon_name = "network-wireless-acquiring-symbolic";
  else if (strength > 80)
    icon_name = "network-wireless-signal-excellent-symbolic";
  else if (strength > 55)
    icon_name = "network-wireless-signal-good-symbolic";
  else if (strength > 30)
    icon_name = "network-wireless-signal-ok-symbolic";
  else if (strength > 5)
    icon_name = "network-wireless-signal-weak-symbolic";
  else
    icon_name = "network-wireless-signal-none-symbolic";

  return icon_name;
}

/*
 * phosh_util_file_equal:
 * @file1: the first `GFile`
 * @file2: the second `GFile`
 *
 * Like `g_file_equal` but handles `NULL` gracefully.
 *
 * Returns: `TRUE` if both files are equal, otherwise `FALSE`
 */
gboolean
phosh_util_file_equal (GFile *file1, GFile *file2)
{
  if (file1 == NULL && file2 == NULL)
    return TRUE;

  if (file1 != NULL && file2 != NULL && g_file_equal (file1, file2))
    return TRUE;

  return FALSE;
}

/**
 * phosh_util_data_uri_to_pixbuf:
 * @uri: The data URI
 * @error: A pointer to a #GError
 *
 * Converts a data URI to a #GdkPixbuf.
 *
 * Returns: (transfer full)(nullable): The decoded #GdkPixbuf or %NULL on error
 */
GdkPixbuf *
phosh_util_data_uri_to_pixbuf (const char *uri, GError **error)
{
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GdkPixbufLoader) loader = NULL;
  g_autofree char *mime_type = NULL;

  g_return_val_if_fail (uri, NULL);

  bytes = soup_uri_decode_data_uri (uri, &mime_type);
  if (!bytes) {
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to decode bytes for '%s'", uri);
    return NULL;
  }

  loader = gdk_pixbuf_loader_new ();
  if (!loader) {
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to create pixbuf loader");
    return NULL;
  }

  if (!gdk_pixbuf_loader_write_bytes (loader, bytes, error) ||
      !gdk_pixbuf_loader_close (loader, error))
    return NULL;

  pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
  if (!pixbuf)
    return NULL;

  g_object_ref (pixbuf);
  return g_steal_pointer (&pixbuf);
}


GdkPixbuf *
phosh_utils_pixbuf_scale_to_min (GdkPixbuf *src, int min_width, int min_height)
{
  double factor;
  int src_width, src_height;
  int new_width, new_height;
  GdkPixbuf *dest;

  g_return_val_if_fail (GDK_IS_PIXBUF (src), NULL);

  src_width = gdk_pixbuf_get_width (src);
  src_height = gdk_pixbuf_get_height (src);

  factor = MAX (min_width / (double) src_width, min_height / (double) src_height);

  new_width = floor (src_width * factor + 0.5);
  new_height = floor (src_height * factor + 0.5);

  dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                         gdk_pixbuf_get_has_alpha (src),
                         8, min_width, min_height);
  if (!dest)
    return NULL;

  /* crop the result */
  gdk_pixbuf_scale (src, dest,
                    0, 0,
                    min_width, min_height,
                    (new_width - min_width) / -2,
                    (new_height - min_height) / -2,
                    factor,
                    factor,
                    GDK_INTERP_BILINEAR);
  return dest;
}


static const char *(*app_attr[]) (GAppInfo *info) = {
  g_app_info_get_display_name,
  g_app_info_get_name,
  g_app_info_get_description,
  g_app_info_get_executable,
};

static const char *(*desktop_attr[]) (GDesktopAppInfo *info) = {
  g_desktop_app_info_get_generic_name,
  g_desktop_app_info_get_categories,
};

/**
 * phosh_util_matches_app_info:
 * @info: app-info to check
 * @search: Search string to use for matching
 *
 * Returns: `TRUE` if the info matches search else `FALSE`
 */
gboolean
phosh_util_matches_app_info (GAppInfo *info, const char *search)
{
  const char *str = NULL;
  for (int i = 0; i < G_N_ELEMENTS (app_attr); i++) {
    g_autofree char *folded = NULL;

    str = app_attr[i] (info);

    if (gm_str_is_null_or_empty (str))
      continue;

    folded = g_utf8_casefold (str, -1);

    if (strstr (folded, search))
      return TRUE;
  }

  if (G_IS_DESKTOP_APP_INFO (info)) {
    const char * const *kwds;

    for (int i = 0; i < G_N_ELEMENTS (desktop_attr); i++) {
      g_autofree char *folded = NULL;

      str = desktop_attr[i] (G_DESKTOP_APP_INFO (info));

      if (gm_str_is_null_or_empty (str))
        continue;

      folded = g_utf8_casefold (str, -1);

      if (strstr (folded, search))
        return TRUE;
    }

    kwds = g_desktop_app_info_get_keywords (G_DESKTOP_APP_INFO (info));

    if (kwds) {
      int i = 0;

      while ((str = kwds[i])) {
        g_autofree char *folded = g_utf8_casefold (str, -1);
        if (strstr (folded, search))
          return TRUE;
        i++;
      }
    }
  }

  return FALSE;
}

/**
 * phosh_util_append_to_strv:
 * @array: A `NULL` terminated array of strings
 * @element: The string to append
 *
 * Append `element` to an array of strings.
 *
 * Returns: (transfer full): A new `NULL` terminated array with the element appended to it.
 */
GStrv
phosh_util_append_to_strv (GStrv array, const char *element)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_strv_builder_addv (builder, (const char **) array);
  g_strv_builder_add (builder, element);
  return g_strv_builder_end (builder);
}

/**
 * phosh_util_remove_from_strv:
 * @array: A `NULL` terminated array of strings
 * @element: The string to remove
 *
 * Remove all elements from a string array that match `element`.
 *
 * Returns: (transfer full): A new `NULL` terminated array with the element removed from it.
 */
GStrv
phosh_util_remove_from_strv (GStrv array, const char *element)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  for (int i = 0; array[i] != NULL; i++) {
    if (g_strcmp0 (array[i], element) == 0)
      continue;
    g_strv_builder_add (builder, array[i]);
  }
  return g_strv_builder_end (builder);
}


static void
on_activate_action_dbus_call_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GError) err = NULL;
  g_autoptr (GVariant) result = NULL;

  result = g_dbus_proxy_call_finish (G_DBUS_PROXY (source_object), res, &err);
  if (result == NULL) {
    g_task_return_error (task, g_steal_pointer (&err));
    return;
  }

  g_task_return_boolean (task, TRUE);
}


static void
on_activate_action_dbus_proxy_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GTask) task = G_TASK (user_data);
  g_autoptr (GDBusProxy) proxy = NULL;
  g_autoptr (GError) err = NULL;
  GCancellable *cancel;
  GVariant *params;
  gboolean has_actions = FALSE;

  proxy = g_dbus_proxy_new_for_bus_finish (res, &err);
  if (proxy == NULL) {
    g_task_return_error (task, g_steal_pointer (&err));
    return;
  }

  params = g_task_get_task_data (task);
  if (g_variant_is_of_type (params,  G_VARIANT_TYPE ("(sava{sv})")))
    has_actions = TRUE;

  cancel = g_task_get_cancellable (task);
  g_dbus_proxy_call (proxy,
                     has_actions ? "ActivateAction" : "Activate",
                     params,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     cancel,
                     on_activate_action_dbus_call_ready,
                     g_steal_pointer (&task));
}



static char *
app_path_for_id (const char *app_id)
{
  char *path;
  int i;

  path = g_strconcat ("/", app_id, NULL);
  for (i = 0; path[i]; i++) {
    if (path[i] == '.')
      path[i] = '/';
    if (path[i] == '-')
      path[i] = '_';
  }

  return path;
}

/**
 * phosh_util_activate_action:
 * @info: The app info
 * @action:(nullable): The action name
 * @params:(nullable): The action parameters
 * @cancel:(nullable): A cancellable
 * @callback:(scope async): The callback to invoked
 * @user_data:(nullable): user data
 *
 * Asynchronously activate the given app corresponding to `info` and
 * invoke the given action on it. If `action` is `NULL` the app is merely
 * activated.
 */
void
phosh_util_activate_action (GAppInfo           *info,
                            const char         *action,
                            GVariant           *params,
                            GCancellable       *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
  g_autoptr (GTask) task = NULL;
  g_autoptr (GVariant) args = NULL;
  g_autofree char *app_id = NULL, *object_path = NULL;

  g_return_if_fail (G_IS_APP_INFO (info) || info == NULL);
  g_return_if_fail (!action || !gm_str_is_null_or_empty (action));
  g_return_if_fail (params == NULL || g_variant_is_of_type (params,  G_VARIANT_TYPE ("av")));

  app_id = phosh_strip_suffix_from_app_id (g_app_info_get_id (info));
  g_return_if_fail (app_id);

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_task_set_source_tag (task, phosh_util_activate_action);

  if (action) {
    if (!params)
      params = g_variant_new ("av", NULL);

    /* Sink ref so we can use `g_variant_unref` for the task's `destroy_notify` */
    args = g_variant_ref_sink (g_variant_new ("(s@av@a{sv})",
                                              action,
                                              params,
                                              phosh_util_get_platform_data (info)));
  } else {
    args = g_variant_ref_sink (g_variant_new ("(@a{sv})", phosh_util_get_platform_data (info)));
  }
  g_task_set_task_data (task, g_steal_pointer (&args), (GDestroyNotify) g_variant_unref);

  object_path = app_path_for_id (app_id);
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            app_id,
                            object_path,
                            "org.freedesktop.Application",
                            cancellable,
                            on_activate_action_dbus_proxy_ready,
                            g_steal_pointer (&task));
}

/**
 * phosh_util_activate_action_finish:
 * @info: The app info
 * @action: The action name
 * @params:(nullable): The action parameters
 * @cancel:(nullable): A cancellable
 * @callback:(scope async): The callback to invoke when the async operation finished
 * @user_data:(nullable): user data
 *
 * Completes the async operation started with `phosh_util_activate_action`.
 */
gboolean
phosh_util_activate_action_finish (GAsyncResult *res, GError **err)
{
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
  g_return_val_if_fail (err == NULL || (err != 0 && *err == NULL), FALSE);

  return g_task_propagate_boolean (G_TASK (res), err);
}

/**
 * phosh_util_open_settings_panel:
 * @panel: A settings panel name
 * @mobile: panel is in mobile settings app
 * @cancellable:(nullable): A cancellable
 * @callback:(scope async): The callback to invoke when the async operation finished
 *
 * Open the settings panel corresponding to the given name.
 */
void
phosh_util_open_settings_panel (const char         *panel,
                                gboolean            mobile,
                                GCancellable       *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer            user_data)
{
  g_autoptr (GDesktopAppInfo) info = NULL;
  const char *action;
  GVariantBuilder builder;

  if (mobile) {
    info = g_desktop_app_info_new ("mobi.phosh.MobileSettings.desktop");
    action = "set-panel";
  } else {
    info = g_desktop_app_info_new ("org.gnome.Settings.desktop");
    action = "launch-panel";
  }

  g_variant_builder_init_static (&builder, G_VARIANT_TYPE ("av"));
  g_variant_builder_add (&builder, "v", g_variant_new ("(sav)", panel, NULL));
  phosh_util_activate_action (G_APP_INFO (info),
                              action,
                              g_variant_builder_end (&builder),
                              cancellable,
                              callback,
                              user_data);
}


gboolean
phosh_util_open_settings_panel_finish (GAsyncResult *res, GError **err)
{
  return phosh_util_activate_action_finish (res, err);
}


#define MINIMUM_SCALE_FACTOR 1.0f
#define MAXIMUM_SCALE_FACTOR 4.0f
#define SCALE_FACTORS_PER_INTEGER 4
#define SCALE_FACTORS_STEPS (1.0 / (float) SCALE_FACTORS_PER_INTEGER)
#define MINIMUM_LOGICAL_AREA_LANDSCAPE (800 * 480)
#define MINIMUM_LOGICAL_AREA_PORTRAIT (360 * 720)

static gboolean
is_logical_size_large_enough (float width, float height)
{
  int area = (width > height) ? MINIMUM_LOGICAL_AREA_LANDSCAPE : MINIMUM_LOGICAL_AREA_PORTRAIT;

  return width * height >= area;
}


static gboolean
is_valid_scale (float width, float height, float scale)
{
  float scaled_h = height / scale;
  float scaled_w = width / scale;

  if (scale < MINIMUM_SCALE_FACTOR || scale > MAXIMUM_SCALE_FACTOR)
    return FALSE;

  return is_logical_size_large_enough (floorf (scaled_w), floorf(scaled_h));
}

static float
get_closest_scale_factor_for_resolution (float width, float height, float scale, float threshold)
{
  unsigned int i;
  float scaled_h;
  float scaled_w;
  float best_scale;
  float base_scaled_w;
  gboolean found_one;

  best_scale = 0;

  if (!is_valid_scale (width, height, scale))
    return 0.0;

  if (G_APPROX_VALUE (fmodf (width, scale), 0.0, FLT_EPSILON) &&
      G_APPROX_VALUE (fmodf (height, scale), 0.0, FLT_EPSILON))
    return scale;

  i = 0;
  found_one = FALSE;
  base_scaled_w = floorf (width / scale);

  do {
    for (int j = 0; j < 2; j++) {
      float current_scale;
      int offset = i * (j ? 1 : -1);

      scaled_w = base_scaled_w + offset;
      current_scale = width / scaled_w;
      scaled_h = height / current_scale;

      if (current_scale >= scale + threshold ||
          current_scale <= scale - threshold ||
          current_scale < MINIMUM_SCALE_FACTOR ||
          current_scale > MAXIMUM_SCALE_FACTOR) {
        return best_scale;
      }

      if (G_APPROX_VALUE (floorf (scaled_h), scaled_h, FLT_EPSILON)) {
        found_one = TRUE;

        if (fabsf (current_scale - scale) < fabsf (best_scale - scale))
          best_scale = current_scale;
      }
    }
    i++;
  } while (!found_one);

  return best_scale;
}

/**
 * phosh_util_calculate_supported_mode_scales:
 * @width: The mode's width
 * @height: The modes' height
 * @n_supported_scales:(out): The number of calculated scales
 * @fractional: Whether to include fractional scales
 *
 * Calculate supported scales for a given mode.
 */
float *
phosh_util_calculate_supported_mode_scales (guint32   width,
                                            guint32   height,
                                            int      *n_supported_scales,
                                            gboolean  fractional)
{
  GArray *supported_scales;

  supported_scales = g_array_new (FALSE, FALSE, sizeof (float));

  if (fractional) {
    for (float i = floorf (MINIMUM_SCALE_FACTOR); i <= ceilf (MAXIMUM_SCALE_FACTOR); i++) {
      float max_bound;

      if (G_APPROX_VALUE (floorf (MINIMUM_SCALE_FACTOR), i, FLT_EPSILON) ||
          G_APPROX_VALUE (ceilf (MAXIMUM_SCALE_FACTOR), i, FLT_EPSILON)) {
        max_bound = SCALE_FACTORS_STEPS;
      } else {
        max_bound = SCALE_FACTORS_STEPS / 2.0;
      }

      for (int j = 0; j < SCALE_FACTORS_PER_INTEGER; j++) {
        float scale;
        float scale_value = i + j * SCALE_FACTORS_STEPS;

        scale = get_closest_scale_factor_for_resolution (width, height,
                                                         scale_value,
                                                         max_bound);
        if (scale > 0.0)
          g_array_append_val (supported_scales, scale);
      }
    }
  } else {
    for (float f = floorf (MINIMUM_SCALE_FACTOR); f <= ceilf (MAXIMUM_SCALE_FACTOR); f++) {
      if (is_valid_scale (width, height, f)) {
        g_array_append_val (supported_scales, f);
      }
    }
  }

  if (supported_scales->len == 0) {
    float fallback_scale = 1.0;
    g_array_append_val (supported_scales, fallback_scale);
  }

  *n_supported_scales = supported_scales->len;
  return (float *) g_array_free (supported_scales, FALSE);
}

/**
 * phosh_util_get_platform_data:
 * @info: An app info
 *
 * Get platform data to pass to DBus invocation
 *
 * Returns: the platform data
 */
GVariant *
phosh_util_get_platform_data (GAppInfo *info)
{
  g_autoptr (GdkAppLaunchContext) context = NULL;
  GdkDisplay *display = gdk_display_get_default ();
  GVariantBuilder builder;
  char *startup_id;

  g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);
  context = gdk_display_get_app_launch_context (display);
  startup_id = g_app_launch_context_get_startup_notify_id (G_APP_LAUNCH_CONTEXT (context),
                                                           info,
                                                           NULL);
  if (!startup_id)
    return g_variant_builder_end (&builder);

  g_variant_builder_add (&builder, "{sv}",
                         "desktop-startup-id", g_variant_new_string (startup_id));
  g_variant_builder_add (&builder, "{sv}",
                         "activation-token", g_variant_new_take_string (startup_id));

  return g_variant_builder_end (&builder);
}
