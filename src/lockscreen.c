/*
 * Copyright (C) 2018 Purism SPC
 *               2023-2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-lockscreen"

#include "auth.h"
#include "call-notification.h"
#include "calls-manager.h"
#include "keypad.h"
#include "layersurface-priv.h"
#include "lockscreen-bg.h"
#include "lockscreen-priv.h"
#include "notifications/notify-manager.h"
#include "notifications/notification-frame.h"
#include "osk-manager.h"
#include "shell-priv.h"
#include "util.h"
#include "widget-box.h"
#include "wall-clock-priv.h"

#include "gmobile.h"

#include <string.h>
#include <glib/gi18n.h>
#include <math.h>

#include <handy.h>
#include <cui-call-display.h>


#define LOCKSCREEN_IDLE_SECONDS 5
#define LOCKSCREEN_SMALL_DATE_AND_TIME_CLASS "p-small"

#define LOCKSCREEN_SMALL_DISPLAY 700

/**
 * PhoshLockscreen:
 *
 * The main lock screen
 *
 * The lock screen displayed on the primary output featuring the clock
 * and unlock keypad. It handles displaying ongoing calls when the
 * shell is locked and can be extended via plugins.
 *
 * Other outputs are locked via PhoshLockshields.
 *
 * # CSS nodes
 *
 * `PhoshLockscreen` has a CSS name with the name `phosh-lockscreen`.
 */


enum {
  PROP_0,
  PROP_CALLS_MANAGER,
  PROP_PAGE,
  PROP_REQUIRE_UNLOCK,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  LOCKSCREEN_UNLOCK,
  WAKEUP_OUTPUT,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

typedef struct {
  HdyDeck            *deck;
  GtkWidget          *carousel;
  PhoshLockscreenPage default_page;
  gboolean            require_unlock;

  /* info page */
  GtkWidget          *box_info;
  GtkWidget          *box_datetime;
  GtkListBox         *list_calls;
  GtkWidget          *lbl_clock;
  GtkWidget          *lbl_date;
  GtkWidget          *list_notifications;
  GtkRevealer        *rev_call_notifications;
  GtkRevealer        *rev_media_player;
  GtkRevealer        *rev_notifications;
  GSettings          *notification_settings;
  guint reveals;

  /* unlock page */
  GtkWidget          *box_unlock;
  GtkWidget          *keypad_revealer;
  GtkWidget          *keypad;
  GtkWidget          *entry_pin;
  GtkGesture         *long_press_del_gesture;
  GtkWidget          *lbl_unlock_status;
  GtkWidget          *btn_submit;
  GtkWidget          *btn_keyboard;
  guint idle_timer;
  gint64 last_input;
  PhoshAuth          *auth;
  GSettings          *lockscreen_settings;

  /* Volume Gesture */
  struct {
    GtkGesture *gesture;
    double      x_start, y_start;
    double      base;
  } brightness;

  /* extra page */
  GtkWidget         *extra_page;

  /* widget box */
  PhoshWidgetBox    *widget_box;

  /* Call page */
  GtkBox            *box_call_display;
  CuiCallDisplay    *call_display;

  PhoshCallsManager *calls_manager;
  char *active;              /* opaque handle to the active call */

  PhoshLockscreenBg *background;
} PhoshLockscreenPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshLockscreen, phosh_lockscreen, PHOSH_TYPE_LAYER_SURFACE)


static gboolean
is_valid (PhoshLockscreen *self, GtkGesture *gesture, double *x_center)
{
  double y_center, scale;
  uint height;
  gboolean active;

  active = gtk_gesture_get_bounding_box_center (gesture, x_center, &y_center);
  if (!active)
    return FALSE;

  height = gtk_widget_get_allocated_height (GTK_WIDGET (self));
  /* Swipe must happen in the upper screen half */
  if (y_center > 0.5 * height)
    return FALSE;

  scale = gtk_gesture_zoom_get_scale_delta (GTK_GESTURE_ZOOM (gesture));
  /* We don't want to actually zoom */
  if (scale >= 0.9 && scale <= 1.1) {
    return TRUE;
  }

  return FALSE;
}


static void
on_zoom_gesture_update (PhoshLockscreen  *self,
                        GdkEventSequence *sequence,
                        GtkGesture       *gesture)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshBrightnessManager *manager;
  double x_center;
  uint width;
  double brightness;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));

  if (!is_valid (self, gesture, &x_center))
    return;

  manager = phosh_shell_get_brightness_manager (phosh_shell_get_default ());
  width = gtk_widget_get_allocated_width (GTK_WIDGET (self));
  brightness = priv->brightness.base + (x_center - priv->brightness.x_start) / width;

  g_debug ("Brightness gesture updating: %f", brightness);
  phosh_brightness_manager_set_value (manager, CLAMP (brightness, 0.0, 1.0), TRUE);
}


