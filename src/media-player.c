/*
 * Copyright (C) 2020 Purism SPC
 *               2024-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-media-player"

#include "phosh-config.h"

#include "mpris-dbus.h"
#include "mpris-manager.h"
#include "media-player.h"
#include "shell-priv.h"
#include "util.h"

#include <gmobile.h>
#include <cui-call.h>

#include <glib/gi18n.h>

#include <handy.h>

#define ART_PIXEL_SIZE 48
#define SEEK_SECOND 1000000
#define SEEK_BACK (-10 * SEEK_SECOND)
#define SEEK_FORWARD (30 * SEEK_SECOND)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_t, cairo_destroy)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (cairo_surface_t, cairo_surface_destroy)

/**
 * PhoshMediaPlayer:
 *
 * A simple MPRIS media player widget
 *
 * The #PhoshMediaPlayer widget uses `PhoshMprisManager` to
 * interface with media players allowing to skip through music and
 * raising the player.
 *
 * Whenever valid mpris player is set, the #PhoshMediaPlayer:attached
 * property is set to %TRUE. This can e.g. be used with
 * #g_object_bind_property() to toggle the widget's visibility.
 *
 * # Example
 *
 * |[
 * <object class="PhoshMediaPlayer" id="media_player">
 *   <property name="visible" bind-source="media_player" bind-property="attached" bind-flags="sync-create"/>
 * </object>
 * ]|
 */

enum {
  PROP_0,
  PROP_ATTACHED,
  PROP_PLAYABLE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  PLAYER_RAISED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

typedef struct _PhoshMediaPlayerPrivate {
  GtkWidget                   *btn_play;
  GtkWidget                   *btn_next;
  GtkWidget                   *btn_prev;
  GtkWidget                   *btn_details;
  GtkWidget                   *btn_seek_backward;
  GtkWidget                   *btn_seek_forward;
  GtkWidget                   *img_art;
  GtkWidget                   *img_play;
  GtkWidget                   *lbl_title;
  GtkWidget                   *lbl_artist;
  GtkWidget                   *box_pos_len;
  GtkWidget                   *prb_position;
  GtkWidget                   *lbl_position;
  GtkWidget                   *lbl_length;

  char                        *url;
  GCancellable                *cancel;
  GCancellable                *fetch_icon_cancel;
  PhoshMprisManager           *manager;
  /* Actual player controls */
  PhoshDBusMediaPlayer2Player *player;
  PhoshMediaPlayerStatus       status;
  gboolean                     attached;
  gboolean                     playable;
  gint64                       track_length;
  gint64                       track_position;
  guint                        pos_poller_id;
} PhoshMediaPlayerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshMediaPlayer, phosh_media_player, GTK_TYPE_GRID);


static void
phosh_media_player_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  PhoshMediaPlayer *self = PHOSH_MEDIA_PLAYER (object);
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  switch (property_id) {
  case PROP_ATTACHED:
    g_value_set_boolean (value, priv->attached);
    break;
  case PROP_PLAYABLE:
    g_value_set_boolean (value, priv->playable);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
set_playable (PhoshMediaPlayer *self, gboolean playable)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  if (priv->playable == playable)
    return;

  priv->playable = playable;
  g_debug ("Playable: %d", playable);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PLAYABLE]);
}


static void
set_attached (PhoshMediaPlayer *self, gboolean attached)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));

  if (priv->attached == attached)
    return;

  priv->attached = attached;
  if (!attached)
    set_playable (self, FALSE);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ATTACHED]);
}


static void
update_position (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autofree char *position_text = NULL;
  double level = 0.0;

  if (priv->track_position >= 0)
    position_text = cui_call_format_duration ((double) priv->track_position / G_USEC_PER_SEC);

  gtk_label_set_label (GTK_LABEL (priv->lbl_position), position_text ?: "-");

  if (priv->track_position > 0 && priv->track_length > 0)
    level = ((double) priv->track_position) / priv->track_length;
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->prb_position), level);
}


