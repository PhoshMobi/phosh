/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-phosh-feedback-status-page"

#include "phosh-config.h"

#include "feedback-status-page.h"
#include "feedback-manager.h"
#include "notifications/notify-manager.h"
#include "shell-priv.h"

/**
 * Phosh_feedback_status_page:
 *
 * The status page for the feedback settings
 */

enum {
  PROP_0,
  PROP_DO_NOT_DISTURB,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshFeedbackStatusPage {
  PhoshStatusPage parent;

  GtkSwitch      *dnd_switch;
  GtkImage       *icon;
  gboolean        do_not_disturb;
  char           *prev_profile;
};
G_DEFINE_TYPE (PhoshFeedbackStatusPage, phosh_feedback_status_page, PHOSH_TYPE_STATUS_PAGE)

static gboolean
dnd_to_icon_name (GBinding     *binding,
                  const GValue *from_value,
                  GValue       *to_value,
                  gpointer      user_data)
{
  gboolean do_not_disturb = g_value_get_boolean (from_value);
  const char *icon_name;

  icon_name = do_not_disturb ? "chat-none-symbolic" : "chat-symbolic";
  g_value_set_string (to_value, icon_name);
  return TRUE;
}


static void
set_do_not_disturb (PhoshFeedbackStatusPage *self, gboolean do_not_disturb)
{
  PhoshFeedbackManager *manager = phosh_shell_get_feedback_manager (phosh_shell_get_default ());

  if (self->do_not_disturb == do_not_disturb)
    return;

  g_debug ("Do not disturb: %d", do_not_disturb);
  self->do_not_disturb = do_not_disturb;

  if (self->do_not_disturb) {
    g_free (self->prev_profile);
    self->prev_profile = g_strdup (phosh_feedback_manager_get_profile (manager));
    phosh_feedback_manager_set_profile (manager, "silent");
  } else {
    /* Restore previous profile only if user didn't change it in between */
    if (g_str_equal (phosh_feedback_manager_get_profile (manager), "silent") &&
        self->prev_profile)
      phosh_feedback_manager_set_profile (manager, self->prev_profile);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DO_NOT_DISTURB]);
  g_signal_emit_by_name (self, "done", NULL);
}


static void
on_dnd_row_activated (PhoshFeedbackStatusPage *self)
{
  g_assert (PHOSH_IS_FEEDBACK_STATUS_PAGE (self));
  set_do_not_disturb (self, !self->do_not_disturb);
}


static void
phosh_feedback_status_page_set_property (GObject      *object,
                                         guint         property_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  PhoshFeedbackStatusPage *self = PHOSH_FEEDBACK_STATUS_PAGE (object);

  switch (property_id) {
  case PROP_DO_NOT_DISTURB:
    set_do_not_disturb (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_feedback_status_page_get_property (GObject    *object,
                                         guint       property_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  PhoshFeedbackStatusPage *self = PHOSH_FEEDBACK_STATUS_PAGE (object);

  switch (property_id) {
  case PROP_DO_NOT_DISTURB:
    g_value_set_boolean (value, self->do_not_disturb);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_feedback_status_page_finalize (GObject *object)
{
  PhoshFeedbackStatusPage *self = PHOSH_FEEDBACK_STATUS_PAGE (object);

  g_clear_pointer (&self->prev_profile, g_free);

  G_OBJECT_CLASS (phosh_feedback_status_page_parent_class)->finalize (object);
}


static void
phosh_feedback_status_page_class_init (PhoshFeedbackStatusPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_feedback_status_page_finalize;
  object_class->get_property = phosh_feedback_status_page_get_property;
  object_class->set_property = phosh_feedback_status_page_set_property;


  props[PROP_DO_NOT_DISTURB] =
    g_param_spec_boolean ("do-not-disturb", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/feedback-status-page.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshFeedbackStatusPage, dnd_switch);
  gtk_widget_class_bind_template_child (widget_class, PhoshFeedbackStatusPage, icon);

  gtk_widget_class_bind_template_callback (widget_class, on_dnd_row_activated);
}


static void
phosh_feedback_status_page_init (PhoshFeedbackStatusPage *self)
{
  g_autoptr (GSettings) settings = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  g_object_bind_property (self, "do-not-disturb",
                          self->dnd_switch, "active",
                          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

  g_object_bind_property_full (self, "do-not-disturb",
                               self->icon, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               dnd_to_icon_name,
                               NULL, NULL, NULL);

  settings = g_settings_new (PHOSH_NOTIFICATIONS_SCHEMA_ID);
  g_settings_bind (settings, PHOSH_NOTIFICATIONS_KEY_SHOW_BANNERS,
                   self, "do-not-disturb",
                   G_SETTINGS_BIND_DEFAULT | G_SETTINGS_BIND_INVERT_BOOLEAN);
}


PhoshFeedbackStatusPage *
phosh_feedback_status_page_new (void)
{
  return g_object_new (PHOSH_TYPE_FEEDBACK_STATUS_PAGE, NULL);
}