static void
on_zoom_gesture_begin (PhoshLockscreen  *self,
                       GdkEventSequence *sequence,
                       GtkGesture       *gesture)
{
  gdouble x_center;
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshBrightnessManager *manager;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));

  if (!is_valid (self, gesture, &x_center))
    return;

  gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_CLAIMED);
  manager = phosh_shell_get_brightness_manager (phosh_shell_get_default ());
  priv->brightness.x_start = x_center;
  priv->brightness.base = phosh_brightness_manager_get_value (manager);
  g_debug ("Brightness gesture start at: %f (%f)",
           priv->brightness.x_start,
           priv->brightness.base);
}


static void
phosh_lockscreen_map (GtkWidget *widget)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (widget);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  GTK_WIDGET_CLASS (phosh_lockscreen_parent_class)->map (widget);

  phosh_layer_surface_set_stacked_below (PHOSH_LAYER_SURFACE (priv->background),
                                         PHOSH_LAYER_SURFACE (self));
}


static void
set_require_unlock (PhoshLockscreen *self, gboolean require_unlock)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  if (priv->require_unlock != require_unlock) {
    priv->require_unlock = require_unlock;
    g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REQUIRE_UNLOCK]);
  }
}

static void
phosh_lockscreen_set_property (GObject      *object,
                               guint         property_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (object);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  switch (property_id) {
  case PROP_CALLS_MANAGER:
    priv->calls_manager = g_value_dup_object (value);
    break;
  case PROP_REQUIRE_UNLOCK:
    set_require_unlock (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_lockscreen_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (object);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  switch (property_id) {
  case PROP_CALLS_MANAGER:
    g_value_set_object (value, priv->calls_manager);
    break;
  case PROP_PAGE:
    g_value_set_enum (value, phosh_lockscreen_get_page (self));
    break;
  case PROP_REQUIRE_UNLOCK:
    g_value_set_boolean (value, priv->require_unlock);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
clear_input (PhoshLockscreen *self, gboolean clear_all)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  if (clear_all)
    gtk_editable_delete_text (GTK_EDITABLE (priv->entry_pin), 0, -1);
  else
    g_signal_emit_by_name (priv->entry_pin, "backspace", NULL);
}


static gboolean
keypad_check_idle (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  gint64 now = g_get_monotonic_time ();

  g_assert (PHOSH_IS_LOCKSCREEN (self));
  if (priv->auth == NULL && now - priv->last_input > LOCKSCREEN_IDLE_SECONDS * 1000 * 1000) {
    phosh_lockscreen_set_page (self, priv->default_page);
    priv->idle_timer = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}


static void
show_unlock_page (PhoshLockscreen *self)
{
  phosh_lockscreen_set_page (self, PHOSH_LOCKSCREEN_PAGE_UNLOCK);
  /* skip signal on init */
  if (signals[WAKEUP_OUTPUT])
    g_signal_emit (self, signals[WAKEUP_OUTPUT], 0);
}


static gboolean
finish_shake_entry (PhoshLockscreen *self)
{
  clear_input (self, TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self), TRUE);
  return G_SOURCE_REMOVE;
}


static gboolean
shake_entry (GtkWidget     *widget,
             GdkFrameClock *frame_clock,
             gpointer       data)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (widget);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  gint64 start_time = g_variant_get_int64 (data);
  gint64 end_time = start_time + 1000 * 300;
  gint64 now = gdk_frame_clock_get_frame_time (frame_clock);

  float t = (float) (now - start_time) / (float) (end_time - start_time);
  float pos = sin (t * 10) * 0.05 + 0.5;

  if (now > end_time) {
    /* Stop the animation only when we would step over the idle position (0.5) */
    if ((gtk_entry_get_alignment (GTK_ENTRY (priv->entry_pin)) > 0.5 && pos < 0.5) || pos > 0.5) {
      guint id;

      gtk_entry_set_alignment (GTK_ENTRY (priv->entry_pin), 0.5);
      id = g_timeout_add (400, (GSourceFunc) finish_shake_entry, self);
      g_source_set_name_by_id (id, "[PhoshLockscreen] shake PIN entry");
      return FALSE;
    }
  }

  gtk_entry_set_alignment (GTK_ENTRY (priv->entry_pin), pos);
  return TRUE;
}

static void
focus_pin_entry (PhoshLockscreen *self, gboolean enable_osk)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  if (enable_osk) {
    /* restore default OSK behavior */
    g_object_set (priv->entry_pin, "im-module", NULL, NULL);
  }

  gtk_widget_set_sensitive (priv->entry_pin, TRUE);
  gtk_entry_grab_focus_without_selecting (GTK_ENTRY (priv->entry_pin));
}


static void
on_auth_authenticate_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  PhoshAuth *auth = PHOSH_AUTH (source_object);
  g_autoptr (PhoshLockscreen) self = PHOSH_LOCKSCREEN (user_data);
  PhoshLockscreenPrivate *priv;
  GError *error = NULL;
  gboolean authenticated;

  priv = phosh_lockscreen_get_instance_private (self);
  authenticated = phosh_auth_authenticate_finish (auth, result, &error);
  if (error != NULL) {
    g_warning ("Auth failed unexpected: %s", error->message);
    return;
  }

  if (authenticated) {
    g_signal_emit (self, signals[LOCKSCREEN_UNLOCK], 0);
  } else {
    /* give visual feedback on error */
    phosh_lockscreen_set_unlock_status (self, _("Enter Passcode"));
    phosh_lockscreen_shake_pin_entry (self);
    phosh_keypad_distribute (PHOSH_KEYPAD (priv->keypad));
  }
  g_clear_object (&priv->auth);
  priv->last_input = g_get_monotonic_time ();
}