static void
stop_pos_poller (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  if (priv->pos_poller_id <= 0)
    return;

  g_debug ("Stopping position poller");
  g_source_remove (priv->pos_poller_id);
  priv->pos_poller_id = 0;
}


static void
on_poll_position_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GDBusProxy *proxy = G_DBUS_PROXY (source_object);
  PhoshMediaPlayer *self;
  PhoshMediaPlayerPrivate *priv;
  g_autoptr (GError) err = NULL;
  g_autoptr (GVariant) var = NULL;

  var = g_dbus_proxy_call_finish (proxy, res, &err);
  if (!var && g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (user_data));
  self = PHOSH_MEDIA_PLAYER (user_data);
  priv = phosh_media_player_get_instance_private (self);

  if (var) {
    g_autoptr (GVariant) var2 = NULL;

    /* Return variant has type "(v)" where v has type x (i.e. gint64) */
    g_variant_get_child (var, 0, "v", &var2);
    priv->track_position = g_variant_get_int64 (var2);
    g_debug ("MPRIS Position: %" G_GINT64_FORMAT, priv->track_position);
  } else {
    g_warning ("Could not get Position from MPRIS player, hiding box_pos_len: %s", err->message);
    priv->track_position = -1;
    gtk_widget_set_visible (priv->box_pos_len, FALSE);
    stop_pos_poller (self);
  }

  update_position (self);
}


static gboolean
poll_position (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MEDIA_PLAYER (self), G_SOURCE_CONTINUE);

  if (!priv->attached || priv->player == NULL) {
    g_debug ("No MPRIS player attached");
    priv->pos_poller_id = 0;
    return G_SOURCE_REMOVE;
  }

  if (!gtk_widget_is_visible (priv->lbl_position)) {
    g_debug ("Widget hidden, not updating Position");
    return G_SOURCE_CONTINUE;
  }

  g_dbus_proxy_call (G_DBUS_PROXY (priv->player),
                     "org.freedesktop.DBus.Properties.Get",
                     g_variant_new ("(ss)", "org.mpris.MediaPlayer2.Player", "Position"),
                     G_DBUS_CALL_FLAGS_NONE, -1, priv->cancel,
                     on_poll_position_done, self);

  return G_SOURCE_CONTINUE;
}


#define POLLER_INTERVAL 1 /* seconds */
static void
start_pos_poller (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  if (!gtk_widget_get_visible (priv->box_pos_len)) {
    g_debug ("box_pos_len not visible, not starting position poller");
    return;
  }
  if (priv->pos_poller_id != 0) {
    g_debug ("Position poller already running");
    return;
  }
  g_debug ("Starting position poller");
  poll_position (self);
  priv->pos_poller_id = g_timeout_add_seconds (POLLER_INTERVAL,
                                               (GSourceFunc) poll_position,
                                               self);
  g_source_set_name_by_id (priv->pos_poller_id, "[PhoshMediaPlayer] pos_poller");
}


static void
on_play_pause_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusMediaPlayer2Player *player = PHOSH_DBUS_MEDIA_PLAYER2_PLAYER (source_object);
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (player));

  if (!phosh_dbus_media_player2_player_call_play_pause_finish (player, res, &err))
    phosh_async_error_warn (err, "Failed to trigger play/pause");
}


static void
btn_play_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (priv->player));

  g_debug ("Play/pause");
  phosh_media_player_toggle_play_pause (self);
}


static void
on_next_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusMediaPlayer2Player *player = PHOSH_DBUS_MEDIA_PLAYER2_PLAYER (source_object);
  PhoshMediaPlayer *self = PHOSH_MEDIA_PLAYER (user_data);
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (player));

  if (!phosh_dbus_media_player2_player_call_next_finish (player, res, &err)) {
    phosh_async_error_warn (err, "Failed to trigger next");
    return;
  }
  priv->track_position = 0;
}


static void
btn_next_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (priv->player));

  g_debug ("next");
  phosh_dbus_media_player2_player_call_next (priv->player, priv->cancel, on_next_done, self);
}


