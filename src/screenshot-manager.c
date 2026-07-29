/*
 * Copyright (C) 2021-2022 Purism SPC
 *               2023-2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-screenshot-manager"

#include "phosh-config.h"
#include "fader.h"
#include "phosh-wayland.h"
#include "notifications/notify-manager.h"
#include "screenshot-manager.h"
#include "shell-priv.h"
#include "util.h"
#include "wl-buffer.h"

#include "dbus/phosh-screenshot-dbus.h"

#include <gmobile.h>

#include <gio/gunixinputstream.h>

#define BUS_NAME "org.gnome.Shell.Screenshot"
#define OBJECT_PATH "/org/gnome/Shell/Screenshot"

#define KEYBINDINGS_SCHEMA_ID "org.gnome.shell.keybindings"
#define KEYBINDING_KEY_SCREENSHOT "screenshot"

#define FLASH_FADER_TIMEOUT 500

/**
 * PhoshScreenshotManager:
 *
 * Screenshot interaction
 *
 * The #PhoshScreenshotManager is responsible for
 * taking screenshots.
 */

static void phosh_screenshot_manager_screenshot_iface_init (
  PhoshDBusScreenshotIface *iface);

typedef enum {
  FRAME_STATE_FAILURE = -1,
  FRAME_STATE_UNKNOWN = 0,
  FRAME_STATE_SUCCESS = 1,
} ScreencopyFrameState;

typedef struct _ScreencopyFrame {
  struct zwlr_screencopy_frame_v1 *frame;
  uint32_t                         flags;
  PhoshWlBuffer                   *buffer;
  GdkPixbuf                       *pixbuf;
  PhoshMonitor                    *monitor;
  ScreencopyFrameState             state;
  PhoshScreenshotManager          *manager;
} ScreencopyFrame;

typedef struct {
  GList                    *frames;
  GDBusMethodInvocation    *invocation;
  gboolean                  flash;
  char                     *filename;
  guint                     num_outputs;
  float                     max_scale;
  GdkRectangle             *area;
  gboolean                  copy_to_clipboard;
} ScreencopyFrames;

typedef struct {
  guint                   child_watch_id;
  GPid                    pid;
  GDBusMethodInvocation  *invocation;
  GInputStream           *stdout_;
  GCancellable           *cancel;
  char                    read_buf[64];
  GString                *response;
} SlurpArea;


typedef struct _PhoshScreenshotManager {
  PhoshDBusScreenshotSkeleton        parent;

  int                                dbus_name_id;
  struct zwlr_screencopy_manager_v1 *wl_scm;
  ScreencopyFrames                  *frames;
  SlurpArea                         *slurp;

  PhoshFader                        *fader;
  guint                              fader_id;
  PhoshFader                        *opaque;
  guint                              opaque_id;

  GdkPixbuf                         *for_clipboard;

  GStrv                              action_names;
  GSettings                         *settings;

  GCancellable                      *cancel;
} PhoshScreenshotManager;


G_DEFINE_TYPE_WITH_CODE (PhoshScreenshotManager,
                         phosh_screenshot_manager,
                         PHOSH_DBUS_TYPE_SCREENSHOT_SKELETON,
                         G_IMPLEMENT_INTERFACE (
                           PHOSH_DBUS_TYPE_SCREENSHOT,
                           phosh_screenshot_manager_screenshot_iface_init));


static void
slurp_area_dispose (SlurpArea *slurp)
{
  if (slurp->response) {
    g_string_free (slurp->response, TRUE);
    slurp->response = NULL;
  }
  g_cancellable_cancel (slurp->cancel);
  g_clear_object (&slurp->cancel);
  g_clear_object (&slurp->stdout_);
}


static void
screencopy_frame_dispose (ScreencopyFrame *frame)
{
  g_clear_pointer (&frame->buffer, phosh_wl_buffer_destroy);
  g_clear_pointer (&frame->frame, zwlr_screencopy_frame_v1_destroy);
  g_clear_object (&frame->pixbuf);

  if (frame->monitor) {
    g_object_remove_weak_pointer (G_OBJECT (frame->monitor), (gpointer)&frame->monitor);
    frame->monitor = NULL;
  }

  g_free (frame);
}


static void
screencopy_frames_dispose (ScreencopyFrames *frames)
{
  g_clear_pointer (&frames->area, g_free);
  g_clear_list (&frames->frames, (GDestroyNotify) screencopy_frame_dispose);
  g_free (frames->filename);
  g_free (frames);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (ScreencopyFrames, screencopy_frames_dispose);


static void
screencopy_frame_handle_buffer (void                            *data,
                                struct zwlr_screencopy_frame_v1 *frame,
                                uint32_t                         format,
                                uint32_t                         width,
                                uint32_t                         height,
                                uint32_t                         stride)
{
  ScreencopyFrame *screencopy_frame = data;

  g_debug ("Handling buffer %dx%d for %s", width, height, screencopy_frame->monitor->name);
  screencopy_frame->buffer = phosh_wl_buffer_new (format, width, height, stride);
  g_return_if_fail (screencopy_frame->buffer);

  zwlr_screencopy_frame_v1_copy (frame, screencopy_frame->buffer->wl_buffer);
}


static void
screencopy_frame_handle_flags (void                            *data,
                               struct zwlr_screencopy_frame_v1 *frame,
                               uint32_t                         flags)
{
  ScreencopyFrame *screencopy_frame = data;

  g_return_if_fail (screencopy_frame);

  screencopy_frame->flags = flags;
}

static gboolean
on_fader_timeout (gpointer user_data)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (user_data);

  g_clear_pointer (&self->fader, phosh_cp_widget_destroy);

  self->fader_id = 0;
  return G_SOURCE_REMOVE;
}