static void
delete_button_clicked_cb (PhoshLockscreen *self,
                          GtkWidget       *widget)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));

  clear_input (self, FALSE);
}

static void
osk_button_clicked_cb (PhoshLockscreen *self,
                       GtkWidget       *widget)
{
  PhoshLockscreenPrivate *priv;

  g_assert (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  priv->last_input = g_get_monotonic_time ();

  focus_pin_entry (self, TRUE);
}


static void
on_osk_visibility_changed (PhoshLockscreen *self,
                           GParamSpec      *pspec,
                           PhoshOskManager *osk)
{
  PhoshLockscreenPrivate *priv;

  g_assert (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  if (!phosh_osk_manager_get_visible (osk))
    g_object_set (priv->entry_pin, "im-module", "gtk-im-context-none", NULL);
}


static void
long_press_del_cb (PhoshLockscreen *self,
                   double           x,
                   double           y,
                   GtkGesture      *gesture)
{
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  g_debug ("Long press on delete button");
  clear_input (self, TRUE);
}


static void
input_changed_cb (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  guint16 length;

  g_assert (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  priv->last_input = g_get_monotonic_time ();

  length = gtk_entry_get_text_length (GTK_ENTRY (priv->entry_pin));

  gtk_widget_set_sensitive (priv->btn_submit, length != 0);
}


static void
submit_cb (PhoshLockscreen *self)
{
  PhoshLockscreenClass *klass = PHOSH_LOCKSCREEN_GET_CLASS (self);

  g_return_if_fail (klass->unlock_submit);
  klass->unlock_submit (self);
}


static gboolean
key_press_event_cb (PhoshLockscreen *self, GdkEventKey *event, gpointer data)
{
  PhoshLockscreenPrivate *priv;
  gboolean handled = FALSE;
  gboolean on_unlock_page, with_control;

  g_assert (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  on_unlock_page = phosh_lockscreen_get_page (self) == PHOSH_LOCKSCREEN_PAGE_UNLOCK;
  with_control = event->state & GDK_CONTROL_MASK;

  if (gtk_entry_im_context_filter_keypress (GTK_ENTRY (priv->entry_pin), event)) {
    show_unlock_page (self);
    handled = TRUE;
  } else {
    switch (event->keyval) {
    case GDK_KEY_space:
      show_unlock_page (self);
      handled = TRUE;
      break;
    case GDK_KEY_Escape:
      clear_input (self, TRUE);
      phosh_lockscreen_set_page (self, priv->default_page);
      handled = TRUE;
      break;
    case GDK_KEY_Delete:
    case GDK_KEY_KP_Delete:
    case GDK_KEY_BackSpace:
      if (on_unlock_page == TRUE) {
        clear_input (self, FALSE);
        handled = TRUE;
      }
      break;
    case GDK_KEY_Return:
    case GDK_KEY_ISO_Enter:
    case GDK_KEY_KP_Enter:
      if (on_unlock_page == TRUE) {
        submit_cb (self);
        handled = TRUE;
      }
      break;
    case GDK_KEY_Left:
      if (!on_unlock_page && with_control && hdy_deck_get_can_swipe_back (priv->deck)) {
        hdy_deck_navigate (priv->deck, HDY_NAVIGATION_DIRECTION_BACK);
        handled = TRUE;
      }
      break;
    case GDK_KEY_Right:
      if (!on_unlock_page && with_control && hdy_deck_get_can_swipe_forward (priv->deck)) {
        hdy_deck_navigate (priv->deck, HDY_NAVIGATION_DIRECTION_FORWARD);
        handled = TRUE;
      }
    default:
      /* nothing to do */
      break;
    }
  }

  priv->last_input = g_get_monotonic_time ();
  return handled;
}


static void
wall_clock_notify_cb (PhoshLockscreen *self,
                      GParamSpec      *pspec,
                      PhoshWallClock  *wall_clock)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  const char *time;
  g_autofree char *date = NULL;
  g_autofree char *stripped = NULL;

  time = phosh_wall_clock_get_clock (wall_clock, TRUE);

  stripped = phosh_wall_clock_strip_am_pm (wall_clock, time);
  gtk_label_set_text (GTK_LABEL (priv->lbl_clock), stripped);

  date = phosh_wall_clock_local_date (wall_clock);
  gtk_label_set_label (GTK_LABEL (priv->lbl_date), date);
}


static void
carousel_position_notified_cb (PhoshLockscreen *self,
                               GParamSpec      *pspec,
                               HdyCarousel     *carousel)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  g_clear_handle_id (&priv->idle_timer, g_source_remove);
}

static void
carousel_page_changed_cb (PhoshLockscreen *self,
                          guint            index,
                          HdyCarousel     *carousel)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshOskManager *osk_manager = phosh_shell_get_osk_manager (shell);
  gboolean osk_visible = phosh_osk_manager_get_visible (osk_manager);
  PhoshLockscreenPage page = phosh_lockscreen_get_page (self);

  if (page == PHOSH_LOCKSCREEN_PAGE_UNLOCK) {
    focus_pin_entry (self, osk_visible);

    if (!priv->idle_timer) {
      priv->last_input = g_get_monotonic_time ();
      priv->idle_timer = g_timeout_add_seconds (LOCKSCREEN_IDLE_SECONDS,
                                                (GSourceFunc) keypad_check_idle,
                                                self);
      g_source_set_name_by_id (priv->idle_timer, "[PhoshLockscreen] keypad check");
    }
    if (!priv->require_unlock) {
      g_signal_emit (self, signals[LOCKSCREEN_UNLOCK], 0);
      return;
    }
  } else {
    gtk_widget_set_sensitive (priv->entry_pin, FALSE);
    clear_input (self, TRUE);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_PAGE]);
}


