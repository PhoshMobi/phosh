/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "phosh-plugin-prefs-config.h"

#include "upcoming-events-prefs.h"

#define UPCOMING_EVENTS_SCHEMA_ID "mobi.phosh.plugins.upcoming-events"
#define UPCOMING_EVENTS_DAYS_KEY "days"

/**
 * PhoshUpcomingEventsPrefs:
 *
 * Preferences for upcoming-events plugin.
 */
struct _PhoshUpcomingEventsPrefs {
  AdwPreferencesDialog  parent;

  GtkAdjustment        *adjustment;

  GSettings            *settings;
};

G_DEFINE_TYPE (PhoshUpcomingEventsPrefs, phosh_upcoming_events_prefs, ADW_TYPE_PREFERENCES_DIALOG);


static void
phosh_upcoming_events_prefs_finalize (GObject *object)
{
  PhoshUpcomingEventsPrefs *self = PHOSH_UPCOMING_EVENTS_PREFS (object);

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (phosh_upcoming_events_prefs_parent_class)->finalize (object);
}

static void
phosh_upcoming_events_prefs_class_init (PhoshUpcomingEventsPrefsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_upcoming_events_prefs_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/upcoming-events-prefs/upcoming-events-prefs.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshUpcomingEventsPrefs, adjustment);
}


static void
phosh_upcoming_events_prefs_init (PhoshUpcomingEventsPrefs *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (UPCOMING_EVENTS_SCHEMA_ID);
  gtk_adjustment_set_upper (self->adjustment, G_MAXUINT);
  g_settings_bind (self->settings, UPCOMING_EVENTS_DAYS_KEY,
                   self->adjustment, "value",
                   G_SETTINGS_BIND_DEFAULT);
}