static void
show_fader (PhoshScreenshotManager *self)
{
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (phosh_shell_get_default ());

  self->fader_id = g_timeout_add (FLASH_FADER_TIMEOUT, on_fader_timeout, self);
  g_source_set_name_by_id (self->fader_id, "[phosh] screenshot fader");
  self->fader = g_object_new (PHOSH_TYPE_FADER,
                              "monitor", monitor,
                              "style-class", "phosh-fader-flash-fade",
                              NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->fader), TRUE);
}


static void
screenshot_done (PhoshScreenshotManager *self, gboolean success)
{
  /* Invocation via DBus API */
  if (self->frames->invocation) {
    phosh_dbus_screenshot_complete_screenshot (PHOSH_DBUS_SCREENSHOT (self),
                                               self->frames->invocation,
                                               success,
                                               self->frames->filename ?: "");
    g_clear_pointer (&self->frames, screencopy_frames_dispose);
    return;
  }

  /* Internal screenshot */
  if (self->frames->filename) {
    PhoshNotifyManager *nm = phosh_notify_manager_get_default ();
    g_autoptr (GIcon) icon = g_themed_icon_new ("screenshot-portrait-symbolic");
    g_autoptr (PhoshNotification) noti = NULL;
    g_autofree char *msg = NULL;

    if (success) {
      g_autofree char *filename = NULL;

      filename = g_path_get_basename (self->frames->filename);
      /* Translators: '%s' is the filename of a screenshot */
      msg = g_strdup_printf (_("Screenshot saved to '%s'"), filename);
    } else {
      msg = g_strdup (_("Failed to save screenshot"));
    }

    noti = g_object_new (PHOSH_TYPE_NOTIFICATION,
                         "summary", _("Screenshot"),
                         "body", msg,
                         "image", icon,
                         NULL);

    phosh_notify_manager_add_shell_notification (nm, noti, 0, 5000);
    g_clear_pointer (&self->frames->filename, g_free);
  }

  if (!self->frames->filename && !self->frames->copy_to_clipboard)
    g_clear_pointer (&self->frames, screencopy_frames_dispose);
}


static void
update_recent_files (PhoshScreenshotManager *self)
{
  g_autofree char *recent = g_build_filename (g_get_user_data_dir (), "recently-used.xbel", NULL);
  g_autofree char *uri = NULL;
  g_autoptr (GBookmarkFile) bookmarks = g_bookmark_file_new ();
  g_autoptr (GError) err = NULL;

  g_return_if_fail (self->frames->filename);

  if (!g_bookmark_file_load_from_file (bookmarks, recent, &err)) {
    if (!g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_warning ("Failed to open bookarks %s: %s", recent, err->message);
      return;
    }
  }

  uri = g_filename_to_uri (self->frames->filename, NULL, &err);
  if (!uri) {
    g_warning ("Failed to create bookmark uri for '%s': %s", uri, err->message);
    return;
  }
  g_bookmark_file_add_application (bookmarks, uri, "Phosh", "gio open %u");

  if (!g_bookmark_file_to_file (bookmarks, recent, &err)) {
    g_warning ("Failed to save bookmarks %s: %s", recent, err->message);
    return;
  }
}


static char *
phosh_screenshot_manager_build_thumbnail_path (const char *uri)
{
  g_autoptr (GChecksum) checksum = g_checksum_new (G_CHECKSUM_MD5);
  guint8 digest[16];
  gsize digest_len = sizeof (digest);
  g_autofree char *name = NULL, *path = NULL;

  g_checksum_update (checksum, (const guchar *) uri, strlen (uri));
  g_checksum_get_digest (checksum, digest, &digest_len);
  g_assert (digest_len == 16);

  name = g_strconcat (g_checksum_get_string (checksum), ".png", NULL);
  path = g_build_filename (g_get_user_cache_dir (),
                           "thumbnails",
                           "normal",
                           name,
                           NULL);

  return g_steal_pointer (&path);
}


static void
on_save_thumbnail_ready (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;

  success = gdk_pixbuf_save_to_stream_finish (res, &err);
  if (!success)
    g_warning ("Failed to save thumbnail: %s", err->message);
}


#define THUMBNAIL_SIZE 128

