/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-quick-settings-box-standalone"

#include "quick-setting.h"
#include "quick-settings-box.h"
#include "status-icon.h"

#define SPACING 12
#define COLUMNS 3

#define MIN_PIXEL_SIZE 32
#define MAX_PIXEL_SIZE 512

/**
 * A tool to (stress) test the quick-settings box.
 *
 * Use "Add Child" button to create a random quick-setting.
 * Use "Can Show Status?" to toggle the box's `can-show-status` property.
 *
 * Below the quick-settings box, there is a grid of check, delete and re-status-page buttons.
 * The check button sets the visibility of its corresponding quick-setting. The delete button
 * removes the quick-setting. The re-status-page button is used to reassign the quick-setting with a
 * status-page or `NULL` randomly.
 *
 * The quick-settings are created in a randomized manner. It may or may not have a status-page.
 * The status-page, if exists, may or may not have a scrolled window (to test how the box
 * handles arbitrary status-pages). Every time a quick-setting wants to show status-page, the
 * image in its status-page grows to a random size (to see how the box handles dynamically sized
 * status-pages).
 */


static void
on_child_clicked (PhoshQuickSetting *child)
{
  phosh_quick_setting_set_active (child, !phosh_quick_setting_get_active (child));
}


static void
on_child_long_pressed (PhoshQuickSetting *child)
{
  GtkWidget *dialog = gtk_message_dialog_new (NULL,
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_CLOSE,
                                              "You long-pressed or right-clicked on a child.");

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
on_show_status (PhoshQuickSetting *child)
{
  GtkImage *image = g_object_get_data (G_OBJECT (child), "image");

  gtk_image_set_pixel_size (image, g_random_int_range (MIN_PIXEL_SIZE, MAX_PIXEL_SIZE));
}


static PhoshStatusPage *
make_status_page (GtkWidget *child)
{
  GtkWidget *image;
  g_autofree char *str = g_strdup_printf ("Status %d", g_random_int ());
  GtkWidget *scrollw;
  PhoshStatusPage *status_page;

  if (g_random_boolean ())
    return NULL;

  image = g_object_new (GTK_TYPE_IMAGE,
                        "icon-name", "face-cool-symbolic",
                        "pixel-size", 16,
                        NULL);
  gtk_widget_set_visible (image, TRUE);
  g_object_set_data (G_OBJECT (child), "image", image);

  status_page = phosh_status_page_new ();
  phosh_status_page_set_title (status_page, str);
  gtk_widget_set_visible (GTK_WIDGET (status_page), TRUE);

  if (g_random_boolean ()) {
    scrollw = gtk_scrolled_window_new (NULL, NULL);
    gtk_widget_set_visible (scrollw, TRUE);
    gtk_container_add (GTK_CONTAINER (scrollw), image);
    gtk_widget_set_hexpand (scrollw, TRUE);
    phosh_status_page_set_content (status_page, scrollw);
  } else {
    gtk_widget_set_hexpand (image, TRUE);
    phosh_status_page_set_content (status_page, image);
  }

  return status_page;
}


static GtkWidget *
make_child (int i)
{
  g_autofree char *label = g_strdup_printf ("%d", i);
  PhoshStatusIcon *status_icon = g_object_new (PHOSH_TYPE_STATUS_ICON,
                                               "icon-name", "face-smile-symbolic",
                                               "info", label,
                                               "visible", TRUE,
                                               NULL);
  GtkWidget *child = phosh_quick_setting_new (NULL);
  PhoshStatusPage *status_page = make_status_page (child);

  phosh_quick_setting_set_status_icon (PHOSH_QUICK_SETTING (child), status_icon);
  phosh_quick_setting_set_status_page (PHOSH_QUICK_SETTING (child), status_page);

  g_signal_connect_object (child, "show-status", G_CALLBACK (on_show_status), NULL,
                           G_CONNECT_DEFAULT);
  g_signal_connect_object (child, "clicked", G_CALLBACK (on_child_clicked), NULL,
                           G_CONNECT_DEFAULT);
  g_signal_connect_object (child, "long-pressed", G_CALLBACK (on_child_long_pressed), NULL,
                           G_CONNECT_DEFAULT);
  return child;
}


static void
on_del_clicked (GtkWidget *child)
{
  GtkWidget *box = g_object_get_data (G_OBJECT (child), "box");
  GtkWidget *controls_grid = g_object_get_data (G_OBJECT (child), "controls_grid");
  GtkWidget *controls_box = g_object_get_data (G_OBJECT (child), "controls_box");

  gtk_container_remove (GTK_CONTAINER (controls_grid), controls_box);
  phosh_quick_settings_box_remove (PHOSH_QUICK_SETTINGS_BOX (box), PHOSH_QUICK_SETTING (child));
}


static void
on_restatus_clicked (GtkWidget *child)
{
  PhoshStatusPage *status_page = make_status_page (child);

  phosh_quick_setting_set_status_page (PHOSH_QUICK_SETTING (child), status_page);
}


static GtkWidget *
make_controls_box (GtkWidget *child, int i)
{
  g_autofree char *label = g_strdup_printf ("%d", i);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *check = gtk_check_button_new_with_label (label);
  GtkWidget *del_img = g_object_new (GTK_TYPE_IMAGE,
                                     "icon-name", "user-trash-symbolic",
                                     "pixel-size", 16,
                                     NULL);
  GtkWidget *restatus_img = g_object_new (GTK_TYPE_IMAGE,
                                          "icon-name", "view-refresh-symbolic",
                                          "pixel-size", 16,
                                          NULL);
  GtkWidget *del_btn = g_object_new (GTK_TYPE_BUTTON, "image", del_img, NULL);
  GtkWidget *restatus_btn = g_object_new (GTK_TYPE_BUTTON, "image", restatus_img, NULL);

  g_object_bind_property (check, "active", child, "visible", G_BINDING_SYNC_CREATE);
  g_signal_connect_object (del_btn, "clicked", G_CALLBACK (on_del_clicked), child,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (restatus_btn, "clicked", G_CALLBACK (on_restatus_clicked), child,
                           G_CONNECT_SWAPPED);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);

  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_container_add (GTK_CONTAINER (box), del_btn);
  gtk_container_add (GTK_CONTAINER (box), restatus_btn);

  gtk_widget_set_visible (box, TRUE);
  gtk_widget_set_visible (check, TRUE);
  gtk_widget_set_visible (del_btn, TRUE);
  gtk_widget_set_visible (restatus_btn, TRUE);

  return box;
}


static void
on_add_clicked (PhoshQuickSettingsBox *box)
{
  static int i = 0;
  GtkWidget *controls_grid = g_object_get_data (G_OBJECT (box), "controls_grid");
  GtkWidget *child = make_child (i);
  GtkWidget *controls_box = make_controls_box (child, i);

  g_object_set_data (G_OBJECT (child), "box", box);
  g_object_set_data (G_OBJECT (child), "controls_grid", controls_grid);
  g_object_set_data (G_OBJECT (child), "controls_box", controls_box);

  phosh_quick_settings_box_add (PHOSH_QUICK_SETTINGS_BOX (box), PHOSH_QUICK_SETTING (child));
  gtk_grid_attach (GTK_GRID (controls_grid), controls_box, i % COLUMNS, i / COLUMNS, 1, 1);
  i += 1;
}


static GtkWidget *
make_ui (void)
{
  GtkWidget *root_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, SPACING);
  GtkWidget *box = phosh_quick_settings_box_new (COLUMNS, SPACING);
  GtkWidget *controls_grid = g_object_new (GTK_TYPE_GRID, "column-homogeneous", TRUE, NULL);
  GtkWidget *add_btn = gtk_button_new_with_label ("Add Child");
  GtkWidget *status_toggle = gtk_toggle_button_new_with_label ("Can Show Status?");
  GtkWidget *max_columns_lbl = gtk_label_new ("Max Columns");
  GtkWidget *max_columns_spin = gtk_spin_button_new_with_range (1, G_MAXUINT, 1);
  GtkWidget *spacing_lbl = gtk_label_new ("Spacing");
  GtkWidget *spacing_spin = gtk_spin_button_new_with_range (0, G_MAXUINT, 1);

  g_object_bind_property (status_toggle, "active", box, "can-show-status", G_BINDING_SYNC_CREATE);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (status_toggle), TRUE);
  g_object_set_data (G_OBJECT (box), "controls_grid", controls_grid);
  g_signal_connect_object (add_btn, "clicked", G_CALLBACK (on_add_clicked), box, G_CONNECT_SWAPPED);
  g_object_bind_property (max_columns_spin, "value", box, "max-columns", G_BINDING_SYNC_CREATE);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (max_columns_spin), COLUMNS);
  g_object_bind_property (spacing_spin, "value", box, "spacing", G_BINDING_SYNC_CREATE);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spacing_spin), SPACING);

  gtk_widget_set_visible (root_box, TRUE);
  gtk_widget_set_visible (box, TRUE);
  gtk_widget_set_visible (controls_grid, TRUE);
  gtk_widget_set_visible (add_btn, TRUE);
  gtk_widget_set_visible (status_toggle, TRUE);
  gtk_widget_set_visible (max_columns_lbl, TRUE);
  gtk_widget_set_visible (max_columns_spin, TRUE);
  gtk_widget_set_visible (spacing_lbl, TRUE);
  gtk_widget_set_visible (spacing_spin, TRUE);

  gtk_container_add (GTK_CONTAINER (root_box), box);
  gtk_container_add (GTK_CONTAINER (root_box), controls_grid);
  gtk_container_add (GTK_CONTAINER (root_box), add_btn);
  gtk_container_add (GTK_CONTAINER (root_box), status_toggle);
  gtk_container_add (GTK_CONTAINER (root_box), max_columns_lbl);
  gtk_container_add (GTK_CONTAINER (root_box), max_columns_spin);
  gtk_container_add (GTK_CONTAINER (root_box), spacing_lbl);
  gtk_container_add (GTK_CONTAINER (root_box), spacing_spin);

  return root_box;
}


static void
css_setup (void)
{
  g_autoptr (GtkCssProvider) provider = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) err = NULL;

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &err)) {
    g_warning ("Failed to load CSS file: %s", err->message);
    return;
  }
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


int
main (int argc, char *argv[])
{
  GtkWidget *win;
  GtkWidget *box;

  gtk_init (&argc, &argv);

  css_setup ();

  g_object_set (gtk_settings_get_default (),
                "gtk-application-prefer-dark-theme", TRUE,
                NULL);

  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (win), "Quick Settings Box");
  gtk_widget_set_visible (win, TRUE);
  g_signal_connect (win, "delete-event", G_CALLBACK (gtk_main_quit), NULL);

  box = make_ui ();
  gtk_container_add (GTK_CONTAINER (win), box);

  gtk_main ();

  return 0;
}