static void
on_previous_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusMediaPlayer2Player *player = PHOSH_DBUS_MEDIA_PLAYER2_PLAYER (source_object);
  PhoshMediaPlayer *self = PHOSH_MEDIA_PLAYER (user_data);
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (player));

  if (!phosh_dbus_media_player2_player_call_previous_finish (player, res, &err)) {
    phosh_async_error_warn (err, "Failed to trigger prev");
    return;
  }
  priv->track_position = 0;
}


static void
btn_prev_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (priv->player));

  g_debug ("prev");
  phosh_dbus_media_player2_player_call_previous (priv->player,
                                                 priv->cancel,
                                                 on_previous_done,
                                                 self);
}


static void
on_seek_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshDBusMediaPlayer2Player *player = PHOSH_DBUS_MEDIA_PLAYER2_PLAYER (source_object);
  PhoshMediaPlayer *self = PHOSH_MEDIA_PLAYER (user_data);
  g_autoptr (GError) err = NULL;

  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (player));

  if (!phosh_dbus_media_player2_player_call_seek_finish (player, res, &err)) {
    g_warning ("Failed to trigger seek: %s", err->message);
    return;
  }
  poll_position (self);
}


static void
btn_seek_backward_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (priv->player));

  g_debug ("seek backward for %ds", SEEK_BACK / SEEK_SECOND);
  phosh_dbus_media_player2_player_call_seek (priv->player,
                                             SEEK_BACK,
                                             priv->cancel,
                                             on_seek_done,
                                             self);
}


static void
btn_seek_forward_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_DBUS_IS_MEDIA_PLAYER2_PLAYER (priv->player));

  g_debug ("seek forward by %ds", SEEK_FORWARD / SEEK_SECOND);
  phosh_dbus_media_player2_player_call_seek (priv->player,
                                             SEEK_FORWARD,
                                             priv->cancel,
                                             on_seek_done,
                                             self);
}


static void
on_raise_done (GObject *object, GAsyncResult *res, gpointer data)
{
  PhoshMprisManager *manager = PHOSH_MPRIS_MANAGER (object);
  g_autoptr (GError) err = NULL;
  PhoshMediaPlayer *self;

  if (!phosh_mpris_manager_raise_finish (manager, res, &err)) {
    phosh_async_error_warn (err, "Failed to raise player");
    return;
  }

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (data));
  self = PHOSH_MEDIA_PLAYER (data);
  g_signal_emit (self, signals[PLAYER_RAISED], 0);
}


static void
btn_details_clicked_cb (PhoshMediaPlayer *self, GtkButton *button)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_IS_MPRIS_MANAGER (priv->manager));

  if (!phosh_mpris_manager_get_can_raise (priv->manager))
    return;

  phosh_mpris_manager_raise_async (priv->manager, priv->cancel, on_raise_done, self);
}


static GdkPixbuf *
center_pixbuf (GdkPixbuf *pixbuf)
{
  int width, height, size;
  g_autoptr (GdkPixbuf) centered = NULL;
  g_autoptr (cairo_t) cr = NULL;
  g_autoptr (cairo_surface_t) surface = NULL;

  g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  if (width == height)
    return g_object_ref (pixbuf);

  size = MAX (width, height);
  /* gdk_pixbuf_copy_area would work as well but that goes via gdk_pixbuf_scale, …*/
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);
  if (width > height)
    gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, (width - height) / 2.0);
  else
    gdk_cairo_set_source_pixbuf (cr, pixbuf, (height - width) / 2.0, 0);
  cairo_paint (cr);

  return gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
}