static void
phosh_screenshot_manager_save_thumbnail (PhoshScreenshotManager *self,
                                         const char             *filename,
                                         GdkPixbuf              *pixbuf)
{
  int width, height;
  double scale;
  g_autoptr (GdkPixbuf) scaled = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree char *thumbnail_name = NULL, *dirname = NULL, *uri = NULL;
  g_autofree char *mtime_str = NULL, *width_str = NULL, *height_str = NULL;
  g_autoptr (GDateTime) now = NULL;

  uri = g_filename_to_uri (filename, NULL, &err);
  if (!uri) {
    g_warning ("Failed to create thumbnail name for '%s': %s", filename, err->message);
    return;
  }

  thumbnail_name = phosh_screenshot_manager_build_thumbnail_path (uri);
  dirname = g_path_get_dirname (thumbnail_name);
  if (g_mkdir_with_parents (dirname, 0700) != 0) {
    g_warning ("Failed to create thumbnail folder '%s'", dirname);
    return;
  }

  file = g_file_new_for_path (thumbnail_name);
  stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &err);
  if (!stream) {
    g_warning ("Failed to create thumbnail file %s: %s", thumbnail_name, err->message);
    return;
  }

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  scale = (double)THUMBNAIL_SIZE / MAX (width, height);
  scaled = gdk_pixbuf_scale_simple (pixbuf,
                                    floor (width * scale + 0.5),
                                    floor (height * scale + 0.5),
                                    GDK_INTERP_BILINEAR);
  gdk_pixbuf_copy_options (pixbuf, scaled);

  now = g_date_time_new_now_local();
  mtime_str = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) g_date_time_to_unix (now));
  width_str = g_strdup_printf ("%d", width);
  height_str = g_strdup_printf ("%d", height);
  gdk_pixbuf_save_to_stream_async (scaled,
                                   G_OUTPUT_STREAM (stream),
                                   "png",
                                   NULL,
                                   on_save_thumbnail_ready,
                                   NULL,
                                   "tEXt::Thumb::Image::Width", width_str,
                                   "tEXt::Thumb::Image::Height", height_str,
                                   "tEXt::Thumb::URI", uri,
                                   "tEXt::Thumb::MTime", mtime_str,
                                   "tEXt::Software", "Phosh::Shell",
                                   NULL);
}


static void
on_opaque_timeout (gpointer data)
{
  PhoshScreenshotManager *self = data;
  GdkDisplay *display = gdk_display_get_default ();
  GtkClipboard *clipboard;

  if (!display) {
    g_critical ("Couldn't get GDK display");
    goto out;
  }

  clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_image (clipboard, self->for_clipboard);
  g_debug ("Updated clipboard");
  self->frames->copy_to_clipboard = FALSE;
  screenshot_done (self, TRUE);

 out:
  g_clear_object (&self->for_clipboard);
  g_clear_pointer (&self->opaque, phosh_cp_widget_destroy);
  self->opaque_id = 0;
}


static void
copy_to_clipboard (PhoshScreenshotManager *self, GdkPixbuf *pixbuf)
{
  PhoshMonitor *monitor = phosh_shell_get_primary_monitor (phosh_shell_get_default ());

  /* The Wayland clipboard only works if we have focus so use a fully opaque surface */
  self->opaque = g_object_new (PHOSH_TYPE_FADER,
                               "monitor", monitor,
                               "style-class", "phosh-fader-screenshot-opaque",
                               "kbd-interactivity", TRUE,
                               NULL);
  self->for_clipboard = g_object_ref (pixbuf);
  /* FIXME: Would be better to trigger when the opaque window is up and got
     input focus but all such attempts failed */
  self->opaque_id = g_timeout_add_seconds_once (1, on_opaque_timeout, self);
  g_source_set_name_by_id (self->opaque_id, "[phosh] screenshot opaque");

  gtk_widget_set_visible (GTK_WIDGET (self->opaque), TRUE);
}


static void
on_save_pixbuf_ready (GObject      *source_object,
                      GAsyncResult *res,
                      gpointer      user_data)
{
  gboolean success;
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshScreenshotManager) self = PHOSH_SCREENSHOT_MANAGER (user_data);

  g_return_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (self));
  g_return_if_fail (self->frames->filename);

  success = gdk_pixbuf_save_to_stream_finish (res, &err);
  if (!success) {
    g_warning ("Failed to save screenshot: %s", err->message);
    screenshot_done (self, FALSE);
    return;
  }

  if (!self->frames->invocation)
    update_recent_files (self);

  phosh_screenshot_manager_save_thumbnail (self,
                                           self->frames->filename,
                                           GDK_PIXBUF (source_object));

  if (self->frames->copy_to_clipboard)
    copy_to_clipboard (self, GDK_PIXBUF (source_object));
  else
    screenshot_done (self, success);
}


/* Taken from grim */
static GdkRectangle
get_output_layout (PhoshScreenshotManager *self)
{
  GdkRectangle box;
  guint x1 = G_MAXUINT, y1 = G_MAXUINT, x2 = 0, y2 = 0;

  for (GList *l = self->frames->frames; l; l = l->next) {
    ScreencopyFrame *frame = l->data;
    PhoshMonitor *monitor = PHOSH_MONITOR (frame->monitor);

    if (monitor->logical.x < x1)
      x1 = monitor->logical.x;

    if (monitor->logical.y < y1)
      y1 = monitor->logical.y;

    if (monitor->logical.x + monitor->logical.width > x2)
      x2 = monitor->logical.x + monitor->logical.width;

    if (monitor->logical.y + monitor->logical.height > y2)
      y2 = monitor->logical.y + monitor->logical.height;
  }

  box.x = x1;
  box.y = y1;
  box.width = x2 - x1;
  box.height = y2 - y1;

  return box;
}


static guint
get_angle (PhoshMonitorTransform transform)
{
  switch (transform) {
  case PHOSH_MONITOR_TRANSFORM_FLIPPED:
  case PHOSH_MONITOR_TRANSFORM_NORMAL:
    return 0;
  case PHOSH_MONITOR_TRANSFORM_FLIPPED_90:
  case PHOSH_MONITOR_TRANSFORM_90:
    return 270;
  case PHOSH_MONITOR_TRANSFORM_FLIPPED_180:
  case PHOSH_MONITOR_TRANSFORM_180:
    return 180;
  case PHOSH_MONITOR_TRANSFORM_FLIPPED_270:
  case PHOSH_MONITOR_TRANSFORM_270:
    return 90;
  default:
    g_return_val_if_reached (0);
  }
}

