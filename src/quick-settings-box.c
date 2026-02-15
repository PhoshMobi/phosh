/*
 * Copyright (C) 2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-quick-settings-box"

#include "phosh-config.h"

#include "quick-settings-box.h"

#include <math.h>

#define MAX_CHILD_WIDTH 150

/**
 * PhoshQuickSettingsBox:
 *
 * A `PhoshQuickSettingsBox` displays [class@Phosh.QuickSetting] in a responsive grid.
 *
 * It takes care of displaying the quick-settings' status-pages as per
 * [signal@Phosh.QuickSetting::show-status] and [signal@Phosh.QuickSetting::hide-status]. At a time,
 * only zero or one status-page is displayed.
 *
 * Use [property@Phosh.QuickSettingsBox:can-show-status] to temporarily prevent displaying of status
 * widgets, like in a lock-screen.
 *
 * Due to the inclusion of status-pages, the box's height grows dynamically. For best usage, it is
 * advised to add the box to a vertically-scrollable parent.
 */

enum {
  PROP_0,
  PROP_MAX_COLUMNS,
  PROP_SPACING,
  PROP_CAN_SHOW_STATUS,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshQuickSettingsBox {
  GtkContainer       parent;

  guint              max_columns;
  guint              spacing;
  gboolean           can_show_status;

  GPtrArray         *children;
  GtkRevealer       *revealer;
  PhoshQuickSetting *shown_child;
  PhoshQuickSetting *to_show_child;
};

G_DEFINE_TYPE (PhoshQuickSettingsBox, phosh_quick_settings_box, GTK_TYPE_CONTAINER);


static void
phosh_quick_settings_box_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (object);