static void
update_active_call (PhoshLockscreen *self, const char *path)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshCall *call;

  g_debug ("New call %s", path);
  g_signal_emit (self, signals[WAKEUP_OUTPUT], 0);

  g_free (priv->active);
  priv->active = g_strdup (path);

  call = phosh_calls_manager_get_call (priv->calls_manager, path);
  g_return_if_fail (PHOSH_IS_CALL (call));

  cui_call_display_set_call (priv->call_display, CUI_CALL (call));
}


static void
on_calls_call_added (PhoshLockscreen *self, const char *path)
{
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  g_return_if_fail (PHOSH_IS_CALLS_MANAGER (priv->calls_manager));

  update_active_call (self, path);

  hdy_deck_set_visible_child (priv->deck, GTK_WIDGET (priv->box_call_display));
}


static void
on_calls_call_removed (PhoshLockscreen *self, const char *path)
{
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (path != NULL);
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  g_debug ("Call %s removed, active: %s", path, priv->active);

  if (g_strcmp0 (path, priv->active))
    return;

  g_clear_pointer (&priv->active, g_free);

  hdy_deck_set_visible_child (priv->deck, GTK_WIDGET (priv->box_info));
}


static GtkWidget *
create_call_notification_row (gpointer item, gpointer data)
{
  PhoshCallNotification *noti = NULL;
  PhoshCall *call = PHOSH_CALL (item);

  noti = phosh_call_notification_new (call);
  return GTK_WIDGET (noti);
}


static void
on_deck_visible_child_changed (PhoshLockscreen *self, GParamSpec *pspec, HdyDeck *deck)
{
  GtkWidget *visible_child;
  PhoshLockscreenPrivate *priv;
  gboolean swipe_forward = TRUE;
  gboolean swipe_back = TRUE;

  g_return_if_fail (HDY_IS_DECK (deck));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  visible_child = hdy_deck_get_visible_child (deck);

  /* Avoid forward swipe to calls page if there's no active call */
  if (visible_child == priv->box_info &&
      phosh_calls_manager_get_active_call_handle (priv->calls_manager) == NULL)
    swipe_forward = FALSE;

  /* Avoid backward swipe to widget-box if there's no plugin */
  if (visible_child == priv->box_info && !phosh_widget_box_has_plugins (priv->widget_box))
    swipe_back = FALSE;

  hdy_deck_set_can_swipe_forward (deck, swipe_forward);
  hdy_deck_set_can_swipe_back (deck, swipe_back);
}


static void
on_deck_transition_running_changed (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  if (hdy_deck_get_transition_running (priv->deck))
    return;

  /* Otherwise we might see stale information */
  /* See https://gitlab.gnome.org/World/Phosh/phosh/-/issues/922 */
  gtk_widget_queue_draw (priv->lbl_clock);
}


static GtkWidget *
create_notification_row (gpointer item, gpointer data)
{
  GtkWidget *row = NULL;
  GtkWidget *frame = NULL;
  const char *action_filters[] = { "X-Phosh-Lockscreen-Actions", NULL };

  row = g_object_new (GTK_TYPE_LIST_BOX_ROW,
                      "activatable", FALSE,
                      "visible", TRUE,
                      NULL);

  frame = phosh_notification_frame_new (FALSE, action_filters);
  phosh_notification_frame_bind_model (PHOSH_NOTIFICATION_FRAME (frame), item);

  gtk_widget_set_visible (frame, TRUE);
  gtk_container_add (GTK_CONTAINER (row), frame);

  return row;
}


static void
on_info_reveal_child_changed (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  gboolean has_info = FALSE;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  has_info |= gtk_revealer_get_reveal_child (priv->rev_notifications);
  has_info |= gtk_revealer_get_reveal_child (priv->rev_call_notifications);
  has_info |= gtk_revealer_get_reveal_child (priv->rev_media_player);

  /* Use small clock if any additional info elements are revealed */
  phosh_util_toggle_style_class (priv->box_datetime,
                                 LOCKSCREEN_SMALL_DATE_AND_TIME_CLASS,
                                 has_info);
}


static void
on_call_notification_activated (PhoshLockscreen *self,
                                GtkListBoxRow   *row,
                                GtkListBox      *list)
{
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  hdy_deck_set_visible_child (priv->deck, GTK_WIDGET (priv->box_call_display));
}