/**
 * create_internal_file:
 * @self: The screenshot manager
 * @err: An error location
 *
 * Create a file for saving the screenshot. This is used when the the
 * shell takes the screenshot e.g. via keybinding. See
 * `build_dbus_filename` for the DBus case.
 *
 * Returns: An output stream that writes to the created file
 */
static GFileOutputStream *
create_internal_file (PhoshScreenshotManager *self, GError **err)

{
  g_autoptr (GFile) dir = NULL;
  g_autoptr (GDateTime) dt = NULL;
  g_autofree char *dirname = NULL;
  g_autofree char *timestamp = NULL;
  const char *base_dir;

  g_assert (PHOSH_IS_SCREENSHOT_MANAGER (self));
  g_assert (err && *err == NULL);

  base_dir = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
  if (!base_dir)
    base_dir = g_get_home_dir ();

  /* Translators: name of the folder beneath ~/Pictures used to store screenshots */
  dirname = g_build_filename (base_dir, _("Screenshots"), NULL);
  dir = g_file_new_for_path (dirname);
  if (!g_file_make_directory_with_parents (dir, NULL, err)) {
    if (!g_error_matches (*err, G_IO_ERROR, G_IO_ERROR_EXISTS))
      return NULL;
  }

  dt = g_date_time_new_now_local ();
  timestamp = g_date_time_format (dt, "%Y-%m-%d %H:%M:%S");

  for (int i = 0; i < 100; i++) {
    g_autofree char *suffix = i ? g_strdup_printf ("-%d", i) : g_strdup ("");
    g_autofree char *filename = NULL;
    g_autofree char *path = NULL;
    g_autoptr (GFile) file = NULL;
    g_autoptr (GFileOutputStream) stream = NULL;

    /* Translators: Name of a screenshot file. The first '%s' is a timestamp
     * like "2017-05-21 12-24-03" the 2nd '%s' is a possible suffix in case
     * the file already exists like '-3' */
    filename = g_strdup_printf (_("Screenshot from %s%s.png"), timestamp, suffix);
    path = g_build_filename (dirname, filename, NULL);
    file = g_file_new_for_path (path);
    stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, err);
    if (stream) {
      g_debug ("Saving screenshot to '%s'", path);
      self->frames->filename = g_strdup (path);
      return g_steal_pointer (&stream);
    }
  }

  g_warning ("Failed to build screenshot filename in '%s'", dirname);
  return NULL;
}

/* Got all pixbufs, prepare result */
static void
submit_screenshot (PhoshScreenshotManager *self)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  GdkRectangle box;
  float screenshot_scale = self->frames->max_scale;

  box = get_output_layout (self);
  g_debug ("Screenshot of %d,%d %dx%d", box.x, box.y, box.width, box.height);

  pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                           TRUE,
                           8,
                           box.width * screenshot_scale,
                           box.height * screenshot_scale);

  /* TODO: Using cairo would avoid lots of copies */
  for (GList *l = self->frames->frames; l; l = l->next) {
    ScreencopyFrame *frame = l->data;
    float scale;
    /* how much this monitor gets enlarged based on its scale, >= 1.0 */
    double zoom;
    g_autoptr (GdkPixbuf) transformed = NULL;

    if (frame->monitor == NULL)
      continue;

    scale = phosh_monitor_get_fractional_scale (frame->monitor);
    zoom = screenshot_scale / scale;
    g_debug ("Screenshot of '%s' of %d,%d %dx%d, scale: %f",
             frame->monitor->name,
             frame->monitor->logical.x - box.x,
             frame->monitor->logical.y - box.y,
             frame->monitor->logical.width,
             frame->monitor->logical.height,
             scale);

    /* TODO: handle flips */
    transformed = gdk_pixbuf_rotate_simple (frame->pixbuf,
                                            get_angle (frame->monitor->transform));
    gdk_pixbuf_composite (transformed,
                          pixbuf,
                          (frame->monitor->logical.x - box.x) * screenshot_scale,
                          (frame->monitor->logical.y - box.y) * screenshot_scale,
                          frame->monitor->logical.width * screenshot_scale,
                          frame->monitor->logical.height * screenshot_scale,
                          (frame->monitor->logical.x - box.x) * screenshot_scale,
                          (frame->monitor->logical.y - box.y) * screenshot_scale,
                          zoom, zoom,
                          GDK_INTERP_BILINEAR,
                          255);
  }

  if (self->frames->area) {
    g_autoptr (GdkPixbuf) tmp = pixbuf;

    pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                             TRUE,
                             8,
                             self->frames->area->width * screenshot_scale,
                             self->frames->area->height * screenshot_scale);
    gdk_pixbuf_copy_area (tmp,
                          (self->frames->area->x - box.x) * screenshot_scale,
                          (self->frames->area->y - box.y) * screenshot_scale,
                          self->frames->area->width * screenshot_scale,
                          self->frames->area->height * screenshot_scale,
                          pixbuf,
                          0,
                          0);
  }

  if (self->frames->filename) {
    file = g_file_new_for_path (self->frames->filename);
    stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &err);
    if (!stream) {
      g_warning ("Failed to create screenshot %s: %s", self->frames->filename, err->message);
      screenshot_done (self, FALSE);
      return;
    }
  } else if (!self->frames->invocation) {
    /* Generate filename for internal screenshot */
    stream = create_internal_file (self, &err);
    if (!stream) {
      g_warning ("Failed to create screenshot: %s", err->message);
      screenshot_done (self, FALSE);
      return;
    }
  }

  if (stream) {
    /* on_save_to_pixbuf will trigger copy_to_clipboard if needed */
    gdk_pixbuf_save_to_stream_async (pixbuf,
                                     G_OUTPUT_STREAM (stream),
                                     "png",
                                     self->cancel,
                                     on_save_pixbuf_ready,
                                     g_object_ref (self),
                                     NULL);
  } else if (self->frames->copy_to_clipboard) {
    /* Copy to clipboard only */
    copy_to_clipboard (self, pixbuf);
  }

  if (self->frames->flash) {
    phosh_trigger_feedback ("screen-capture", FALSE);
    show_fader (self);
  }
}