  switch (property_id) {
  case PROP_MAX_COLUMNS:
    phosh_quick_settings_box_set_max_columns (self, g_value_get_uint (value));
    break;
  case PROP_SPACING:
    phosh_quick_settings_box_set_spacing (self, g_value_get_uint (value));
    break;
  case PROP_CAN_SHOW_STATUS:
    phosh_quick_settings_box_set_can_show_status (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_quick_settings_box_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (object);

  switch (property_id) {
  case PROP_MAX_COLUMNS:
    g_value_set_uint (value, phosh_quick_settings_box_get_max_columns (self));
    break;
  case PROP_SPACING:
    g_value_set_uint (value, phosh_quick_settings_box_get_spacing (self));
    break;
  case PROP_CAN_SHOW_STATUS:
    g_value_set_boolean (value, phosh_quick_settings_box_get_can_show_status (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_quick_settings_box_destroy (GtkWidget *widget)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (widget);

  GTK_WIDGET_CLASS (phosh_quick_settings_box_parent_class)->destroy (widget);

  if (self->children != NULL) {
    g_ptr_array_free (self->children, TRUE);
    self->children = NULL;
  }

  if (self->revealer != NULL) {
    gtk_widget_unparent (GTK_WIDGET (self->revealer));
    self->revealer = NULL;
  }
}


/*
 * Allocation Algorithm:
 *
 * 1. Structure:
 * - PhoshQuickSettingsBox has a list of PhoshQuickSetting and a revealer.
 * - The revealer is an internal child, which is used to show the status-pages.
 * - Given a rectangular region, our aim is to display all the (visible) quick-settings as a grid,
 *   whose maximum number of columns is the box's columns property and whose rows and columns are
 *   separated by box's spacing property.
 * - Whenever a child wants to show its status-page, we add it to our revealer and reveal the
 *   revealer below the child's row.
 * - Whenever we mention children, we mean only the visible children.
 * - By height of the quick-settings, we mean the maximum of height of all quick-settings.
 * - Same for width, except that the width is capped by `MAX_CHILD_WIDTH` to prevent lengthy labels
 *   from forcing single column.
 *
 * 2. Spacing
 * - 1 child has 0 spacing.
 * - 2 children have 1 spacing.
 * - 3 children have 2 spacing.
 * - N children have N - 1 spacing.
 * - In general, the value of a measurement (height or width) =
 *   number of fields (rows or columns) * value of child + (number of fields - 1) * spacing.
 * - Or in a concise form =
 *   number of fields * (value of child + spacing) - spacing.
 * - To keep the rest of algorithm description simple, we use "total spacing" to refer to the total
 *   spacing among the rows or columns.
 *
 * 3. Request mode
 * - PhoshQuickSettingsBox uses height-for-width request mode.
 * - Gtk asks for our preferred width first.
 * - Then it gives us a width and asks for our preferred height.
 *
 * 4. Preferred width
 * - The preferred width of the box is computed using the natural width of the quick-settings.
 * - The minimum width is just the child width, i.e. equivalent of a single column.
 * - The natural width is number of columns * child width + total spacing.
 * - If the revealer is revealed, then the values are maximum of its minimum and natural width.
 *
 * 5. Preferred height
 * - The preferred height of the box is computed using the natural height and width of the
 *   quick-settings.
 * - We divide the given width with the child's width (with spacing also taken into account).
 * - This gives us the number of columns.
 * - The columns are then reassigned as the minimum of this value and the box's columns property.
 * - The rows are then determined as the number of children / columns.
 * - Both the minimum and natural height are set as number of rows * child height + total spacing.
 * - If the revealer is revealed, then the values are added with its minimum and natural height.
 *
 * 6. Actual allocation
 * - There are two kinds of allocation possible.
 * - Either Gtk gives us equal or more than the height and width we requested.
 * - Or it gives less than what we asked.
 * - This equality is determined in the same way as preferred height.
 * - We use the given allocation's width to compute the number of columns and rows.
 * - Then we check if the allocation's height is sufficient for the number of rows we have.
 *
 * 7. Equal or more
 * - In this case, we divide the extra width by the number of columns and distribute it equally to
 *   each quick-setting.
 * - For height, we get the extra height.
 * - We let the revealer take the minimum of its height and the extra height.
 * - Then the remaining extra height is divided by the number of rows and is then equally
 *   distributed to each quick-setting.
 *
 * 8. Less
 * - In this case, the number of columns is 1 and the number of rows is the
 *   number of children.
 * - Then each quick-setting is given the full allocation width.
 * - Then we subtract the revealer's height from allocation's height.
 * - After this, each quick-setting is given a height that is determined by dividing this new height
 *   by the number of rows (with spacing also taken into account).
 *
 * 9. Revealing status-pages
 * - Revealing status-page may not be immediate.
 * - If the box is not showing any status-page, then any request to show status-page, happens
 *   immediately.
 * - But if the box is already showing a status-page, then a request from other child to show its
 *   status-page gets queued.
 * - By queue, we mean that the new child is stored in `to_show_child` variable.
 * - We ask the revealer to unreveal itself and wait for it to complete the transition (using
 *   `child-revealed` signal).
 * - Then we ask the existing child to hide its status-page.
 * - Once the transition is complete, we add the next child's status-page and reveal it.
 *
 * 10. Immediate hiding of status-page
 * - For some scenarios, status-page are immediately hidden instead of the usual removal after
 *   transition.
 * - This is done by removing the status-page from the revealer, setting `showing-status` on the
 *   shown child to false and then setting `shown_child` to `NULL`.
 * - After this the revealer's `reveal-child` is set to false.
 * - This immediate removal happens when the shown child's sensitivity or visibility changes,
 *   status-page changes to `NULL` or the child itself is removed from the box.
 * - When the status-page changes to a valid page, we hot-swap the old page with new one
 *   without any transition.
 *
 * 11. Bottom margin on status-pages
 * - We set `spacing` as the bottom margin of status-pages.
 * - To understand why we don't request extra height and allocate spacing from it, consider the
 *   below scenario.
 * - Let's say we have two quick-settings A and B, arranged in a single column.
 * - With A's status-page revealed, the structure will be like: A, spacing, status-page, spacing, B.
 * - Now, when the status-page is unrevealed, it happens gradually.
 * - At the final step of unrevealing, there could be just 1px height of status-page.
 * - But at the same time, the spacing before and after status-page is still there.
 * - Now, when the status-page gets completely hidden, the structure will be like: A, spacing, B.
 * - That is, a `spacing` amount of pixels suddenly disappears.
 * - As the value of `spacing` increases, the visual flicker caused by this disappearance gets more
 *   noticeable.
 * - To avoid this, we need to make the spacing also disappear gradually.
 * - An easy way to do it is by making that spacing part of margin of the status-page.
 * - This way, the revealer will ensure that entire structure of status-page with bottom margin
 *   reveals and unreveals smoothly.
 */

static int
compute_child_height (PhoshQuickSettingsBox *self)
{
  int height = 0;
  int nat_height = 0;

  for (int i = 0; i < self->children->len; i++) {
    gtk_widget_get_preferred_height (g_ptr_array_index (self->children, i), NULL, &nat_height);
    height = MAX (height, nat_height);
  }

  return height;
}


static int
compute_child_width (PhoshQuickSettingsBox *self)
{
  int width = 0;
  int nat_width = 0;

  for (int i = 0; i < self->children->len; i++) {
    gtk_widget_get_preferred_width (g_ptr_array_index (self->children, i), NULL, &nat_width);
    width = MAX (width, nat_width);
  }

  return MIN (width, MAX_CHILD_WIDTH);
}


static GtkSizeRequestMode
phosh_quick_settings_box_get_request_mode (GtkWidget *widget)
{
  g_debug ("%p: Querying for request mode", widget);
  return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}


static void
phosh_quick_settings_box_get_preferred_width (GtkWidget *widget, int *minimum_width,
                                              int *natural_width)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (widget);
  int len = 0;
  int child_width;
  int cols;

  g_debug ("%p: Querying for preferred width", self);

  for (int i = 0; i < self->children->len; i++)
    if (gtk_widget_get_visible (g_ptr_array_index (self->children, i)))
      len += 1;

  if (len == 0) {
    g_debug ("%p: No visible children so preferred width is 0", self);
    *natural_width = 0;
    *minimum_width = 0;
    return;
  }

  g_debug ("%p: Configuration: children = %d/%d\tcolumns = %d\tspacing = %d",
           self, len, self->children->len, self->max_columns, self->spacing);

  child_width = compute_child_width (self);
  cols = self->max_columns;

  g_debug ("%p: Computed child width = %d", self, child_width);

  *minimum_width = child_width;
  *natural_width = cols * (child_width + self->spacing) - self->spacing;

  g_debug ("%p: Computed preferred width: minimum = %d\tnatural = %d",
           self, *minimum_width, *natural_width);

  if (self->shown_child != NULL) {
    int rev_min_width = 0;
    int rev_nat_width = 0;
    gtk_widget_get_preferred_width (GTK_WIDGET (self->revealer), &rev_min_width, &rev_nat_width);
    *minimum_width = MAX (*minimum_width, rev_min_width);
    *natural_width = MAX (*natural_width, rev_nat_width);

    g_debug ("%p: Revealer preferred width: minimum = %d\tnatural = %d",
             self, rev_min_width, rev_nat_width);
    g_debug ("%p: Adjusted preferred width: minimum = %d\tnatural = %d",
             self, *minimum_width, *natural_width);
  }
}


static void
phosh_quick_settings_box_get_preferred_height_for_width (GtkWidget *widget, int width,
                                                         int *minimum_height, int *natural_height)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (widget);
  int len = 0;
  int child_width;
  int child_height;
  int cols;
  int rows;

  g_debug ("%p: Querying preferred height for width = %d", self, width);

  for (int i = 0; i < self->children->len; i++)
    if (gtk_widget_get_visible (g_ptr_array_index (self->children, i)))
      len += 1;

  if (len == 0) {
    g_debug ("%p: No visible children so preferred height is 0", self);
    *natural_height = 0;
    *minimum_height = 0;
    return;
  }

  g_debug ("%p: Configuration: children = %d/%d\tcolumns = %d\tspacing = %d",
           self, len, self->children->len, self->max_columns, self->spacing);

  child_width = compute_child_width (self);
  child_height = compute_child_height (self);
  cols = (int) floor ((float) (width + self->spacing) / (child_width + self->spacing));
  cols = MIN (cols, self->max_columns);
  rows = (int) ceil ((float) len / cols);

  g_debug ("%p: Computed children values: width = %d\theight = %d\tcols = %d\trows = %d",
           self, child_width, child_height, cols, rows);

  g_return_if_fail (cols > 0 && rows > 0);

  *minimum_height = rows * (child_height + self->spacing) - self->spacing;
  *natural_height = *minimum_height;

  g_debug ("%p: Computed preferred height: minimum = %d\tnatural = %d",
           self, *minimum_height, *natural_height);

  if (self->shown_child != NULL) {
    int rev_min_height = 0;
    int rev_nat_height = 0;
    gtk_widget_get_preferred_height_for_width (GTK_WIDGET (self->revealer), width, &rev_min_height,
                                               &rev_nat_height);
    *minimum_height += rev_min_height;
    *natural_height += rev_nat_height;

    g_debug ("%p: Revealer preferred height: minimum = %d\tnatural = %d",
             self, rev_min_height, rev_nat_height);
    g_debug ("%p: Adjusted preferred height: minimum = %d\tnatural = %d",
             self, *minimum_height, *natural_height);
  }
}


static void
allocate_children (PhoshQuickSettingsBox *self,
                   int x, int y, int width, int height,
                   int cols, int rows,
                   int revealer_width, int revealer_height)
{
  int i = 0;
  gboolean show_at_this_row = FALSE;
  GtkAllocation rect;
  gboolean ltr = gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_LTR;

  rect.x = ltr ? x : x + (cols - 1) * (width + self->spacing);
  rect.y = y;

  if (self->shown_child == NULL) {
    rect.width = 0;
    rect.height = 0;
    gtk_widget_size_allocate (GTK_WIDGET (self->revealer), &rect);
  }

  rect.width = width;
  rect.height = height;

  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      while (i < self->children->len) {
        PhoshQuickSetting *child = g_ptr_array_index (self->children, i);
        i += 1;
        if (gtk_widget_get_visible (GTK_WIDGET (child))) {
          gtk_widget_size_allocate (GTK_WIDGET (child), &rect);
          if (self->shown_child == child)
            show_at_this_row = TRUE;
          break;
        }
      }
      if (ltr)
        rect.x += width + self->spacing;
      else
        rect.x -= width + self->spacing;
    }
    rect.x = ltr ? x : x + (cols - 1) * (width + self->spacing);
    rect.y += height + self->spacing;

    if (show_at_this_row) {
      rect.x = x;
      rect.height = revealer_height;
      rect.width = revealer_width;
      gtk_widget_size_allocate (GTK_WIDGET (self->revealer), &rect);
      rect.x = ltr ? x : x + (cols - 1) * (width + self->spacing);
      rect.height = height;
      rect.width = width;
      rect.y += revealer_height;
      show_at_this_row = FALSE;
    }
  }
}


static void
phosh_quick_settings_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (widget);
  int len = 0;
  int child_width;
  int child_height;
  int cols;
  int rows;
  int total_width;
  int total_height;
  int extra_space;
  int revealer_width = 0;
  int revealer_height;