static GdkPixbuf *
round_corners (GdkPixbuf *pixbuf)
{
  g_autoptr (GdkPixbuf) rounded = NULL;
  g_autoptr (cairo_t) cr = NULL;
  g_autoptr (cairo_surface_t) surface = NULL;
  int width, height, size;
  double radius;
  const double degrees = M_PI / 180.0;

  g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);

  /* We only round square images */
  g_return_val_if_fail (width == height, NULL);

  size = width;
  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, size, size);
  cr = cairo_create (surface);

  radius = size / 8.0;
  cairo_new_path (cr);
  cairo_arc (cr, size - radius, radius, radius, -90 * degrees, 0 * degrees);
  cairo_arc (cr, size - radius, size - radius, radius, 0 * degrees, 90 * degrees);
  cairo_arc (cr, radius, size - radius, radius, 90 * degrees, 180 * degrees);
  cairo_arc (cr, radius, radius, radius, 180 * degrees, 270 * degrees);
  cairo_close_path (cr);
  cairo_clip (cr);

  gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
  cairo_paint (cr);

  return gdk_pixbuf_get_from_surface (surface, 0, 0, size, size);
}


static void
phosh_media_player_set_image (PhoshMediaPlayer *self, GdkPixbuf *pixbuf)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autoptr (GdkPixbuf) centered = NULL;
  g_autoptr (GdkPixbuf) rounded = NULL;

  centered = center_pixbuf (pixbuf);
  rounded = round_corners (centered);
  g_object_set (priv->img_art, "gicon", rounded, NULL);
}


static void
on_fetch_icon_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshMediaPlayer *self;
  PhoshMediaPlayerPrivate *priv;
  g_autoptr (GInputStream) stream = NULL;
  g_autoptr (GError) err = NULL;
  g_autofree char *type = NULL;
  g_autoptr (GdkPixbuf) pixbuf = NULL;

  stream = g_loadable_icon_load_finish (G_LOADABLE_ICON (source_object), res, &type, &err);
  if (!stream) {
    g_warning ("Failed to fetch icon: %s", err->message);
    return;
  }

  g_debug ("Loading icon of type: %s", type);
  self = PHOSH_MEDIA_PLAYER (user_data);
  priv = phosh_media_player_get_instance_private (self);
  pixbuf = gdk_pixbuf_new_from_stream (stream, priv->fetch_icon_cancel, &err);

  phosh_media_player_set_image (self, pixbuf);
}


static void
fetch_icon_async (PhoshMediaPlayer *self, const char *url)

{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autoptr (GFile) file = g_file_new_for_uri (url);
  GIcon *icon;

  g_debug ("Fetching icon for %s", url);

  if (!priv->fetch_icon_cancel)
    priv->fetch_icon_cancel = g_cancellable_new ();

  icon = g_file_icon_new (file);
  g_loadable_icon_load_async (G_LOADABLE_ICON (icon),
                              ART_PIXEL_SIZE,
                              priv->fetch_icon_cancel,
                              on_fetch_icon_ready,
                              self);
}


static void
on_load_icon_from_file_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  PhoshMediaPlayer *self;
  g_autoptr (GdkPixbuf) pixbuf = NULL;
  g_autoptr (GError) err = NULL;

  pixbuf = gdk_pixbuf_new_from_stream_finish (res, &err);
  if (!pixbuf) {
    phosh_async_error_warn (err, "Failed to load image");
    return;
  }

  self = PHOSH_MEDIA_PLAYER (user_data);
  phosh_media_player_set_image (self, pixbuf);
}


static void
phosh_media_player_load_icon_from_file_async (PhoshMediaPlayer *self, GFile *file)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  g_autoptr (GFileInputStream) stream = NULL;
  g_autoptr (GError) err = NULL;

  stream = g_file_read (file, NULL, &err);
  if (stream == NULL) {
    g_autofree char *path = g_file_get_path (file);

    g_warning ("Failed to open '%s': %s", path, err->message);
    return;
  }

  if (!priv->fetch_icon_cancel)
    priv->fetch_icon_cancel = g_cancellable_new ();

  gdk_pixbuf_new_from_stream_async (G_INPUT_STREAM (stream),
                                    priv->fetch_icon_cancel,
                                    on_load_icon_from_file_ready,
                                    self);
}