static void
on_call_notifications_items_changed (PhoshLockscreen *self,
                                     guint            position,
                                     guint            removed,
                                     guint            added,
                                     GListModel      *list)
{
  PhoshLockscreenPrivate *priv;
  gboolean is_empty;

  g_return_if_fail (G_IS_LIST_MODEL (list));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  is_empty = !g_list_model_get_n_items (list);

  gtk_revealer_set_reveal_child (priv->rev_call_notifications, !is_empty);
}


static void
on_notification_items_changed (PhoshLockscreen *self,
                               guint            position,
                               guint            removed,
                               guint            added,
                               GListModel      *list)
{
  PhoshLockscreenPrivate *priv;
  gboolean is_empty;
  gboolean show_in_lockscreen;
  gboolean reveal;

  g_return_if_fail (G_IS_LIST_MODEL (list));
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  show_in_lockscreen = g_settings_get_boolean (priv->notification_settings, "show-in-lock-screen");
  is_empty = !g_list_model_get_n_items (list);

  reveal = show_in_lockscreen && !is_empty;

  gtk_revealer_set_reveal_child (priv->rev_notifications, reveal);
}


static void
on_show (PhoshLockscreen *self, gpointer userdata)
{
  PhoshLockscreenPrivate *priv;
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  phosh_lockscreen_set_page (self, priv->default_page);
}


static void
phosh_lockscreen_add_background (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshWayland *wl = phosh_wayland_get_default ();
  struct wl_output *wl_output;

  wl_output = phosh_layer_surface_get_wl_output (PHOSH_LAYER_SURFACE (self));
  priv->background = phosh_lockscreen_bg_new (phosh_wayland_get_zwlr_layer_shell_v1 (wl),
                                              wl_output);
  g_object_bind_property (self, "visible", priv->background, "visible", G_BINDING_SYNC_CREATE);
}