  g_debug ("%p: Doing size allocation", self);

  GTK_WIDGET_CLASS (phosh_quick_settings_box_parent_class)->size_allocate (widget, allocation);

  for (int i = 0; i < self->children->len; i++)
    if (gtk_widget_get_visible (g_ptr_array_index (self->children, i)))
      len += 1;

  if (len == 0) {
    g_debug ("%p: Exiting allocation as there are no visible children", self);
    return;
  }

  g_debug ("%p: Allocation: x = %d\ty = %d\twidth = %d\theight = %d",
           self, allocation->x, allocation->y, allocation->width, allocation->height);

  if (self->shown_child) {
    gtk_widget_get_preferred_height_for_width (GTK_WIDGET (self->revealer),
                                               allocation->width, NULL,
                                               &revealer_height);
  } else {
    revealer_height = 0;
  }

  child_width = compute_child_width (self);
  child_height = compute_child_height (self);

  g_debug ("%p: Before: revealer width = %d\trevealer height = %d\tchild width = %d\t"
           "child height = %d",
           self, revealer_width, revealer_height, child_width, child_height);

  g_return_if_fail (allocation->width >= child_width && allocation->height >= child_height);

  cols = (int) floor ((float) (allocation->width + self->spacing) / (child_width + self->spacing));
  cols = MIN (cols, self->max_columns);
  rows = (int) ceil ((float) len / cols);
  g_debug ("%p: cols = %d\trows = %d", self, cols, rows);