static void
maybe_screencopy_done (PhoshScreenshotManager *self)
{
  guint failed = 0, done = 0;

  for (GList *l = self->frames->frames; l; l = l->next) {
    ScreencopyFrame *frame = l->data;

    switch (frame->state) {
    case FRAME_STATE_UNKNOWN:
      return;
    case FRAME_STATE_FAILURE:
      failed++;
      break;
    case FRAME_STATE_SUCCESS:
      done++;
      break;
    default:
      g_warn_if_reached ();
      break;
    }
  }

  /* Wait for all screencopies until we start merging all outputs */
  if (done + failed != self->frames->num_outputs)
    return;

  /* With a failure no need to merge pixbufs */
  if (failed) {
    phosh_dbus_screenshot_complete_screenshot (PHOSH_DBUS_SCREENSHOT (self),
                                               self->frames->invocation,
                                               FALSE,
                                               self->frames->filename ?: "");
    return;
  }

  submit_screenshot (self);
}


static void
screencopy_frame_handle_ready (void                            *data,
                               struct zwlr_screencopy_frame_v1 *frame,
                               uint32_t                         tv_sec_hi,
                               uint32_t                         tv_sec_lo,
                               uint32_t                         tv_nsec)
{
  ScreencopyFrame *screencopy_frame = data;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GBytes) bytes = NULL;

  if (screencopy_frame->monitor == NULL) {
    g_warning ("Output went away during screenshot");
    screencopy_frame->state = FRAME_STATE_FAILURE;
    goto out;
  }

  g_debug ("Frame %p %dx%d, stride %d, format 0x%x for  %s ready",
           frame,
           screencopy_frame->buffer->width,
           screencopy_frame->buffer->height,
           screencopy_frame->buffer->stride,
           screencopy_frame->buffer->format,
           screencopy_frame->monitor->name);

  switch ((uint32_t) screencopy_frame->buffer->format) {
  case WL_SHM_FORMAT_ABGR8888:
  case WL_SHM_FORMAT_XBGR8888:
    break;
  case WL_SHM_FORMAT_ARGB8888:
  case WL_SHM_FORMAT_XRGB8888: { /* ARGB -> ABGR */
    PhoshWlBuffer *buffer = screencopy_frame->buffer;
    uint8_t *d = buffer->data;
    for (int i = 0; i < buffer->height; ++i) {
      for (int j = 0; j < buffer->width; ++j) {
        uint32_t *px = (uint32_t *)(d + i * buffer->stride + j * 4);
        uint8_t a = (*px >> 24) & 0xFF;
        uint8_t b = (*px >> 16) & 0xFF;
        uint8_t g = (*px >> 8) & 0xFF;
        uint8_t r = *px & 0xFF;
        *px = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    if (buffer->format == WL_SHM_FORMAT_ARGB8888)
      buffer->format = WL_SHM_FORMAT_ABGR8888;
    else
      buffer->format = WL_SHM_FORMAT_XBGR8888;
  }
  break;
  default:
    g_warning ("Unknown buffer formeat 0x%x on %s",
               screencopy_frame->buffer->format,
               screencopy_frame->monitor->name);
    screencopy_frame->state = FRAME_STATE_FAILURE;
    goto out;
  }

  bytes = phosh_wl_buffer_get_bytes (screencopy_frame->buffer);
  pixbuf = gdk_pixbuf_new_from_bytes (bytes,
                                      GDK_COLORSPACE_RGB,
                                      TRUE,
                                      8,
                                      screencopy_frame->buffer->width,
                                      screencopy_frame->buffer->height,
                                      screencopy_frame->buffer->stride);
  if (screencopy_frame->flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) {
    GdkPixbuf *tmp = gdk_pixbuf_flip (pixbuf, FALSE);
    g_object_unref (pixbuf);
    pixbuf = tmp;
  }

  screencopy_frame->pixbuf = g_steal_pointer (&pixbuf);
  screencopy_frame->state = FRAME_STATE_SUCCESS;

 out:
  maybe_screencopy_done (screencopy_frame->manager);
}


static void
screencopy_frame_handle_failed (void                            *data,
                                struct zwlr_screencopy_frame_v1 *frame)
{
  ScreencopyFrame *screencopy_frame = data;
  const char *name = screencopy_frame->monitor ? screencopy_frame->monitor->name : "<unknown>";

  screencopy_frame->state = FRAME_STATE_FAILURE;
  g_warning ("Failed to copy output '%s'\n", name);
  maybe_screencopy_done (screencopy_frame->manager);
}


static const struct zwlr_screencopy_frame_v1_listener screencopy_frame_listener = {
  .buffer = screencopy_frame_handle_buffer,
  .flags = screencopy_frame_handle_flags,
  .ready = screencopy_frame_handle_ready,
  .failed = screencopy_frame_handle_failed,
};


/**
 * build_dbus_filename:
 * @pattern: Absolute path or relative name without extension
 *
 * Builds an absolute filename based on the given input pattern.
 * Returns: The target filename or %NULL on errors.
 */
static char *
build_dbus_filename (const char *pattern)
{
  g_autofree char *filename = NULL;

  if (gm_str_is_null_or_empty (pattern))
    return NULL;

  if (g_path_is_absolute (pattern)) {
    return g_strdup (pattern);
  } else {
    const char *dir = NULL;
    const char *const *dirs = (const char * []) {
      g_get_user_special_dir (G_USER_DIRECTORY_PICTURES),
      g_get_home_dir (),
      NULL
    };

    for (int i = 0; i < g_strv_length ((GStrv) dirs); i++) {
      if (g_file_test (dirs[i], G_FILE_TEST_EXISTS)) {
        dir = dirs[i];
        break;
      }
    }

    if (!dir)
      return NULL;

    filename = g_build_filename (dir, pattern, NULL);
  }
  if (!g_str_has_suffix (filename, ".png"))
    filename = g_strdup_printf ("%s.png", filename);

  return g_steal_pointer (&filename);
}

/**
 * phosh_screenshot_manager_do_screenshot:
 * @self: The screenshot manager
 * @area: (nullable): The area to capture or %NULL to capture all outputs
 * @include_cursor: Whether to include the cursor
 *
 * Initiate a screenshot of all outputs or the given area.
 *
 * Returns: `FALSE` on failure, otherwise `TRUE`
 */
static gboolean
phosh_screenshot_manager_do_screenshot (PhoshScreenshotManager *self,
                                        const GdkRectangle     *area,
                                        gboolean                include_cursor)
{
  g_autoptr (ScreencopyFrames) frames = NULL;
  PhoshMonitorManager *monitor_manager;
  PhoshWayland *wl = phosh_wayland_get_default ();
  float max_scale = 0.0;
  int num_outputs = 0;

  monitor_manager = phosh_shell_get_monitor_manager (phosh_shell_get_default ());
  g_return_val_if_fail (PHOSH_IS_MONITOR_MANAGER (monitor_manager), -EPERM);
  g_return_val_if_fail (PHOSH_IS_WAYLAND (wl), -EPERM);

  if (self->wl_scm == NULL) {
    g_debug ("No screenshot support");
    return FALSE;
  }

  if (self->frames) {
    g_debug ("Screenshot already in progress");
    return FALSE;
  }

  frames = g_new0 (ScreencopyFrames, 1);
  frames->flash = TRUE;

  /* Determine which monitors are involved in the area we want to screenshot. */
  for (int i = 0; i < phosh_monitor_manager_get_num_monitors (monitor_manager); i++) {
    PhoshMonitor *monitor = phosh_monitor_manager_get_monitor (monitor_manager, i);
    ScreencopyFrame *screencopy_frame;
    GdkRectangle monitor_area;
    float monitor_scale;

    if (monitor == NULL)
      continue;

    if (area) {
      monitor_area = (GdkRectangle) {
        .x = monitor->logical.x,
        .y = monitor->logical.y,
        .width = monitor->logical.width,
        .height = monitor->logical.height,
      };
      if (gdk_rectangle_intersect (area, &monitor_area, NULL) == FALSE)
        continue;
    }

    screencopy_frame = g_new0 (ScreencopyFrame, 1);
    screencopy_frame->manager = self;
    screencopy_frame->monitor = monitor;
    g_object_add_weak_pointer (G_OBJECT (monitor), (gpointer)&screencopy_frame->monitor);
    screencopy_frame->frame = zwlr_screencopy_manager_v1_capture_output (
      self->wl_scm, include_cursor, monitor->wl_output);
    zwlr_screencopy_frame_v1_add_listener (screencopy_frame->frame, &screencopy_frame_listener,
                                           screencopy_frame);
    frames->frames = g_list_prepend (frames->frames, screencopy_frame);
    num_outputs++;

    /* Use the maximum scale of an involved monitor as the screenshot scale. */
    monitor_scale = phosh_monitor_get_fractional_scale (monitor);
    max_scale = fmaxf (max_scale, monitor_scale);
  }
  frames->num_outputs = num_outputs;

  g_return_val_if_fail (max_scale > 0.0, FALSE);
  frames->max_scale = max_scale;

  if (area)
    frames->area = g_memdup2 (area, sizeof (GdkRectangle));

  self->frames = g_steal_pointer (&frames);
  return TRUE;
}


static gboolean
handle_screenshot_area (PhoshDBusScreenshot   *object,
                        GDBusMethodInvocation *invocation,
                        gint                   arg_x,
                        gint                   arg_y,
                        gint                   arg_width,
                        gint                   arg_height,
                        gboolean               arg_flash,
                        const char            *arg_filename)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (object);
  GdkRectangle area;
  gboolean success;

  g_debug ("DBus call %s: @%d,%d %dx%d, flash %d, to %s",
           __func__, arg_x, arg_y, arg_width, arg_height, arg_flash, arg_filename);

  area = (GdkRectangle) {
    .x = arg_x,
    .y = arg_y,
    .width = arg_width,
    .height = arg_height,
  };

  success = phosh_screenshot_manager_do_screenshot (self, &area, FALSE);
  if (!success) {
    phosh_dbus_screenshot_complete_screenshot_area (object, invocation, FALSE, "");
    return TRUE;
  }

  self->frames->flash = arg_flash;
  self->frames->invocation = invocation;
  self->frames->filename = build_dbus_filename (arg_filename);
  self->frames->copy_to_clipboard = !self->frames->filename;

  return TRUE;
}


