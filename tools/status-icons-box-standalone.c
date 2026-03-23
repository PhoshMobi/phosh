/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-status-icons-box-standalone"

#include "status-icon.h"
#include "status-icons-box.h"

#define SPACING 12
#define COLUMNS 4
#define MIN_PIXEL_SIZE 16
#define MAX_PIXEL_SIZE 48

/**
 * A tool to (stress) test the status-icons box.
 *
 * Use "Add Child" button to create a random status-icon.
 *
 * Below the status-icons box, there is a grid of check and delete buttons. The check button sets
 * the visibility of its corresponding status-icon. The delete button removes the status-icon.
 *
 * The status-icons are created in a randomized manner. Each has a different icon size to test the
 * situation of custom status-icons.
 */


static char *ICON_NAMES[] = {"face-angel-symbolic", "face-angry-symbolic", "face-cool-symbolic",
                             "face-devilish-symbolic", "face-smile-symbolic",
                             "face-smirk-symbolic"};


static GtkWidget *
make_child (int i)
{
  int idx = g_random_int_range (0, sizeof (ICON_NAMES) / sizeof (char *));
  int pixel_size = g_random_int_range (MIN_PIXEL_SIZE, MAX_PIXEL_SIZE);
  g_autofree char *label = g_strdup_printf ("%d", i);
  GtkWidget *child = g_object_new (PHOSH_TYPE_STATUS_ICON,
                                   "icon-name", ICON_NAMES[idx],
                                   "info", label,
                                   "pixel-size", pixel_size,
                                   "visible", TRUE,
                                   NULL);

  return child;
}


static void
on_del_clicked (GtkWidget *child)
{
  PhoshStatusIconsBox *box = g_object_get_data (G_OBJECT (child), "box");
  GtkContainer *controls_grid = g_object_get_data (G_OBJECT (child), "controls_grid");
  GtkWidget *controls_box = g_object_get_data (G_OBJECT (child), "controls_box");

  gtk_container_remove (controls_grid, controls_box);
  phosh_status_icons_box_remove (box, child);
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
  GtkWidget *del_btn = g_object_new (GTK_TYPE_BUTTON, "image", del_img, NULL);

  g_object_bind_property (check, "active", child, "visible", G_BINDING_SYNC_CREATE);
  g_signal_connect_object (del_btn, "clicked", G_CALLBACK (on_del_clicked), child,
                           G_CONNECT_SWAPPED);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), TRUE);

  gtk_container_add (GTK_CONTAINER (box), check);
  gtk_container_add (GTK_CONTAINER (box), del_btn);

  gtk_widget_set_visible (box, TRUE);
  gtk_widget_set_visible (check, TRUE);
  gtk_widget_set_visible (del_btn, TRUE);

  return box;
}


static void
on_add_clicked (PhoshStatusIconsBox *box)
{
  static int i = 0;
  GtkGrid *controls_grid = g_object_get_data (G_OBJECT (box), "controls_grid");
  GtkWidget *child = make_child (i);
  GtkWidget *controls_box = make_controls_box (child, i);

  g_object_set_data (G_OBJECT (child), "box", box);
  g_object_set_data (G_OBJECT (child), "controls_grid", controls_grid);
  g_object_set_data (G_OBJECT (child), "controls_box", controls_box);

  phosh_status_icons_box_append (box, child);
  gtk_grid_attach (GTK_GRID (controls_grid), controls_box, i % COLUMNS, i / COLUMNS, 1, 1);
  i += 1;
}


static GtkWidget *
make_ui (void)
{
  GtkWidget *root_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, SPACING);
  GtkWidget *box = phosh_status_icons_box_new (SPACING);
  GtkWidget *controls_grid = g_object_new (GTK_TYPE_GRID, "column-homogeneous", TRUE, NULL);
  GtkWidget *add_btn = gtk_button_new_with_label ("Add Child");
  GtkWidget *spacing_lbl = gtk_label_new ("Spacing");
  GtkWidget *spacing_spin = gtk_spin_button_new_with_range (0, G_MAXUINT, 1);

  g_object_set_data (G_OBJECT (box), "controls_grid", controls_grid);
  g_signal_connect_object (add_btn, "clicked", G_CALLBACK (on_add_clicked), box, G_CONNECT_SWAPPED);
  g_object_bind_property (spacing_spin, "value", box, "spacing", G_BINDING_SYNC_CREATE);
  gtk_spin_button_set_value (GTK_SPIN_BUTTON (spacing_spin), SPACING);

  gtk_widget_set_visible (root_box, TRUE);
  gtk_widget_set_visible (box, TRUE);
  gtk_widget_set_visible (controls_grid, TRUE);
  gtk_widget_set_visible (add_btn, TRUE);
  gtk_widget_set_visible (spacing_lbl, TRUE);
  gtk_widget_set_visible (spacing_spin, TRUE);

  gtk_container_add (GTK_CONTAINER (root_box), box);
  gtk_container_add (GTK_CONTAINER (root_box), controls_grid);
  gtk_container_add (GTK_CONTAINER (root_box), add_btn);
  gtk_container_add (GTK_CONTAINER (root_box), spacing_lbl);
  gtk_container_add (GTK_CONTAINER (root_box), spacing_spin);

  return root_box;
}


static void
css_setup (void)
{
  g_autoptr (GtkCssProvider) provider = NULL;
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;

  provider = gtk_css_provider_new ();
  file = g_file_new_for_uri ("resource:///mobi/phosh/stylesheet/adwaita-dark.css");

  if (!gtk_css_provider_load_from_file (provider, file, &error)) {
    g_warning ("Failed to load CSS file: %s", error->message);
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