static gboolean
phosh_media_player_load_icon (PhoshMediaPlayer *self, const char *url)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  gboolean has_art = FALSE;

  if (!g_set_str (&priv->url, url)) {
    g_debug ("Media URL did not change, skippig load");
    return TRUE;
  }

  g_debug ("Loading '%s'", url);
  /* Cancel any pending icon loads */
  g_cancellable_cancel (priv->fetch_icon_cancel);
  g_clear_object (&priv->fetch_icon_cancel);

  if (url && g_strcmp0 (g_uri_peek_scheme (url), "file") == 0) {
    g_autoptr (GFile) file = g_file_new_for_uri (url);

    phosh_media_player_load_icon_from_file_async (self, file);
  } else if (url && (g_strcmp0 (g_uri_peek_scheme (url), "http") == 0 ||
                     g_strcmp0 (g_uri_peek_scheme (url), "https") == 0)) {
    fetch_icon_async (self, url);
  } else if (url && g_strcmp0 (g_uri_peek_scheme (url), "data") == 0) {
    g_autoptr (GdkPixbuf) pixbuf = NULL;
    g_autoptr (GError) error = NULL;

    pixbuf = phosh_util_data_uri_to_pixbuf (url, &error);
    if (pixbuf) {
      phosh_media_player_set_image (self, pixbuf);
      has_art = TRUE;
    } else {
      g_warning_once ("Failed to load album art from base64 string: %s", error->message);
    }
  }

  return has_art;
}


static void
on_metadata_changed (PhoshMediaPlayer *self, GParamSpec *psepc, PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  GVariant *metadata;
  char *title = NULL;
  char *url = NULL;
  gint64 length = -1;
  g_auto (GStrv) artist = NULL;
  g_auto (GVariantDict) dict = G_VARIANT_DICT_INIT (NULL);
  gboolean has_art = FALSE;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_debug ("Updating metadata");

  metadata = phosh_dbus_media_player2_player_get_metadata (player);

  if (metadata) {
    g_variant_dict_init (&dict, metadata);
    g_variant_dict_lookup (&dict, "xesam:title", "&s", &title);
    g_variant_dict_lookup (&dict, "xesam:artist", "^as", &artist);
    g_variant_dict_lookup (&dict, "mpris:artUrl", "&s", &url);
    g_variant_dict_lookup (&dict, "mpris:length", "x", &length);
  }

  if (title) {
    gtk_label_set_label (GTK_LABEL (priv->lbl_title), title);
  } else {
    /* Translators: Used when the title of a song is unknown */
    gtk_label_set_label (GTK_LABEL (priv->lbl_title), _("Unknown Title"));
  }

  if (artist && g_strv_length (artist) > 0) {
    g_autofree char *artists = g_strjoinv (", ", artist);
    gtk_label_set_label (GTK_LABEL (priv->lbl_artist), artists);
  } else {
    /* Translators: Used when the artist of a song is unknown */
    gtk_label_set_label (GTK_LABEL (priv->lbl_artist), _("Unknown Artist"));
  }

  if (length >= 0) {
    g_autofree char *length_text = cui_call_format_duration ((double) length / G_USEC_PER_SEC);
    gtk_label_set_label (GTK_LABEL (priv->lbl_length), length_text);
    g_debug ("Metadata has length, showing box_pos_len");
    gtk_widget_set_visible (priv->box_pos_len, TRUE);
    if (priv->status == PHOSH_MEDIA_PLAYER_STATUS_PLAYING)
      start_pos_poller (self);
  } else {
    gtk_label_set_label (GTK_LABEL (priv->lbl_length), "-");
  }
  priv->track_length = length;
  update_position (self);

  has_art = phosh_media_player_load_icon (self, url);

  if (!has_art)
    g_object_set (priv->img_art, "icon-name", "audio-x-generic-symbolic", NULL);
}