  total_width = cols * (child_width + self->spacing) - self->spacing;
  total_height = rows * (child_height + self->spacing) - self->spacing;

  if (total_width <= allocation->width && total_height <= allocation->height) {
    extra_space = allocation->width - total_width;
    child_width += extra_space / cols;
    revealer_width = allocation->width;

    extra_space = allocation->height - total_height;
    revealer_height = MIN (extra_space, revealer_height);
    extra_space = allocation->height - total_height - revealer_height;
    child_height += extra_space / rows;
  } else {
    cols = 1;
    rows = len;
    child_width = allocation->width;
    child_height = (allocation->height - revealer_height + self->spacing) / rows - self->spacing;
    revealer_width = allocation->width;
  }

  g_return_if_fail (child_width >= 0 && child_height >= 0);

  g_debug ("%p: After:  revealer width = %d\trevealer height = %d\tchild width = %d\t"
           "child height = %d",
           self, revealer_width, revealer_height, child_width, child_height);

  allocate_children (self,
                     allocation->x, allocation->y, child_width, child_height,
                     cols, rows,
                     revealer_width, revealer_height);
}

static void
hide_status_page (PhoshQuickSettingsBox *self)
{
  PhoshStatusPage *status_page = phosh_quick_setting_get_status_page (self->shown_child);
  gtk_container_remove (GTK_CONTAINER (self->revealer), GTK_WIDGET (status_page));
  phosh_quick_setting_set_showing_status (self->shown_child, FALSE);
  self->shown_child = NULL;
}


