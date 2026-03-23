/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "revealer.h"
#include "status-icon.h"
#include "status-icons-box.h"


static void
test_phosh_status_icons_box_new (void)
{
  PhoshStatusIconsBox *box;
  int spacing;
  g_autoptr (GList) children = NULL;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);
  g_assert_true (PHOSH_IS_STATUS_ICONS_BOX (box));

  spacing = phosh_status_icons_box_get_spacing (box);
  g_assert_cmpuint (spacing, ==, 0);

  g_assert_finalize_object (box);

  box = PHOSH_STATUS_ICONS_BOX (phosh_status_icons_box_new (0));
  g_object_ref_sink (box);
  g_assert_true (PHOSH_IS_STATUS_ICONS_BOX (box));
  g_assert_finalize_object (box);
}



static void
test_phosh_status_icons_box_set_get_spacing (void)
{
  PhoshStatusIconsBox *box;
  int spacing;
  int got_spacing;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);

  spacing = 12;
  phosh_status_icons_box_set_spacing (box, spacing);
  got_spacing = phosh_status_icons_box_get_spacing (box);
  g_assert_cmpuint (got_spacing, ==, spacing);

  g_assert_finalize_object (box);
}


static void
test_phosh_status_icons_box_append_revealer (void)
{
  PhoshStatusIconsBox *box;
  g_autoptr (GList) children = NULL;
  GtkWidget *status_icon;
  PhoshRevealer *revealer;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);

  status_icon = phosh_status_icon_new ();
  revealer = phosh_revealer_new ();
  phosh_revealer_set_child (revealer, status_icon);
  phosh_status_icons_box_append (box, GTK_WIDGET (revealer));

  children = gtk_container_get_children (GTK_CONTAINER (box));
  g_assert_cmpuint (g_list_length (children), ==, 1);
  g_assert_true (g_list_nth_data (children, 0) == revealer);

  g_assert_finalize_object (box);
}


static void
test_phosh_status_icons_box_append_status_icon (void)
{
  PhoshStatusIconsBox *box;
  g_autoptr (GList) children = NULL;
  GtkWidget *status_icon;
  PhoshRevealer *revealer;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);

  status_icon = phosh_status_icon_new ();
  phosh_status_icons_box_append (box, status_icon);

  children = gtk_container_get_children (GTK_CONTAINER (box));
  g_assert_cmpuint (g_list_length (children), ==, 1);
  revealer = g_list_nth_data (children, 0);
  g_assert_true (phosh_revealer_get_child (revealer) == status_icon);

  g_assert_finalize_object (box);
}


static void
test_phosh_status_icons_box_remove_revealer (void)
{
  PhoshStatusIconsBox *box;
  GtkWidget *status_icon;
  PhoshRevealer *revealer;
  g_autoptr (GList) children = NULL;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);

  status_icon = phosh_status_icon_new ();
  revealer = phosh_revealer_new ();
  phosh_revealer_set_child (revealer, status_icon);
  phosh_status_icons_box_append (box, GTK_WIDGET (revealer));

  phosh_status_icons_box_remove (box, GTK_WIDGET (revealer));

  children = gtk_container_get_children (GTK_CONTAINER (box));
  g_assert_cmpuint (g_list_length (children), ==, 0);

  g_assert_finalize_object (box);
}


static void
test_phosh_status_icons_box_remove_status_icon (void)
{
  PhoshStatusIconsBox *box;
  GtkWidget *status_icon;
  g_autoptr (GList) children = NULL;

  box = g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, NULL);
  g_object_ref_sink (box);

  status_icon = phosh_status_icon_new ();
  phosh_status_icons_box_append (box, GTK_WIDGET (status_icon));

  phosh_status_icons_box_remove (box, GTK_WIDGET (status_icon));

  children = gtk_container_get_children (GTK_CONTAINER (box));
  g_assert_cmpuint (g_list_length (children), ==, 0);

  g_assert_finalize_object (box);
}


int
main (int argc, char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/phosh/status-icons-box/new",
                   test_phosh_status_icons_box_new);
  g_test_add_func ("/phosh/status-icons-box/set_get_spacing",
                   test_phosh_status_icons_box_set_get_spacing);
  g_test_add_func ("/phosh/status-icons-box/append_revealer",
                   test_phosh_status_icons_box_append_revealer);
  g_test_add_func ("/phosh/status-icons-box/append_status_icon",
                   test_phosh_status_icons_box_append_status_icon);
  g_test_add_func ("/phosh/status-icons-box/remove_revealer",
                   test_phosh_status_icons_box_remove_revealer);
  g_test_add_func ("/phosh/status-icons-box/remove_status_icon",
                   test_phosh_status_icons_box_remove_status_icon);

  return g_test_run ();
}
