/*
 * Copyright (C) 2023 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-call-notification"

#include "phosh-config.h"

#include "call-notification.h"
#include "calls-manager.h"
#include "util.h"

#include <gmobile.h>

#include <cui-call.h>
#include <handy.h>
#include <glib/gi18n.h>

/**
 * PhoshCallNotification:
 *
 * The notifictaion shown when a call is ongoing.  The call is set at
 * construction time and can't be changed.
 */

enum {
  PROP_0,
  PROP_CALL,
  PROP_ACTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshCallNotification {
  GtkListBoxRow parent;

  HdyAvatar    *avatar;
  GtkLabel     *call_duration;
  GtkLabel     *call_state;
  GtkLabel     *caller;
  GtkLabel     *caller_detail;

  PhoshCall    *call;
  gboolean      active;
};
G_DEFINE_TYPE (PhoshCallNotification, phosh_call_notification, GTK_TYPE_LIST_BOX_ROW)


static void
on_caller_info_changed (PhoshCallNotification *self, GParamSpec *pspec, PhoshCall *call)
{
  const char *caller, *caller_detail;
  const char *display_name = cui_call_get_display_name (CUI_CALL (call));
  const char *id = cui_call_get_id (CUI_CALL (call));

  g_debug ("%s %s", display_name, id);
  if (gm_str_is_null_or_empty (display_name)) {
    caller_detail = NULL;
    caller = gm_str_is_null_or_empty (id) ? _("Unknown caller") : id;
  } else {
    caller = display_name;
    caller_detail = id;
  }

  gtk_label_set_label (self->caller, caller);
  gtk_label_set_label (self->caller_detail, caller_detail);
}


static gboolean
transform_label_to_visible (GBinding     *binding,
                            const GValue *from_value,
                            GValue       *to_value,
                            gpointer      user_data)
{
  const char *label = g_value_get_string (from_value);
  gboolean visible = TRUE;

  /* Hide details for unknown callers so the display name is centered */
  if (gm_str_is_null_or_empty (label))
    visible = FALSE;

  g_value_set_boolean (to_value, visible);
  return TRUE;
}


static gboolean
transform_display_name_initals (GBinding     *binding,
                                const GValue *from_value,
                                GValue       *to_value,
                                gpointer      user_data)
{
  const char *display_name = g_value_get_string (from_value);
  gboolean show_initials = TRUE;

  /* Don't show initials for unknown callers as it would always be 'UC' */
  if (gm_str_is_null_or_empty (display_name))
    show_initials = FALSE;

  g_value_set_boolean (to_value, show_initials);
  return TRUE;
}


static gboolean
transform_active_time (GBinding     *binding,
                       const GValue *from_value,
                       GValue       *to_value,
                       gpointer      user_data)
{
  double elapsed = g_value_get_double (from_value);

  g_value_take_string (to_value, cui_call_format_duration (elapsed));
  return TRUE;
}


static gboolean
transform_call_state (GBinding     *binding,
                      const GValue *from_value,
                      GValue       *to_value,
                      gpointer      user_data)
{
  PhoshCallState state = g_value_get_enum (from_value);

  g_value_set_string (to_value, cui_call_state_to_string ((CuiCallState)state));
  return TRUE;
}


static void
phosh_call_notification_set_call (PhoshCallNotification *self, PhoshCall *call)
{
  g_return_if_fail (PHOSH_IS_CALL_NOTIFICATION (self));
  g_return_if_fail (PHOSH_IS_CALL (call));
  g_return_if_fail (self->call == NULL);

  g_set_object (&self->call, call);
  g_object_connect (call,
                    "swapped-object-signal::notify::display-name", on_caller_info_changed, self,
                    "swapped-object-signal::notify::id", on_caller_info_changed, self,
                    NULL);
  on_caller_info_changed (self, NULL, call);

  g_object_bind_property (call, "display-name",
                          self->avatar, "text",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (call, "display-name",
                               self->avatar, "show-initials",
                               G_BINDING_SYNC_CREATE,
                               transform_display_name_initals,
                               NULL,
                               NULL,
                               NULL);
  g_object_bind_property (call, "avatar-icon",
                          self->avatar, "loadable-icon",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (call, "active-time",
                               self->call_duration, "label",
                               G_BINDING_SYNC_CREATE,
                               transform_active_time,
                               NULL,
                               NULL,
                               NULL);
  g_object_bind_property_full (call, "state",
                               self->call_state, "label",
                               G_BINDING_SYNC_CREATE,
                               transform_call_state,
                               NULL,
                               NULL,
                               NULL);

  /* Only show detail label when non-empty */
  g_object_bind_property_full (self->caller_detail, "label",
                               self->caller_detail, "visible",
                               G_BINDING_SYNC_CREATE,
                               transform_label_to_visible,
                               NULL,
                               NULL,
                               NULL);
}


static void
phosh_call_notification_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  PhoshCallNotification *self = PHOSH_CALL_NOTIFICATION (object);

  switch (property_id) {
  case PROP_CALL:
    phosh_call_notification_set_call (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_call_notification_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  PhoshCallNotification *self = PHOSH_CALL_NOTIFICATION (object);

  switch (property_id) {
  case PROP_ACTIVE:
    g_value_set_boolean (value, self->active);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_call_notification_dispose (GObject *object)
{
  PhoshCallNotification *self = PHOSH_CALL_NOTIFICATION (object);

  g_clear_object (&self->call);

  G_OBJECT_CLASS (phosh_call_notification_parent_class)->dispose (object);
}


static void
phosh_call_notification_class_init (PhoshCallNotificationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_call_notification_get_property;
  object_class->set_property = phosh_call_notification_set_property;
  object_class->dispose = phosh_call_notification_dispose;

  /**
   * PhoshCallNotification:call:
   *
   * The call tracked by this notification
   */
  props[PROP_CALL] =
    g_param_spec_object ("call", "", "",
                         PHOSH_TYPE_CALL,
                         G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * PhoshCallNotification:active:
   *
   * %TRUE when the notification has an associated call and it is active.
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/call-notification.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshCallNotification, avatar);
  gtk_widget_class_bind_template_child (widget_class, PhoshCallNotification, call_duration);
  gtk_widget_class_bind_template_child (widget_class, PhoshCallNotification, call_state);
  gtk_widget_class_bind_template_child (widget_class, PhoshCallNotification, caller);
  gtk_widget_class_bind_template_child (widget_class, PhoshCallNotification, caller_detail);

  gtk_widget_class_set_css_name (widget_class, "phosh-call-notification");
}


static void
phosh_call_notification_init (PhoshCallNotification *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


PhoshCallNotification *
phosh_call_notification_new (PhoshCall *call)
{
  return g_object_new (PHOSH_TYPE_CALL_NOTIFICATION,
                       "call", call,
                       NULL);
}