static void
show_status_page (PhoshQuickSettingsBox *self)
{
  PhoshStatusPage *status_page = phosh_quick_setting_get_status_page (self->shown_child);
  gtk_container_add (GTK_CONTAINER (self->revealer), GTK_WIDGET (status_page));
  phosh_quick_setting_set_showing_status (self->shown_child, TRUE);
  gtk_revealer_set_reveal_child (self->revealer, TRUE);
}


static void
on_child_revealed_changed (PhoshQuickSettingsBox *self)
{
  gboolean child_revealed = gtk_revealer_get_child_revealed (self->revealer);

  if (child_revealed)
    return;

  /* shown_child is NULL when the status-page was removed immediately */
  if (self->shown_child != NULL)
    hide_status_page (self);

  if (self->to_show_child == NULL)
    return;

  self->shown_child = self->to_show_child;
  self->to_show_child = NULL;
  show_status_page (self);
}


static void
on_hide_status (PhoshQuickSettingsBox *self, PhoshQuickSetting *child)
{
  if (child != self->shown_child)
    return;

  gtk_revealer_set_reveal_child (self->revealer, FALSE);
}


static void
on_show_status (PhoshQuickSettingsBox *self, PhoshQuickSetting *child)
{
  if (self->shown_child != NULL) {
    self->to_show_child = child;
    gtk_revealer_set_reveal_child (self->revealer, FALSE);
  } else {
    self->shown_child = child;
    show_status_page (self);
  }
}


static void
on_status_page_changed (PhoshQuickSettingsBox *self, GParamSpec *pspec, PhoshQuickSetting *child)
{
  PhoshStatusPage *status_page = phosh_quick_setting_get_status_page (child);

  if (status_page)
    gtk_widget_set_margin_bottom (GTK_WIDGET (status_page), self->spacing);

  if (child == self->shown_child) {
    GtkWidget *existing_status = gtk_bin_get_child (GTK_BIN (self->revealer));
    gtk_container_remove (GTK_CONTAINER (self->revealer), existing_status);

    if (status_page == NULL) {
      phosh_quick_setting_set_showing_status (self->shown_child, FALSE);
      self->shown_child = NULL;
      gtk_revealer_set_reveal_child (self->revealer, FALSE);
    } else {
      gtk_container_add (GTK_CONTAINER (self->revealer), GTK_WIDGET (status_page));
    }
  } else if (child == self->to_show_child && status_page == NULL) {
    self->to_show_child = NULL;
  }
}