static void
phosh_lockscreen_constructed (GObject *object)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (object);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);
  PhoshWallClock *wall_clock = phosh_wall_clock_get_default ();
  const char *active;
  PhoshNotifyManager *manager;
  PhoshOskManager *osk_manager;
  PhoshShell *shell;
  g_autoptr (GSettings) plugin_settings = NULL;
  g_auto (GStrv) plugins = NULL;

  G_OBJECT_CLASS (phosh_lockscreen_parent_class)->constructed (object);

  /* window properties */
  gtk_window_set_title (GTK_WINDOW (self), "phosh lockscreen");
  gtk_window_set_decorated (GTK_WINDOW (self), FALSE);

  gtk_widget_add_events (GTK_WIDGET (self), GDK_KEY_PRESS_MASK);
  g_signal_connect (G_OBJECT (self),
                    "key_press_event",
                    G_CALLBACK (key_press_event_cb),
                    NULL);
  g_signal_connect (G_OBJECT (self),
                    "show",
                    G_CALLBACK (on_show),
                    NULL);

  g_signal_connect_object (wall_clock,
                           "notify::time",
                           G_CALLBACK (wall_clock_notify_cb),
                           self,
                           G_CONNECT_SWAPPED);
  wall_clock_notify_cb (self, NULL, wall_clock);

  g_signal_connect_object (priv->calls_manager,
                           "call-added",
                           G_CALLBACK (on_calls_call_added),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (priv->calls_manager,
                           "call-removed",
                           G_CALLBACK (on_calls_call_removed),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_list_box_bind_model (GTK_LIST_BOX (priv->list_calls),
                           G_LIST_MODEL (priv->calls_manager),
                           create_call_notification_row,
                           NULL,
                           NULL);
  g_signal_connect_object (G_LIST_MODEL (priv->calls_manager),
                           "items-changed",
                           G_CALLBACK (on_call_notifications_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_call_notifications_items_changed (self, -1, -1, -1, G_LIST_MODEL (priv->calls_manager));
  active = phosh_calls_manager_get_active_call_handle (priv->calls_manager);
  if (active)
    update_active_call (self, active);

  manager = phosh_notify_manager_get_default ();
  priv->notification_settings = g_settings_new (PHOSH_NOTIFICATIONS_SCHEMA_ID);
  gtk_list_box_bind_model (GTK_LIST_BOX (priv->list_notifications),
                           G_LIST_MODEL (phosh_notify_manager_get_list (manager)),
                           create_notification_row,
                           NULL,
                           NULL);
  g_signal_connect_object (phosh_notify_manager_get_list (manager),
                           "items-changed",
                           G_CALLBACK (on_notification_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
  on_notification_items_changed (self, -1, -1, -1,
                                 G_LIST_MODEL (phosh_notify_manager_get_list (manager)));

  shell = phosh_shell_get_default ();
  osk_manager = phosh_shell_get_osk_manager (shell);
  g_object_bind_property (osk_manager, "visible",
                          priv->keypad_revealer, "reveal-child",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  g_signal_connect_object (osk_manager, "notify::visible",
                           G_CALLBACK (on_osk_visibility_changed), self,
                           G_CONNECT_SWAPPED);
  g_object_bind_property (osk_manager, "available",
                          priv->btn_keyboard, "sensitive",
                          G_BINDING_SYNC_CREATE);

  priv->lockscreen_settings = g_settings_new ("sm.puri.phosh.lockscreen");
  g_settings_bind (priv->lockscreen_settings, "shuffle-keypad",
                   priv->keypad, "shuffle",
                   G_SETTINGS_BIND_GET);
  g_settings_bind (priv->lockscreen_settings, "require-unlock",
                   self, "require-unlock",
                   G_SETTINGS_BIND_GET);

  g_object_bind_property (self, "require-unlock",
                          priv->box_unlock, "visible",
                          G_BINDING_SYNC_CREATE);

  plugin_settings = g_settings_new ("sm.puri.phosh.plugins");
  plugins = g_settings_get_strv (plugin_settings, "lock-screen");
  if (plugins)
    phosh_widget_box_set_plugins (PHOSH_WIDGET_BOX (priv->widget_box), plugins);

  on_deck_visible_child_changed (self, NULL, priv->deck);
  on_info_reveal_child_changed (self);

  phosh_lockscreen_add_background (self);
}

static void
deck_back_clicked_cb (GtkWidget       *sender,
                      PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  hdy_deck_set_visible_child (priv->deck, GTK_WIDGET (priv->box_info));
}


static void
deck_forward_clicked_cb (GtkWidget       *sender,
                         PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  hdy_deck_set_visible_child (priv->deck, GTK_WIDGET (priv->box_info));
}


static void
phosh_lockscreen_dispose (GObject *object)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (object);
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  g_clear_object (&priv->brightness.gesture);
  g_clear_object (&priv->notification_settings);
  g_clear_handle_id (&priv->idle_timer, g_source_remove);
  g_clear_object (&priv->calls_manager);
  g_clear_pointer (&priv->active, g_free);
  g_clear_object (&priv->lockscreen_settings);

  g_clear_pointer (&priv->background, phosh_cp_widget_destroy);

  G_OBJECT_CLASS (phosh_lockscreen_parent_class)->dispose (object);
}


static void
phosh_lockscreen_configured (PhoshLayerSurface *layer_surface)
{
  PhoshLockscreen *self = PHOSH_LOCKSCREEN (layer_surface);
  PhoshLockscreenPrivate *priv;
  guint height, margin = 100;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));

  priv = phosh_lockscreen_get_instance_private (self);
  height = phosh_layer_surface_get_configured_height (layer_surface);

  /* Avoid margin on smaller displays */
  if (height < LOCKSCREEN_SMALL_DISPLAY)
    margin = 0;

  gtk_widget_set_margin_top (GTK_WIDGET (priv->box_unlock), margin);

  PHOSH_LAYER_SURFACE_CLASS (phosh_lockscreen_parent_class)->configured (layer_surface);
}


static void
on_unlock_submit (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  const char *input;
  guint16 length;

  g_assert (PHOSH_IS_LOCKSCREEN (self));

  priv = phosh_lockscreen_get_instance_private (self);
  priv->last_input = g_get_monotonic_time ();

  length = gtk_entry_get_text_length (GTK_ENTRY (priv->entry_pin));
  if (length == 0)
    return;

  input = gtk_entry_get_text (GTK_ENTRY (priv->entry_pin));

  phosh_lockscreen_set_unlock_status (self, _("Checking…"));
  gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);

  if (priv->auth == NULL)
    priv->auth = PHOSH_AUTH (phosh_auth_new ());
  phosh_auth_authenticate_async (priv->auth,
                                 input,
                                 NULL,
                                 on_auth_authenticate_ready,
                                 g_object_ref (self));
}


static void
phosh_lockscreen_class_init (PhoshLockscreenClass *klass)
{
  GObjectClass *object_class = (GObjectClass *)klass;
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  PhoshLayerSurfaceClass *layer_surface_class = PHOSH_LAYER_SURFACE_CLASS (klass);

  object_class->constructed = phosh_lockscreen_constructed;
  object_class->dispose = phosh_lockscreen_dispose;

  object_class->set_property = phosh_lockscreen_set_property;
  object_class->get_property = phosh_lockscreen_get_property;

  widget_class->map = phosh_lockscreen_map;

  layer_surface_class->configured = phosh_lockscreen_configured;

  klass->unlock_submit = on_unlock_submit;

  /**
   * PhoshLockscreen:calls-manager:
   *
   * The calls manager handling incoming and active calls.
   */
  props[PROP_CALLS_MANAGER] =
    g_param_spec_object ("calls-manager", "", "",
                         PHOSH_TYPE_CALLS_MANAGER,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

  /**
   * PhoshLockscreen:require-unlock:
   *
   * Require entering PIN or password to unlock. If false, unlock by swiping up.
   */
  props[PROP_REQUIRE_UNLOCK] =
    g_param_spec_boolean ("require-unlock", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshLockscreen:page:
   *
   * The currently active carousel page
   */
  props[PROP_PAGE] =
    g_param_spec_enum ("page", "", "",
                       PHOSH_TYPE_LOCKSCREEN_PAGE,
                       PHOSH_LOCKSCREEN_PAGE_UNLOCK,
                       G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshLockscreen::lockscreen-unlock
   * @self: The #PhoshLockscreen emitting this signal
   *
   * This signal is emitted when authentication was successful and the
   * session should be unlocked.
   */
  signals[LOCKSCREEN_UNLOCK] = g_signal_new ("lockscreen-unlock",
                                             G_TYPE_FROM_CLASS (klass),
                                             G_SIGNAL_RUN_LAST,
                                             0, NULL, NULL, NULL,
                                             G_TYPE_NONE,
                                             0);
  /**
   * PhoshLockscreen::wakeup-output
   * @self: The #PhoshLockscreen emitting this signal
   *
   * Emitted when the output showing the lock screen should be woken
   * up.
   */
  signals[WAKEUP_OUTPUT] = g_signal_new ("wakeup-output",
                                         G_TYPE_FROM_CLASS (klass),
                                         G_SIGNAL_RUN_LAST,
                                         0, NULL, NULL, NULL,
                                         G_TYPE_NONE,
                                         0);

  g_type_ensure (PHOSH_TYPE_KEYPAD);
  g_type_ensure (PHOSH_TYPE_WIDGET_BOX);
  gtk_widget_class_set_css_name (widget_class, "phosh-lockscreen");
  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/lockscreen.ui");
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, carousel);
  gtk_widget_class_bind_template_callback_full (widget_class,
                                                "carousel_position_notified_cb",
                                                G_CALLBACK (carousel_position_notified_cb));
  gtk_widget_class_bind_template_callback_full (widget_class,
                                                "carousel_page_changed_cb",
                                                G_CALLBACK (carousel_page_changed_cb));

  /* main deck */
  gtk_widget_class_bind_template_callback (widget_class, deck_forward_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, deck_forward_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, deck_back_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_deck_visible_child_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_deck_transition_running_changed);

  /* unlock page */
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, box_unlock);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, keypad);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, keypad_revealer);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, entry_pin);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, lbl_unlock_status);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen,
                                                long_press_del_gesture);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, btn_submit);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, btn_keyboard);

  gtk_widget_class_bind_template_callback (widget_class, long_press_del_cb);
  gtk_widget_class_bind_template_callback (widget_class, delete_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, osk_button_clicked_cb);
  gtk_widget_class_bind_template_callback (widget_class, submit_cb);
  gtk_widget_class_bind_template_callback (widget_class, input_changed_cb);

  /* info page */
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, box_info);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, box_datetime);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, lbl_clock);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, lbl_date);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, list_calls);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, list_notifications);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen,
                                                rev_call_notifications);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, rev_media_player);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, rev_notifications);
  gtk_widget_class_bind_template_callback (widget_class, on_call_notification_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_info_reveal_child_changed);
  gtk_widget_class_bind_template_callback (widget_class, show_unlock_page);

  /* plugin page */
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, widget_box);

  /* Call UI */
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, deck);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, box_call_display);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshLockscreen, call_display);
}