static gboolean
handle_screenshot (PhoshDBusScreenshot   *object,
                   GDBusMethodInvocation *invocation,
                   gboolean               arg_include_cursor,
                   gboolean               arg_flash,
                   const char            *arg_filename)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (object);
  gboolean success;

  g_debug ("DBus call %s, cursor: %d, flash %d, to %s",
           __func__, arg_include_cursor, arg_flash, arg_filename);

  success = phosh_screenshot_manager_do_screenshot (self, NULL, arg_include_cursor);
  if (!success) {
    phosh_dbus_screenshot_complete_screenshot (object, invocation, FALSE, "");
    return TRUE;
  }

  self->frames->flash = arg_flash;
  self->frames->invocation = invocation;
  self->frames->filename = build_dbus_filename (arg_filename);
  self->frames->copy_to_clipboard = !self->frames->filename;

  return TRUE;
}


/* Taken from grim */
static gboolean
parse_slurp (const char *str, GdkRectangle *box)
{
  char *end = NULL;
  char *next;

  box->x = strtol (str, &end, 10);
  if (end[0] != ',')
    return FALSE;

  next = end + 1;
  box->y = strtol (next, &end, 10);
  if (end[0] != ' ')
    return FALSE;

  next = end + 1;
  box->width = strtol (next, &end, 10);
  if (end[0] != 'x')
    return FALSE;

  next = end + 1;
  box->height = strtol (next, &end, 10);
  if (end[0] != '\0' && end[0] != '\n')
    return FALSE;

  return TRUE;
}