static void
on_playback_status_changed (PhoshMediaPlayer            *self,
                            GParamSpec                  *psepc,
                            PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  const char *status, *icon = "media-playback-start-symbolic";
  PhoshMediaPlayerStatus current;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));

  status = phosh_dbus_media_player2_player_get_playback_status (player);

  /* No mpris running, widget will not be shown */
  if (status == NULL)
    return;

  g_debug ("Status: '%s'", status);
  current = priv->status;
  if (!g_strcmp0 ("Playing", status)) {
    priv->status = PHOSH_MEDIA_PLAYER_STATUS_PLAYING;
    icon = "media-playback-pause-symbolic";
    start_pos_poller (self);
  } else if (!g_strcmp0 ("Paused", status)) {
    priv->status = PHOSH_MEDIA_PLAYER_STATUS_PAUSED;
    stop_pos_poller (self);
    poll_position (self);
  } else if (!g_strcmp0 ("Stopped", status)) {
    priv->status = PHOSH_MEDIA_PLAYER_STATUS_STOPPED;
    stop_pos_poller (self);
    priv->track_position = 0;
    update_position (self);
  } else {
    g_warning ("Unknown status %s", status);
    g_warn_if_reached ();
  }

  if (priv->status != current) {
    g_object_set (priv->img_play, "icon-name", icon, NULL);
    gtk_widget_set_valign (priv->img_play, GTK_ALIGN_START);
  }
}


static void
on_can_go_next_changed (PhoshMediaPlayer            *self,
                        GParamSpec                  *psepc,
                        PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  gboolean sensitive;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  sensitive = phosh_dbus_media_player2_player_get_can_go_next (player);
  g_debug ("Can go next: %d", sensitive);
  gtk_widget_set_sensitive (priv->btn_next, sensitive);
}


static void
on_can_go_previous_changed (PhoshMediaPlayer            *self,
                            GParamSpec                  *psepc,
                            PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  gboolean sensitive;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  sensitive = phosh_dbus_media_player2_player_get_can_go_previous (player);
  g_debug ("Can go prev: %d", sensitive);
  gtk_widget_set_sensitive (priv->btn_prev, sensitive);
}


static void
on_can_play (PhoshMediaPlayer *self, GParamSpec *psepc, PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  gboolean can_play;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  can_play = phosh_dbus_media_player2_player_get_can_play (player);
  g_debug ("Can play: %d", can_play);
  gtk_widget_set_sensitive (priv->btn_play, can_play);
  set_playable (self, can_play);
}


static void
on_can_seek (PhoshMediaPlayer *self, GParamSpec *psepc, PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  gboolean sensitive;

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  sensitive = phosh_dbus_media_player2_player_get_can_seek (player);
  g_debug ("Can seek: %d", sensitive);
  gtk_widget_set_sensitive (priv->btn_seek_backward, sensitive);
  gtk_widget_set_sensitive (priv->btn_seek_forward, sensitive);
}


static void
phosh_media_player_dispose (GObject *object)
{
  PhoshMediaPlayer *self = PHOSH_MEDIA_PLAYER (object);
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  stop_pos_poller (self);
  g_cancellable_cancel (priv->cancel);
  g_clear_object (&priv->cancel);

  g_cancellable_cancel (priv->fetch_icon_cancel);
  g_clear_object (&priv->fetch_icon_cancel);

  g_clear_object (&priv->manager);
  g_clear_object (&priv->player);

  g_clear_pointer (&priv->url, g_free);

  G_OBJECT_CLASS (phosh_media_player_parent_class)->dispose (object);
}


static void
phosh_media_player_class_init (PhoshMediaPlayerClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_media_player_dispose;
  object_class->get_property = phosh_media_player_get_property;

  /**
   * PhoshMediaPlayer:attached
   *
   * Whether a player is attacked. This is %TRUE when we
   * found a suitable player on the session bus.
   */
  props[PROP_ATTACHED] =
    g_param_spec_boolean ("attached", "", "",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * PhoshMediaPlayer:playable
   *
   * Whether the player has a playable track. This is mostly
   * useful to ignore states where the player does not know
   * about any track and so no sensible information can be
   * shown.
   */
  props[PROP_PLAYABLE] =
    g_param_spec_boolean ("playable", "", "",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshMediaPlayer::player-raised:
   *
   * The player was raised to the user
   */
  signals[PLAYER_RAISED] = g_signal_new ("player-raised",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0,
                                         NULL,
                                         NULL,
                                         NULL,
                                         G_TYPE_NONE,
                                         0);

  gtk_widget_class_set_css_name (widget_class, "phosh-media-player");
  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/media-player.ui");
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_next);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_play);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_prev);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_details);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_seek_backward);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, btn_seek_forward);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, img_art);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, img_play);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, lbl_artist);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, lbl_title);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, box_pos_len);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, lbl_position);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, lbl_length);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshMediaPlayer, prb_position);
  gtk_widget_class_bind_template_callback (widget_class, btn_play_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, btn_next_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, btn_prev_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, btn_details_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, btn_seek_backward_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, btn_seek_forward_clicked_cb);
}


