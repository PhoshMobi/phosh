/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "phosh-config.h"

#include "event-list.h"
#include "calendar-event.h"
#include "upcoming-events.h"

#include "phosh-plugin-upcoming-events-phosh-calendar-dbus.h"

#include <handy.h>
#include <gmobile.h>
#include <glib/gi18n.h>

#define UPCOMING_EVENTS_SCHEMA_ID    "sm.puri.phosh.plugins.upcoming-events"
#define UPCOMING_EVENT_DAYS_KEY      "days"
#define UPCOMING_EVENT_SKIP_DAYS_KEY "skip-empty"

#define EXPAND_LIST_ICON "upcoming-events-all-symbolic"
#define SHRINK_LIST_ICON "upcoming-events-skip-empty-symbolic"

/**
 * PhoshUpcomgingEvents:
 *
 * Shows the next upcoming calendar events
 */
struct _PhoshUpcomingEvents {
  GtkBox                         parent;

  PhoshPluginDBusCalendarServer *proxy;
  GCancellable                  *cancel;

  GtkStack                      *stack;
  GtkBox                        *events_box;
  GPtrArray                     *event_lists;
  GListStore                    *events;
  GHashTable                    *event_ids;
  GDateTime                     *since;
  guint                          num_days;
  gboolean                       skip_empty;
  GtkToggleButton               *skip_empty_btn;

  GSettings                     *settings;
  GFileMonitor                  *tz_monitor;
  guint                          today_changed_timeout_id;
};

G_DEFINE_TYPE (PhoshUpcomingEvents, phosh_upcoming_events, GTK_TYPE_BOX);


static void
phosh_upcoming_events_finalize (GObject *object)
{
  PhoshUpcomingEvents *self = PHOSH_UPCOMING_EVENTS (object);

  g_ptr_array_free (self->event_lists, TRUE);
  g_clear_handle_id (&self->today_changed_timeout_id, g_source_remove);
  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->events);
  g_clear_object (&self->settings);
  g_clear_object (&self->tz_monitor);
  g_clear_pointer (&self->event_ids, g_hash_table_unref);
  g_clear_pointer (&self->since, g_date_time_unref);

  G_OBJECT_CLASS (phosh_upcoming_events_parent_class)->finalize (object);
}


static void
on_skip_empty_btn_toggled (GtkToggleButton     *btn,
                           PhoshUpcomingEvents *self)
{
  self->skip_empty = !self->skip_empty;

  g_settings_set_boolean (self->settings, "skip-empty", self->skip_empty);
}


static void
phosh_upcoming_events_class_init (PhoshUpcomingEventsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_upcoming_events_finalize;

  g_type_ensure (PHOSH_TYPE_EVENT_LIST);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/upcoming-events/upcoming-events.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshUpcomingEvents, stack);
  gtk_widget_class_bind_template_child (widget_class, PhoshUpcomingEvents, events_box);
  gtk_widget_class_bind_template_child (widget_class, PhoshUpcomingEvents, skip_empty_btn);

  gtk_widget_class_bind_template_callback (widget_class, on_skip_empty_btn_toggled);

  gtk_widget_class_set_css_name (widget_class, "phosh-upcoming-events");
}


static void
on_set_time_range_finish (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      data)
{
  g_autoptr (GError) err = NULL;
  PhoshPluginDBusCalendarServer *proxy = PHOSH_PLUGIN_DBUS_CALENDAR_SERVER (source_object);
  gboolean success;

  success = phosh_plugin_dbus_calendar_server_call_set_time_range_finish (proxy, res, &err);
  if (!success) {
    g_warning ("Failed to set time range: %s", err->message);
    return;
  }
}


static void
load_events (PhoshUpcomingEvents *self, gboolean force_reload)
{
  g_autoptr (GDateTime) until = NULL;
  g_autofree char *since_str = g_date_time_format_iso8601 (self->since);
  g_autofree char *until_str = NULL;

  until = g_date_time_add_days (self->since, self->num_days);
  until_str = g_date_time_format_iso8601 (until);

  g_debug ("Requesting events from %s to %s", since_str, until_str);

  phosh_plugin_dbus_calendar_server_call_set_time_range (self->proxy,
                                                         g_date_time_to_unix (self->since),
                                                         g_date_time_to_unix (until),
                                                         force_reload,
                                                         self->cancel,
                                                         on_set_time_range_finish,
                                                         self);
}


static gint
calendar_event_begin_compare (gconstpointer a,
                              gconstpointer b,
                              gpointer      user_data)
{
  PhoshCalendarEvent *ca = PHOSH_CALENDAR_EVENT ((gpointer)a);
  PhoshCalendarEvent *cb = PHOSH_CALENDAR_EVENT ((gpointer)b);

  return g_date_time_compare (phosh_calendar_event_get_begin (ca),
                              phosh_calendar_event_get_begin (cb));
}