static void
on_slurp_exited (GPid pid, int wait_status, gpointer user_data)
{
  PhoshScreenshotManager *self;
  GdkRectangle box;

  g_return_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (user_data));
  self = PHOSH_SCREENSHOT_MANAGER (user_data);

  g_return_if_fail (pid == self->slurp->pid);

  g_debug ("Selected area: %s", self->slurp->response->str);

  if (parse_slurp (self->slurp->response->str, &box)) {
    phosh_dbus_screenshot_complete_select_area (PHOSH_DBUS_SCREENSHOT (self),
                                                self->slurp->invocation,
                                                box.x, box.y, box.width, box.height);
  } else {
    g_dbus_method_invocation_return_error (self->slurp->invocation, G_DBUS_ERROR,
                                           G_DBUS_ERROR_FAILED,
                                           "Area selection failed");
  }

  g_clear_pointer (&self->slurp, slurp_area_dispose);
}


static void
on_slurp_read_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GInputStream *slurp_out = G_INPUT_STREAM (source_object);
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (user_data);
  gssize read_size;
  g_autoptr (GError) error = NULL;

  read_size = g_input_stream_read_finish (slurp_out, res, &error);
  switch (read_size) {
  case -1:
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning ("Slurp cancelled");
      g_dbus_method_invocation_return_error (self->slurp->invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_FAILED,
                                             "Area selection cancelled");
      g_clear_pointer (&self->slurp, slurp_area_dispose);
      return;
    }
    break;
  case 0:
    /* Done reading. */
    self->slurp->child_watch_id = g_child_watch_add (self->slurp->pid, on_slurp_exited, self);
    break;
  default:
    g_string_append_len (self->slurp->response, self->slurp->read_buf, read_size);
    g_input_stream_read_async (slurp_out,
                               self->slurp->read_buf,
                               sizeof(self->slurp->read_buf),
                               G_PRIORITY_DEFAULT,
                               NULL,
                               on_slurp_read_done,
                               self);
    return;
  }

  g_input_stream_close (slurp_out, NULL, NULL);
}


static gboolean
handle_select_area (PhoshDBusScreenshot   *object,
                    GDBusMethodInvocation *invocation)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (object);
  const char *cmd[] = { "slurp", NULL };
  gboolean success;
  int slurp_stdout_fd;
  g_autofree SlurpArea *slurp = NULL;
  g_autoptr (GError) err = NULL;
  GPid slurp_pid;

  g_debug ("DBus call %s", __func__);

  g_return_val_if_fail (slurp == NULL, FALSE);

  success = g_spawn_async_with_pipes (NULL,
                                      (char **) cmd,
                                      NULL, /* envp */
                                      G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                      NULL, /* setup-func */
                                      NULL, /* user_data */
                                      &slurp_pid,
                                      NULL,
                                      &slurp_stdout_fd,
                                      NULL,
                                      &err);
  if (!success) {
    g_warning ("Failed to spawn slurp: %s", err->message);
    return FALSE;
  }

  slurp = g_new0 (SlurpArea, 1);
  slurp->stdout_ = g_unix_input_stream_new (slurp_stdout_fd, TRUE);
  slurp->invocation = invocation;
  slurp->pid = slurp_pid;
  slurp->response = g_string_new (NULL);
  g_input_stream_read_async (slurp->stdout_,
                             slurp->read_buf,
                             sizeof(slurp->read_buf),
                             G_PRIORITY_DEFAULT,
                             slurp->cancel,
                             on_slurp_read_done,
                             self);

  self->slurp = g_steal_pointer (&slurp);
  return TRUE;
}


