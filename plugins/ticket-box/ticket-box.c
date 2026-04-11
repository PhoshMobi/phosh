/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ticket.h"
#include "ticket-box.h"
#include "ticket-row.h"

#include <evince-document.h>
#include <evince-view.h>

#define TICKET_BOX_SCHEMA_ID "sm.puri.phosh.plugins.ticket-box"
#define TICKET_BOX_FOLDER_KEY "folder"

/**
 * PhoshTicketBox
 *
 * Show tickets in a folder. For now we do PDF but
 * should add PNG and pass.
 */
struct _PhoshTicketBox {
  GtkBox        parent;

  GFile        *dir;
  char         *ticket_box_path;
  GCancellable *cancel;

  GListStore   *model;
  GtkListBox   *lb_tickets;
  GtkStack     *stack_tickets;

  EvView       *view;
};

G_DEFINE_TYPE (PhoshTicketBox, phosh_ticket_box, GTK_TYPE_BOX);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EvDocument, g_object_unref)

static void
on_row_selected (PhoshTicketBox *self,
                 GtkListBoxRow  *row,
                 GtkListBox     *box)
{
  g_autoptr (GError) err = NULL;
  g_autoptr (PhoshTicket) ticket = NULL;
  g_autoptr (EvDocument) doc = NULL;
  g_autoptr (EvDocumentModel) model = NULL;

  if (row == NULL)
    return;

  g_object_get (row, "ticket", &ticket, NULL);
  g_debug ("row selected: %s", phosh_ticket_get_display_name (ticket));

  doc = ev_document_factory_get_document_for_gfile (phosh_ticket_get_file (ticket),
                                                    EV_DOCUMENT_LOAD_FLAG_NONE,
                                                    self->cancel,
                                                    &err);
  if (doc == NULL) {
    g_warning ("Failed to load %s: %s", phosh_ticket_get_display_name (ticket), err->message);
    return;
  }
  model = ev_document_model_new_with_document (doc);
  ev_view_set_model (self->view, model);

  gtk_stack_set_visible_child_name (self->stack_tickets, "ticket-view");

  gtk_list_box_select_row (box, NULL);
}


static void
on_view_close_clicked (PhoshTicketBox *self)
{
  gtk_stack_set_visible_child_name (self->stack_tickets, "tickets");
}


static void
phosh_ticket_box_finalize (GObject *object)
{
  PhoshTicketBox *self = PHOSH_TICKET_BOX (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  g_clear_object (&self->model);

  g_clear_object (&self->dir);
  g_clear_pointer (&self->ticket_box_path, g_free);

  G_OBJECT_CLASS (phosh_ticket_box_parent_class)->finalize (object);
}


static void
phosh_ticket_box_class_init (PhoshTicketBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_ticket_box_finalize;

  g_type_ensure (EV_TYPE_VIEW);
  g_type_ensure (PHOSH_TYPE_TICKET_ROW);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/ticket-box/ticket-box.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshTicketBox, lb_tickets);
  gtk_widget_class_bind_template_child (widget_class, PhoshTicketBox, stack_tickets);
  gtk_widget_class_bind_template_child (widget_class, PhoshTicketBox, view);
  gtk_widget_class_bind_template_callback (widget_class, on_view_close_clicked);

  gtk_widget_class_set_css_name (widget_class, "phosh-ticket-box");
}


static GtkWidget *
create_ticket_row (gpointer item, gpointer user_data)
{
  PhoshTicket *ticket = PHOSH_TICKET (item);
  GtkWidget *row = phosh_ticket_row_new (ticket);

  return row;
}


static gint
ticket_compare (gconstpointer a, gconstpointer b, gpointer user_data)
{
  g_autoptr (GDateTime) dt_a = phosh_ticket_get_mod_time (PHOSH_TICKET ((gpointer)a));
  g_autoptr (GDateTime) dt_b = phosh_ticket_get_mod_time (PHOSH_TICKET ((gpointer)b));

  return -1 * g_date_time_compare (dt_a, dt_b);
}


static void
on_file_child_enumerated (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  GFile *dir = G_FILE (source_object);
  g_autoptr (GFileEnumerator) enumerator = NULL;
  PhoshTicketBox *self;
  const char *stack_child = "tickets";

  enumerator = g_file_enumerate_children_finish (dir, res, &err);
  if (enumerator == NULL) {
    g_warning ("Failed to list %s", g_file_get_basename (dir));
    return;
  }

  self = PHOSH_TICKET_BOX (user_data);

  while (TRUE) {
    GFile *file;
    GFileInfo *info;
    g_autoptr (PhoshTicket) ticket = NULL;

    if (!g_file_enumerator_iterate (enumerator, &info, &file, self->cancel, &err)) {
      g_warning ("Failed to list contents of ticket dir %s: $%s",
                 self->ticket_box_path,
                 err->message);
      return;
    }

    if (!file)
      break;

    if (g_strcmp0 (g_file_info_get_content_type (info), "application/pdf") != 0)
      continue;

    ticket = phosh_ticket_new (file, info);
    g_list_store_insert_sorted (self->model, ticket, ticket_compare, NULL);
  }

  if (g_list_model_get_n_items (G_LIST_MODEL (self->model)) == 0)
    stack_child = "no-tickets";

  gtk_stack_set_visible_child_name (self->stack_tickets, stack_child);
}


static void
load_tickets (PhoshTicketBox *self)
{
  g_autoptr (GSettings) settings = g_settings_new (TICKET_BOX_SCHEMA_ID);
  g_autofree char *folder = NULL;

  folder = g_settings_get_string (settings, TICKET_BOX_FOLDER_KEY);
  if (folder[0] != '/')
    self->ticket_box_path = g_build_filename (g_get_home_dir (), folder, NULL);
  else
    self->ticket_box_path = g_steal_pointer (&folder);

  self->dir = g_file_new_for_path (self->ticket_box_path);

  /*
   * Since the lockscreen is rebuilt on lock we don't worry about changes in directory contesnt,
   *  should we do, we can add a GFileMonitor later on
   */
  g_file_enumerate_children_async (self->dir,
                                   G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                   G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON ","
                                   G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                   G_FILE_ATTRIBUTE_TIME_MODIFIED ","
                                   G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_LOW,
                                   self->cancel,
                                   on_file_child_enumerated,
                                   self);
}


static void
phosh_ticket_box_init (PhoshTicketBox *self)
{
  g_autoptr (GtkCssProvider) css_provider = NULL;

  ev_init ();

  gtk_widget_init_template (GTK_WIDGET (self));

  self->model = g_list_store_new (PHOSH_TYPE_TICKET);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (css_provider,
                                       "/mobi/phosh/plugins/ticket-box/stylesheet/common.css");
  gtk_style_context_add_provider (gtk_widget_get_style_context (GTK_WIDGET (self)),
                                  GTK_STYLE_PROVIDER (css_provider),
                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gtk_list_box_bind_model (self->lb_tickets,
                           G_LIST_MODEL (self->model),
                           create_ticket_row,
                           NULL,
                           NULL);

  g_signal_connect_swapped (self->lb_tickets,
                            "row-selected",
                            G_CALLBACK (on_row_selected),
                            self);

  load_tickets (self);
}