static void
depromote_child (PhoshQuickSettingsBox *self, PhoshQuickSetting *child)
{
  if (child == self->shown_child) {
    hide_status_page (self);
    gtk_revealer_set_reveal_child (self->revealer, FALSE);
  } else if (child == self->to_show_child) {
    self->to_show_child = NULL;
  }
}


static void
on_sensitive_changed (PhoshQuickSettingsBox *self, GParamSpec *pspec, PhoshQuickSetting *child)
{
  gboolean sensitive = gtk_widget_get_sensitive (GTK_WIDGET (child));

  if (sensitive)
    return;

  depromote_child (self, child);
}


static void
on_visible_changed (PhoshQuickSettingsBox *self, GParamSpec *pspec, PhoshQuickSetting *child)
{
  gboolean visible = gtk_widget_get_visible (GTK_WIDGET (child));

  if (visible)
    return;

  depromote_child (self, child);
}


static void
container_add (GtkContainer *container, GtkWidget *widget)
{
  phosh_quick_settings_box_add (PHOSH_QUICK_SETTINGS_BOX (container),
                                PHOSH_QUICK_SETTING (widget));
}


static void
container_remove (GtkContainer *container, GtkWidget *widget)
{
  phosh_quick_settings_box_remove (PHOSH_QUICK_SETTINGS_BOX (container),
                                   PHOSH_QUICK_SETTING (widget));
}


static void
phosh_quick_settings_box_forall (GtkContainer *container, gboolean include_internals,
                                 GtkCallback callback, gpointer data)
{
  PhoshQuickSettingsBox *self = PHOSH_QUICK_SETTINGS_BOX (container);

  if (self->children != NULL) {
    g_autoptr (GPtrArray) children = g_ptr_array_copy (self->children, NULL, NULL);

    for (int i = 0; i < children->len; i++)
      callback (g_ptr_array_index (children, i), data);
  }

  if (include_internals)
    callback (GTK_WIDGET (self->revealer), data);
}


static void
phosh_quick_settings_box_class_init (PhoshQuickSettingsBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->set_property = phosh_quick_settings_box_set_property;
  object_class->get_property = phosh_quick_settings_box_get_property;

  widget_class->destroy = phosh_quick_settings_box_destroy;
  widget_class->get_request_mode = phosh_quick_settings_box_get_request_mode;
  widget_class->get_preferred_width = phosh_quick_settings_box_get_preferred_width;
  widget_class->get_preferred_height_for_width = phosh_quick_settings_box_get_preferred_height_for_width;
  widget_class->size_allocate = phosh_quick_settings_box_size_allocate;

  container_class->add = container_add;
  container_class->remove = container_remove;
  container_class->forall = phosh_quick_settings_box_forall;

  /**
   * PhoshQuickSettingsBox:columns:
   *
   * The maximum and the preferred number of columns.
   */
  props[PROP_MAX_COLUMNS] =
    g_param_spec_uint ("max-columns", "", "",
                       1, G_MAXUINT, 3,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSettingsBox:spacing:
   *
   * The spacing between the children.
   */
  props[PROP_SPACING] =
    g_param_spec_uint ("spacing", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSettingsBox:can-show-status:
   *
   * If the box can show status of its children.
   */
  props[PROP_CAN_SHOW_STATUS] =
    g_param_spec_boolean ("can-show-status", "", "",
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/quick-settings-box.ui");

  gtk_widget_class_bind_template_callback (widget_class, on_child_revealed_changed);
  gtk_widget_class_bind_template_child (widget_class, PhoshQuickSettingsBox, revealer);

  gtk_widget_class_set_css_name (widget_class, "phosh-quick-settings-box");
}


static void
phosh_quick_settings_box_init (PhoshQuickSettingsBox *self)
{
  self->max_columns = 3;
  self->can_show_status = TRUE;
  self->children = g_ptr_array_new ();

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
  gtk_widget_set_parent (GTK_WIDGET (self->revealer), GTK_WIDGET (self));
}


GtkWidget *
phosh_quick_settings_box_new (guint max_columns, guint spacing)
{
  return g_object_new (PHOSH_TYPE_QUICK_SETTINGS_BOX,
                       "max-columns", max_columns,
                       "spacing", spacing,
                       NULL);
}


void
phosh_quick_settings_box_set_max_columns (PhoshQuickSettingsBox *self, guint max_columns)
{
  g_return_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self));

  if (self->max_columns == max_columns)
    return;

  self->max_columns = max_columns;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MAX_COLUMNS]);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}