static void
update_event_lists_visibility (PhoshUpcomingEvents *self)
{
  int visible_lists = 0;

  for (uint i = 0; i < self->event_lists->len; i++) {
    PhoshEventList *event_list = PHOSH_EVENT_LIST (self->event_lists->pdata[i]);
    uint events = phosh_event_list_get_n_events (event_list);
    gboolean visibility = !self->skip_empty || events != 0;

    gtk_widget_set_visible (GTK_WIDGET (event_list), visibility);
    visible_lists += visibility;
  }

  if (!self->skip_empty || visible_lists)
    gtk_stack_set_visible_child_name (self->stack, "events-window");
  else
    gtk_stack_set_visible_child_name (self->stack, "no-events");
}


#define EVENT_FORMAT "(&s&sxx@a{sv})"

static void
on_events_added_or_updated (PhoshUpcomingEvents *self, GVariant *events)
{
  GVariantIter iter;
  gint64 begin, end;
  const char *id, *summary;
  GVariant *extra_dict;
  gboolean changed = FALSE;

  g_variant_iter_init (&iter, events);
  while (g_variant_iter_next (&iter, EVENT_FORMAT, &id, &summary, &begin, &end, &extra_dict)) {
    PhoshCalendarEvent *event;
    g_auto (GVariantDict) dict = G_VARIANT_DICT_INIT (extra_dict);
    const char *color;

    if (g_variant_dict_lookup (&dict, "color", "&s", &color) == FALSE)
      color = "#ffffff";

    event = g_hash_table_lookup (self->event_ids, id);
    if (event) {
      g_object_set (event,
                    "summary", summary,
                    "begin", g_date_time_new_from_unix_local (begin),
                    "end", g_date_time_new_from_unix_local (end),
                    "color", color,
                    NULL);
      changed = TRUE;
      continue;
    }

    event = phosh_calendar_event_new (id,
                                      summary,
                                      g_date_time_new_from_unix_local (begin),
                                      g_date_time_new_from_unix_local (end),
                                      color);
    g_hash_table_insert (self->event_ids, g_strdup (id), g_object_ref (event));
    g_list_store_insert_sorted (self->events,
                                g_steal_pointer (&event),
                                calendar_event_begin_compare,
                                NULL);
  }

  update_event_lists_visibility (self);

  if (changed == FALSE)
    return;

  /* Changed events might be tz change so refilter days */
  for (int i = 0; i < self->event_lists->len; i++)
    phosh_event_list_set_today (g_ptr_array_index (self->event_lists, i), self->since);
}

#undef EVENT_FORMAT

static void
on_events_removed (PhoshUpcomingEvents *self, GStrv ids)
{
  int removed = 0;

  for (int i = 0; i < g_strv_length (ids); i++) {
    const char *id = ids[i];
    PhoshCalendarEvent *event;
    guint pos;

    event = g_hash_table_lookup (self->event_ids, id);
    if (!event)
      continue;

    if (g_list_store_find (self->events, event, &pos)) {
      g_list_store_remove (self->events, pos);
      removed++;
    } else {
      g_warning ("Found %s in hash but not in list", id);
    }

    g_hash_table_remove (self->event_ids, id);
  }

  g_debug ("Removed %d events of %d", removed, g_strv_length (ids));
}


static void setup_date_change_timeout (PhoshUpcomingEvents *self);


static void
update_calendar (PhoshUpcomingEvents *self, gboolean force_reload)
{
  g_clear_pointer (&self->since, g_date_time_unref);
  self->since = g_date_time_new_now_local ();

  load_events (self, force_reload);

  for (int i = 0; i < self->event_lists->len; i++)
    phosh_event_list_set_today (g_ptr_array_index (self->event_lists, i), self->since);

  /* Rearm timer */
  setup_date_change_timeout (self);
}


static void
on_client_disappeared (PhoshUpcomingEvents *self, const char *client_id)
{
  g_debug ("Client %s gone", client_id);

  /* Update the whole calendar */
  g_list_store_remove_all (self->events);
  g_hash_table_remove_all (self->event_ids);
  update_calendar (self, TRUE);
}


static void
on_today_changed (gpointer data)
{
  PhoshUpcomingEvents *self = PHOSH_UPCOMING_EVENTS (data);

  g_debug ("Date change, reloading events");

  update_calendar (self, FALSE);
}


static void
on_tz_changed (PhoshUpcomingEvents *self,
               GFile               *file,
               GFile               *other_file,
               GFileMonitorEvent   *event,
               GFileMonitor        *monitor)
{
  g_debug ("Timezone changed");

  update_calendar (self, TRUE);
}


static void
setup_date_change_timeout (PhoshUpcomingEvents *self)
{
  g_autoptr (GDateTime) now = NULL;
  g_autoptr (GDateTime) nextday = NULL;
  g_autoptr (GDateTime) tomorrow = NULL;
  guint seconds;
  GTimeSpan span;

  now = g_date_time_new_now_local ();
  nextday = g_date_time_add_days (now, 1);
  tomorrow = g_date_time_new (g_date_time_get_timezone (nextday),
                              g_date_time_get_year (nextday),
                              g_date_time_get_month (nextday),
                              g_date_time_get_day_of_month (nextday),
                              0,
                              0,
                              0.0);
  span = g_date_time_difference (tomorrow, now);

  seconds = 1 + (span / G_TIME_SPAN_SECOND);

  g_debug ("Arming day change timer for %d seconds", seconds);
  self->today_changed_timeout_id = gm_timeout_add_seconds_once (seconds,
                                                                on_today_changed,
                                                                self);
}