static void
phosh_screenshot_manager_screenshot_iface_init (PhoshDBusScreenshotIface *iface)
{
  iface->handle_screenshot = handle_screenshot;
  iface->handle_select_area = handle_select_area;
  iface->handle_screenshot_area = handle_screenshot_area;
}


static void
take_screenshot (GSimpleAction *action, GVariant *param, gpointer data)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (data);

  g_return_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (self));

  phosh_screenshot_manager_take_screenshot (self, NULL, NULL, TRUE, FALSE);
}


static void
add_keybindings (PhoshScreenshotManager *self)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
  g_autoptr (GArray) actions = g_array_new (FALSE, TRUE, sizeof (GActionEntry));

  PHOSH_UTIL_BUILD_KEYBINDING (actions,
                               builder,
                               self->settings,
                               KEYBINDING_KEY_SCREENSHOT,
                               take_screenshot);

  phosh_shell_add_global_keyboard_action_entries (phosh_shell_get_default (),
                                                  (GActionEntry *)actions->data,
                                                  actions->len,
                                                  self);
  self->action_names = g_strv_builder_end (builder);
}


static void
on_keybindings_changed (PhoshScreenshotManager *self)
{
  g_debug ("Updating keybindings in screenshot-manager");
  phosh_shell_remove_global_keyboard_action_entries (phosh_shell_get_default (),
                                                     self->action_names);
  g_clear_pointer (&self->action_names, g_strfreev);
  add_keybindings (self);
}


static void
on_name_acquired (GDBusConnection *connection,
                  const char      *name,
                  gpointer         user_data)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (user_data);

  g_return_if_fail (PHOSH_IS_SCREENSHOT_MANAGER (self));
  g_debug ("Acquired name %s", name);
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
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (user_data);
  g_autoptr (GError) err = NULL;

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self),
                                         connection,
                                         OBJECT_PATH,
                                         &err)) {
    g_warning ("Failed to export screensaver interface skeleton: %s", err->message);
  }

}


static void
phosh_screenshot_manager_constructed (GObject *object)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (object);
  PhoshWayland *wl = phosh_wayland_get_default ();

  G_OBJECT_CLASS (phosh_screenshot_manager_parent_class)->constructed (object);

  self->dbus_name_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                       BUS_NAME,
                                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                       on_bus_acquired,
                                       on_name_acquired,
                                       on_name_lost,
                                       self,
                                       NULL);

  g_return_if_fail (PHOSH_IS_WAYLAND (wl));
  self->wl_scm = phosh_wayland_get_zwlr_screencopy_manager_v1 (wl);
}


static void
phosh_screenshot_manager_dispose (GObject *object)
{
  PhoshScreenshotManager *self = PHOSH_SCREENSHOT_MANAGER (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);

  g_clear_handle_id (&self->dbus_name_id, g_bus_unown_name);

  if (g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (self)))
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self));

  g_clear_pointer (&self->frames, screencopy_frames_dispose);
  g_clear_object (&self->for_clipboard);
  g_clear_pointer (&self->slurp, slurp_area_dispose);

  g_clear_handle_id (&self->fader_id, g_source_remove);
  g_clear_handle_id (&self->opaque_id, g_source_remove);
  g_clear_pointer (&self->fader, phosh_cp_widget_destroy);

  g_clear_pointer (&self->action_names, g_strfreev);
  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_screenshot_manager_parent_class)->dispose (object);
}


static void
phosh_screenshot_manager_class_init (PhoshScreenshotManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_screenshot_manager_constructed;
  object_class->dispose = phosh_screenshot_manager_dispose;
}


static void
phosh_screenshot_manager_init (PhoshScreenshotManager *self)
{
  self->cancel = g_cancellable_new ();
  self->settings = g_settings_new (KEYBINDINGS_SCHEMA_ID);
  g_signal_connect_swapped (self->settings,
                            "changed::" KEYBINDING_KEY_SCREENSHOT,
                            G_CALLBACK (on_keybindings_changed),
                            self);
  add_keybindings (self);
}

PhoshScreenshotManager *
phosh_screenshot_manager_new (void)
{
  return PHOSH_SCREENSHOT_MANAGER (g_object_new (PHOSH_TYPE_SCREENSHOT_MANAGER, NULL));
}

/**
 * phosh_screenshot_manager_take_screenshot:
 * @self: The screenshot manager
 * @area: (nullable): The area to capture or %NULL to capture all outputs
 * @filename: (nullable): The output filename or %NULL to autogenerate a filename
 * @copy_to_clipboard: Whether to use the clipboard
 * @include_cursor: Whether to include the cursor
 *
 * Initiate a screenshot of all outputs or the given area. If `copy_to_clipboard` is
 * `TRUE` the screenshot is also copied to the clipboard.
 *
 * Returns: `FALSE` on failure, otherwise `TRUE`
 */
gboolean
phosh_screenshot_manager_take_screenshot (PhoshScreenshotManager *self,
                                          const GdkRectangle     *area,
                                          const char             *filename,
                                          gboolean                copy_to_clipboard,
                                          gboolean                include_cursor)
{
  gboolean ret;

  ret = phosh_screenshot_manager_do_screenshot (self, area, include_cursor);
  self->frames->copy_to_clipboard = copy_to_clipboard;

  return ret;
}