static void
phosh_lockscreen_init (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  /* LTR doesn't work for the deck
   * https://gitlab.gnome.org/World/Phosh/phosh/-/issues/1132 */
  gtk_widget_set_direction (GTK_WIDGET (priv->deck), GTK_TEXT_DIR_LTR);

  priv->brightness.gesture = gtk_gesture_zoom_new (GTK_WIDGET (self));
  g_object_connect (priv->brightness.gesture,
                    "swapped-object-signal::begin", on_zoom_gesture_begin, self,
                    "swapped-object-signal::update", on_zoom_gesture_update, self,
                    NULL);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (priv->brightness.gesture),
                                              GTK_PHASE_CAPTURE);
}


GtkWidget *
phosh_lockscreen_new (GType              lockscreen_type,
                      gpointer           layer_shell,
                      gpointer           wl_output,
                      PhoshCallsManager *calls_manager)
{
  g_assert (g_type_is_a (lockscreen_type, phosh_lockscreen_get_type ()));
  return g_object_new (lockscreen_type,
                       "layer-shell", layer_shell,
                       "wl-output", wl_output,
                       "anchor", (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                  ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
                       "layer", ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
                       "kbd-interactivity", TRUE,
                       "exclusive-zone", -1,
                       "namespace", "phosh lockscreen",
                       "calls-manager", calls_manager,
                       NULL);
}

/**
 * phosh_lockscreen_get_page:
 * @self: The `PhoshLockscreen`
 *
 * Returns: The #PhoshLockscreenPage that is currently shown
 */
PhoshLockscreenPage
phosh_lockscreen_get_page (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  guint position;

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN (self), PHOSH_LOCKSCREEN_PAGE_INFO);
  priv = phosh_lockscreen_get_instance_private (self);

  /* Round to nearest page - the "current" page is a somewhat arbitrary concept if the carousel
   * is animating (or being manually swiped) from one page to another. By rounding like this, the
   * key_press_event_cb starts accepting input as soon as at least half the unlock page has swiped
   * in. */
  position = round (hdy_carousel_get_position (HDY_CAROUSEL (priv->carousel)));

  if (position == 0)
    return PHOSH_LOCKSCREEN_PAGE_INFO;

  if (position == 1 && priv->extra_page)
    return PHOSH_LOCKSCREEN_PAGE_EXTRA;

  return PHOSH_LOCKSCREEN_PAGE_UNLOCK;
}