guint
phosh_quick_settings_box_get_max_columns (PhoshQuickSettingsBox *self)
{
  g_return_val_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self), 0);

  return self->max_columns;
}


void
phosh_quick_settings_box_set_spacing (PhoshQuickSettingsBox *self, guint spacing)
{
  g_return_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self));

  if (self->spacing == spacing)
    return;

  self->spacing = spacing;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);

  for (int i = 0; i < self->children->len; i++) {
    PhoshQuickSetting *child = g_ptr_array_index (self->children, i);
    PhoshStatusPage *status_page = phosh_quick_setting_get_status_page (child);
    if (status_page)
      gtk_widget_set_margin_bottom (GTK_WIDGET (status_page), self->spacing);
  }

  gtk_widget_queue_resize (GTK_WIDGET (self));
}


guint
phosh_quick_settings_box_get_spacing (PhoshQuickSettingsBox *self)
{
  g_return_val_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self), 0);

  return self->spacing;
}


void
phosh_quick_settings_box_set_can_show_status (PhoshQuickSettingsBox *self, gboolean can_show_status)
{
  g_return_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self));

  if (self->can_show_status == can_show_status)
    return;

  self->to_show_child = NULL;
  if (self->shown_child != NULL && !can_show_status)
    gtk_revealer_set_reveal_child (self->revealer, FALSE);

  self->can_show_status = can_show_status;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CAN_SHOW_STATUS]);
}


gboolean
phosh_quick_settings_box_get_can_show_status (PhoshQuickSettingsBox *self)
{
  g_return_val_if_fail (PHOSH_IS_QUICK_SETTINGS_BOX (self), FALSE);

  return self->can_show_status;
}


void
phosh_quick_settings_box_hide_status (PhoshQuickSettingsBox *self)
{
  g_return_if_fail (PHOSH_QUICK_SETTINGS_BOX (self));

  on_hide_status (self, self->shown_child);
}


void
phosh_quick_settings_box_add (PhoshQuickSettingsBox *self, PhoshQuickSetting *child)
{
  g_return_if_fail (PHOSH_QUICK_SETTINGS_BOX (self));
  g_return_if_fail (PHOSH_IS_QUICK_SETTING (child));

  g_ptr_array_add (self->children, child);
  gtk_widget_set_parent (GTK_WIDGET (child), GTK_WIDGET (self));

  g_object_bind_property (self, "can-show-status",
                          child, "can-show-status",
                          G_BINDING_SYNC_CREATE);
  g_object_connect (child,
                    "swapped-object-signal::show-status",
                    G_CALLBACK (on_show_status), self,
                    "swapped-object-signal::hide-status",
                    G_CALLBACK (on_hide_status), self,
                    "swapped-object-signal::notify::status-page",
                    G_CALLBACK (on_status_page_changed), self,
                    "swapped-object-signal::notify::sensitive",
                    G_CALLBACK (on_sensitive_changed), self,
                    "swapped-object-signal::notify::visible",
                    G_CALLBACK (on_visible_changed), self,
                    NULL);

  on_status_page_changed (self, NULL, child);
}


void
phosh_quick_settings_box_remove (PhoshQuickSettingsBox *self, PhoshQuickSetting *child)
{
  g_return_if_fail (PHOSH_QUICK_SETTINGS_BOX (self));
  g_return_if_fail (PHOSH_IS_QUICK_SETTING (child));

  if (child == self->shown_child) {
    hide_status_page (self);
    gtk_revealer_set_reveal_child (self->revealer, FALSE);
  } else if (child == self->to_show_child) {
    self->to_show_child = NULL;
  }

  gtk_widget_unparent (GTK_WIDGET (child));
  g_ptr_array_remove (self->children, child);
}