static void
on_skip_empty_changed (PhoshUpcomingEvents *self)
{
  const char *icon_name;

  self->skip_empty = g_settings_get_boolean (self->settings, UPCOMING_EVENT_SKIP_DAYS_KEY);
  icon_name = self->skip_empty ? EXPAND_LIST_ICON : SHRINK_LIST_ICON;

  gtk_button_set_image (GTK_BUTTON (self->skip_empty_btn),
                        gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON));

  update_event_lists_visibility (self);
}


static void
on_num_days_changed (PhoshUpcomingEvents *self)
{
  g_autofree char *desc = NULL;
  HdyStatusPage *no_events = HDY_STATUS_PAGE (gtk_stack_get_child_by_name (self->stack,
                                                                           "no-events"));

  self->num_days = g_settings_get_uint (self->settings, UPCOMING_EVENT_DAYS_KEY);
  desc = g_strdup_printf (_("No events for the next %d days"), self->num_days);

  hdy_status_page_set_description (no_events, desc);

  g_debug ("Number of days changed to %u; reconfiguring event lists", self->num_days);

  for (int i = 0; i < self->event_lists->len; i++) {
    GtkWidget *child = g_ptr_array_index (self->event_lists, i);
    gtk_container_remove (GTK_CONTAINER (self->events_box), child);
  }
  g_ptr_array_remove_range (self->event_lists, 0, self->event_lists->len);

  for (int i = 0; i < self->num_days; i++) {
    GtkWidget *event_list = g_object_new (PHOSH_TYPE_EVENT_LIST,
                                          "day-offset", i,
                                          "today", self->since,
                                          "model", self->events,
                                          "visible", TRUE,
                                          NULL);
    gtk_container_add (GTK_CONTAINER (self->events_box), event_list);
    g_ptr_array_add (self->event_lists, event_list);
  }

  load_events (self, FALSE);
}


static void
on_proxy_new_for_bus_finish (GObject      *source_object,
                             GAsyncResult *res,
                             gpointer      data)
{
  g_autoptr (GError) err = NULL;
  PhoshUpcomingEvents *self;
  PhoshPluginDBusCalendarServer *proxy;

  proxy = phosh_plugin_dbus_calendar_server_proxy_new_for_bus_finish (res, &err);
  if (proxy == NULL) {
    g_warning ("Failed to get CalendarServer proxy: %s", err->message);
    return;
  }
  self = PHOSH_UPCOMING_EVENTS (data);
  self->proxy = proxy;

  g_debug ("CalendarServer initialized");
  g_object_connect (self->proxy,
                    "swapped-object-signal::events-added-or-updated",
                    G_CALLBACK (on_events_added_or_updated), self,
                    "swapped-object-signal::events-removed",
                    G_CALLBACK (on_events_removed), self,
                    "swapped-object-signal::client-disappeared",
                    G_CALLBACK (on_client_disappeared), self,
                    NULL);

  on_today_changed (self);
  on_num_days_changed (self);
}


static void
phosh_upcoming_events_init (PhoshUpcomingEvents *self)
{
  g_autoptr (GtkCssProvider) css_provider = NULL;
  g_autoptr (GFile) tz = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new (UPCOMING_EVENTS_SCHEMA_ID);
  g_signal_connect_object (self->settings,
                           "changed::days",
                           G_CALLBACK (on_num_days_changed),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->settings,
                           "changed::skip-empty",
                           G_CALLBACK (on_skip_empty_changed),
                           self,
                           G_CONNECT_SWAPPED);

  self->event_lists = g_ptr_array_new ();
  self->events = g_list_store_new (PHOSH_TYPE_CALENDAR_EVENT);

  self->event_ids = g_hash_table_new_full (g_str_hash,
                                           g_str_equal,
                                           g_free,
                                           g_object_unref);

  on_skip_empty_changed (self);

  self->cancel = g_cancellable_new ();
  phosh_plugin_dbus_calendar_server_proxy_new_for_bus (
    G_BUS_TYPE_SESSION,
    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
    PHOSH_APP_ID ".CalendarServer",
    PHOSH_DBUS_PATH_PREFIX "/CalendarServer",
    self->cancel,
    on_proxy_new_for_bus_finish,
    self);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider,
                                       "/mobi/phosh/plugins/upcoming-events/stylesheet/common.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (css_provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_icon_theme_add_resource_path (gtk_icon_theme_get_default (),
                                    "/mobi/phosh/plugins/upcoming-events/icons");

  tz = g_file_new_for_path ("/etc/localtime");
  self->tz_monitor = g_file_monitor_file (tz, 0, NULL, NULL);
  g_signal_connect_swapped (self->tz_monitor, "changed", G_CALLBACK (on_tz_changed), self);
}