/**
 * phosh_lockscreen_set_page:
 * @self: The `PhoshLockscreen`
 * page: The page to scroll to
 *
 * Scrolls to a specific page in the carousel. The state of the deck
 * isn't changed.
 */
void
phosh_lockscreen_set_page (PhoshLockscreen *self, PhoshLockscreenPage page)
{
  GtkWidget *scroll_to;
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  switch (page) {
  case PHOSH_LOCKSCREEN_PAGE_EXTRA:
    scroll_to = priv->extra_page;
    if (scroll_to)
      break;
    /* there's no extra page set, so ... */
    G_GNUC_FALLTHROUGH;
  case PHOSH_LOCKSCREEN_PAGE_INFO:
    scroll_to = GTK_WIDGET (priv->deck);
    break;
  case PHOSH_LOCKSCREEN_PAGE_UNLOCK:
    scroll_to = priv->box_unlock;
    break;
  default:
    scroll_to = GTK_WIDGET (priv->deck);
    break;
  }

  hdy_carousel_scroll_to (HDY_CAROUSEL (priv->carousel), scroll_to);
}

/**
 * phosh_lockscreen_set_default_page:
 * @self: The `PhoshLockscreen`
 * page: the page to show by default
 *
 * Specifies which page should be shown by default when the lockscreen is made visible. This will
 * also be the page that is shown when the keypad idle timer is reached.
 */
void
phosh_lockscreen_set_default_page (PhoshLockscreen *self, PhoshLockscreenPage page)
{
  PhoshLockscreenPrivate *priv;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  priv->default_page = page;
}

/**
 * phosh_lockscreen_get_pin_entry:
 * @self: The `PhoshLockscreen`
 *
 * Get the current contents of the keypad PIN entry buffer
 *
 * Returns: the contents of the entry buffer
 */
const char*
phosh_lockscreen_get_pin_entry (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_LOCKSCREEN (self), "");
  priv = phosh_lockscreen_get_instance_private (self);
  return gtk_entry_get_text (GTK_ENTRY (priv->entry_pin));
}

/**
 * phosh_lockscreen_clear_pin_entry:
 * @self: The `PhoshLockscreen`
 *
 * Clears the current contents of the keypad PIN entry buffer
 */
void
phosh_lockscreen_clear_pin_entry (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  gtk_editable_delete_text (GTK_EDITABLE (priv->entry_pin), 0, -1);
}

/**
 * phosh_lockscreen_shake_pin_entry:
 * @self: The `PhoshLockscreen`
 *
 * Triggers an animation that shakes the PIN entry left and right for a brief period.
 * After the animation is complete, the PIN entry buffer is cleared. Used to visually indicate
 * authentication errors.
 */
void
phosh_lockscreen_shake_pin_entry (PhoshLockscreen *self)
{
  PhoshLockscreenPrivate *priv;
  GdkFrameClock *clock;
  gint64 now;

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);
  clock = gtk_widget_get_frame_clock (priv->entry_pin);
  now = gdk_frame_clock_get_frame_time (clock);
  gtk_widget_add_tick_callback (GTK_WIDGET (self),
                                shake_entry,
                                g_variant_ref_sink (g_variant_new_int64 (now)),
                                (GDestroyNotify) g_variant_unref);
}

/**
 * phosh_lockscreen_add_extra_page:
 * @self: The `PhoshLockscreen`
 * @widget: The extra #GtkWidget to insert into the lockscreen carousel
 *
 * Inserts a custom widget into the "extra" page of the lockscreen. This page sits in-between the
 * info page and the keypad page. By default, this page does not exist and is not used. Once an
 * extra page is added, it can be navigated to by swiping and also via calls to
 * [method@Lockscreen.set_default_page].
 */
void
phosh_lockscreen_add_extra_page (PhoshLockscreen *self, GtkWidget *widget)
{
  PhoshLockscreenPrivate *priv;
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  priv->extra_page = widget;
  hdy_carousel_insert (HDY_CAROUSEL (priv->carousel), priv->extra_page, 1);
}

/**
 * phosh_lockscreen_set_unlock_status:
 * @self: The `PhoshLockscreen`
 * @status: The status text
 *
 * Sets the text displayed in the unlock status label.
 */
void
phosh_lockscreen_set_unlock_status (PhoshLockscreen *self, const char *status)
{
  PhoshLockscreenPrivate *priv;
  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  priv = phosh_lockscreen_get_instance_private (self);

  gtk_label_set_label (GTK_LABEL (priv->lbl_unlock_status), status);
}

/**
 * phosh_lockscreen_set_bg_image:
 * @self: The lockscrenn
 * @image: The background image to set
 */
void
phosh_lockscreen_set_bg_image (PhoshLockscreen *self, PhoshBackgroundImage *image)
{
  PhoshLockscreenPrivate *priv = phosh_lockscreen_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_LOCKSCREEN (self));
  g_return_if_fail (image == NULL || PHOSH_IS_BACKGROUND_IMAGE (image));

  phosh_lockscreen_bg_set_image (priv->background, image);
}
