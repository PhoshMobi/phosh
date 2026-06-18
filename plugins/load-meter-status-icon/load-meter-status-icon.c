/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: johnwa <johnwa@loopgain.net>
 */

#include "load-meter-status-icon.h"

#include <fcntl.h>
#include <ctype.h>
#include <glib/gstdio.h>

#define WIDTH 18
#define HEIGHT 18

#define YAXIS_Y0 5
#define YAXIS_Y1 18

#define XAXIS_X0 1
#define XAXIS_X1 17

#define NUM_SAMPLES 16

#define MAX_BAR_HEIGHT (YAXIS_Y1 - YAXIS_Y0 - 1)

#define INTERVAL 3

/**
 * PhoshLoadMeterStatusIcon:
 *
 * A CPU load meter status icon
 */

struct _PhoshLoadMeterStatusIcon {
  PhoshStatusIcon parent;
  int load_data[NUM_SAMPLES];
  int start_idx;
  GtkDrawingArea *graph;
  guint timeout_id;
};


G_DEFINE_TYPE (PhoshLoadMeterStatusIcon, phosh_load_meter_status_icon,
               PHOSH_TYPE_STATUS_ICON);


/* [cpu] user nice system idle iowait irq softirq steal guest guest_nice */
#define NCPUSTATES 10 /* Number of fields in /proc/stat the we are interested in */
#define IDLE 3

static char*
skip_token (const char* p)
{
  while (isspace (*p)) p++;
  while (*p && !isspace (*p)) p++;
  return (char*)p;
}


static float
get_load (void)
{
  char buffer[256];
  g_autofd int fd = -1;
  int len;
  int total = 0;
  char* p;
  int idle;
  long cp_time[NCPUSTATES];
  static long last[NCPUSTATES];

  fd = open ("/proc/stat", O_RDONLY);
  if (fd == -1) {
    g_warning ("Cannot open /proc/stat: %s", strerror (errno));
    return 0.0;
  }

  len = read (fd, buffer, sizeof (buffer) - 1);
  if (len == -1) {
    g_warning ("read: %s", strerror (errno));
    return 0.0;
  }

  buffer[len] = '\0';

  p = skip_token (buffer);    /* "cpu" */

  for (int i = 0; i < NCPUSTATES; ++i) {
    /* missing fields at the end will read as 0, which is fine for us */
    cp_time[i] = strtoul (p, &p, 0);
    total += cp_time[i] - last[i];
  }

  idle = cp_time[IDLE] - last[IDLE];

  for (int i = 0; i < NCPUSTATES; ++i)
    last[i] = cp_time[i];

  return 1.0f - 1.0f * idle / total;
}


static gboolean
on_timeout (gpointer data)
{
  PhoshLoadMeterStatusIcon *self = PHOSH_LOAD_METER_STATUS_ICON (data);

  self->load_data[self->start_idx] = get_load () * MAX_BAR_HEIGHT;
  self->start_idx++;
  self->start_idx %= NUM_SAMPLES;

  gtk_widget_queue_draw (GTK_WIDGET (self));

  return G_SOURCE_CONTINUE;
}


static void
phosh_load_meter_status_icon_dispose (GObject *object)
{
  PhoshLoadMeterStatusIcon *self = PHOSH_LOAD_METER_STATUS_ICON (object);

  g_clear_handle_id (&self->timeout_id, g_source_remove);

  G_OBJECT_CLASS (phosh_load_meter_status_icon_parent_class)->dispose (object);
}


static gboolean
on_draw (GtkWidget* widget, cairo_t* cr, gpointer data)
{
  PhoshLoadMeterStatusIcon* self = PHOSH_LOAD_METER_STATUS_ICON (data);

  cairo_set_source_rgb (cr, 1, 1, 1);
  cairo_set_line_width (cr, 2);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to (cr, XAXIS_X0, YAXIS_Y1);
  cairo_line_to (cr, XAXIS_X1, YAXIS_Y1);

  cairo_stroke (cr);

  cairo_set_line_width (cr, 1);
  cairo_set_line_cap (cr, CAIRO_LINE_CAP_BUTT);

  for (int i = 0; i < NUM_SAMPLES; ++i) {
    int height = self->load_data[(i + self->start_idx) % NUM_SAMPLES];

    cairo_move_to (cr, i + 1, YAXIS_Y1);
    cairo_line_to (cr, i + 1, YAXIS_Y1 - height);
  }

  cairo_stroke (cr);

  return FALSE;
}


static void
phosh_load_meter_status_icon_class_init (PhoshLoadMeterStatusIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_load_meter_status_icon_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/load-meter-status-icon/si.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshLoadMeterStatusIcon, graph);
  gtk_widget_class_bind_template_callback (widget_class, on_draw);
}


static void
phosh_load_meter_status_icon_init (PhoshLoadMeterStatusIcon *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->graph = GTK_DRAWING_AREA (gtk_drawing_area_new ());

  memset (self->load_data, 0, NUM_SAMPLES * sizeof (int));

  self->start_idx = 0;

  self->timeout_id = g_timeout_add_seconds (INTERVAL, on_timeout, self);
  on_timeout (self);
}