static void
phosh_media_player_init (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);
  PhoshMprisManager *manager = phosh_shell_get_mpris_manager (phosh_shell_get_default ());

  gtk_widget_init_template (GTK_WIDGET (self));

  priv->cancel = g_cancellable_new ();
  priv->track_length = -1;
  priv->track_position = -1;
  priv->pos_poller_id = 0;

  if (manager) {
    priv->manager = g_object_ref (manager);

    g_object_bind_property (priv->manager,
                            "can-raise",
                            priv->btn_details,
                            "sensitive",
                            G_BINDING_DEFAULT);
  }
}


GtkWidget *
phosh_media_player_new (void)
{
  return g_object_new (PHOSH_TYPE_MEDIA_PLAYER, NULL);
}


gboolean
phosh_media_player_get_is_playable (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MEDIA_PLAYER (self), FALSE);

  return priv->playable;
}


PhoshMediaPlayerStatus
phosh_media_player_get_status (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_val_if_fail (PHOSH_IS_MEDIA_PLAYER (self), PHOSH_MEDIA_PLAYER_STATUS_STOPPED);

  return priv->status;
}


void
phosh_media_player_toggle_play_pause (PhoshMediaPlayer *self)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));

  phosh_dbus_media_player2_player_call_play_pause (priv->player,
                                                   priv->cancel,
                                                   on_play_pause_done,
                                                   self);
}


void
phosh_media_player_set_player (PhoshMediaPlayer *self, PhoshDBusMediaPlayer2Player *player)
{
  PhoshMediaPlayerPrivate *priv = phosh_media_player_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_MEDIA_PLAYER (self));
  g_return_if_fail (PHOSH_IS_MPRIS_MANAGER (priv->manager));

  if (priv->player)
    g_signal_handlers_disconnect_by_data (priv->player, self);

  g_set_object (&priv->player, player);

  if (!priv->player) {
    set_attached (self, FALSE);
    return;
  }

  g_debug ("Connected player %p", priv->player);
  g_object_connect (priv->player,
                    "swapped-object-signal::notify::metadata",
                    G_CALLBACK (on_metadata_changed),
                    self,
                    "swapped-object-signal::notify::playback-status",
                    G_CALLBACK (on_playback_status_changed),
                    self,
                    "swapped-object-signal::notify::can-go-next",
                    G_CALLBACK (on_can_go_next_changed),
                    self,
                    "swapped-object-signal::notify::can-go-previous",
                    G_CALLBACK (on_can_go_previous_changed),
                    self,
                    "swapped-object-signal::notify::can-play",
                    G_CALLBACK (on_can_play),
                    self,
                    "swapped-object-signal::notify::can-seek",
                    G_CALLBACK (on_can_seek),
                    self,
                    NULL);

  /* Set 'attached' before running notifiers, since we check it on e.g. start_pos_poller() */
  set_attached (self, TRUE);
  /* Hide progress bar box by default, it's shown if track length is given in metadata */
  gtk_widget_set_visible (priv->box_pos_len, FALSE);

  g_object_notify (G_OBJECT (priv->player), "metadata");
  g_object_notify (G_OBJECT (priv->player), "playback-status");
  g_object_notify (G_OBJECT (priv->player), "can-go-next");
  g_object_notify (G_OBJECT (priv->player), "can-go-previous");
  g_object_notify (G_OBJECT (priv->player), "can-play");
  g_object_notify (G_OBJECT (priv->player), "can-seek");
}
