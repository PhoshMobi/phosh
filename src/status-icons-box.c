/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-status-icons-box"

#include "phosh-config.h"

#include "revealer.h"
#include "status-icon.h"
#include "status-icons-box.h"

/**
 * PhoshStatusIconsBox:
 *
 * `PhoshStatusIconsBox` displays status-icons in a horizontal layout. It tries to horizontally
 * expand and allocates spaces for children it can fit in the given width. It does this by going
 * through the list of children in the descending order of priority and allocates them the space
 * they demand. Once the available width is less than what the child demands, it stops the
 * allocation.
 *
 * `PhoshStatusIconsBox` is used to display status-icons in the top-panel. As the top-panel has a
 * limited width and scrolling is not okay, it is better to display only the status-icons that can
 * be accomodated in the given width and leave the remaining with an invalid allocation.
 *
 * The main difference between `PhoshStatusIconsBox` and a regular horizontal `GtkBox` is that
 * `GtkBox` tries to allocate space for all its children. So in a limited width, this can cause
 * overflow in the top-panel which will prevent other widgets (like clock, battery etc.) from being
 * displayed.
 *
 * The box automatically wraps the status-icons inside a [class@Phosh.Revealer]. It also accepts
 * status-icons wrapped inside revealers.
 *
 * [property@StatusIconsBox:align] can be used to control the alignment of the box when there is
 * extra space.
 */

enum {
  PROP_0,
  PROP_SPACING,
  PROP_ALIGN,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

struct _PhoshStatusIconsBox {
  GtkContainer parent;

  guint        spacing;
  GtkAlign     align;
  GPtrArray   *children;
  GHashTable  *table;
};

G_DEFINE_TYPE (PhoshStatusIconsBox, phosh_status_icons_box, GTK_TYPE_CONTAINER);


static void
phosh_status_icons_box_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (object);

  switch (property_id) {
  case PROP_SPACING:
    phosh_status_icons_box_set_spacing (self, g_value_get_uint (value));
    break;
  case PROP_ALIGN:
    phosh_status_icons_box_set_align (self, g_value_get_enum (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_status_icons_box_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (object);

  switch (property_id) {
  case PROP_SPACING:
    g_value_set_uint (value, phosh_status_icons_box_get_spacing (self));
    break;
  case PROP_ALIGN:
    g_value_set_enum (value, phosh_status_icons_box_get_align (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static GtkSizeRequestMode
phosh_status_icons_box_get_request_mode (GtkWidget *widget)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (widget);

  g_debug ("%p: Querying for request mode", self);

  return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}


static void
phosh_status_icons_box_get_preferred_width (GtkWidget *widget,
                                            int       *minimum_width,
                                            int       *natural_width)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (widget);

  *minimum_width = 0;
  *natural_width = 0;

  g_debug ("%p: Returning minimum width = %d, natural width = %d",
           self, *minimum_width, *natural_width);
}


static void
phosh_status_icons_box_get_preferred_height (GtkWidget *widget,
                                             int       *minimum_height,
                                             int       *natural_height)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (widget);

  g_debug ("%p: Querying for height", self);

  *minimum_height = 0;
  *natural_height = 0;

  for (int i = 0; i < self->children->len; i++) {
    GtkWidget *child = g_ptr_array_index (self->children, i);
    int child_min_height = 0;
    int child_nat_height = 0;

    if (!gtk_widget_get_visible (child))
      continue;

    gtk_widget_get_preferred_height (child, &child_min_height, &child_nat_height);
    *minimum_height = MAX (child_min_height, *minimum_height);
    *natural_height = MAX (child_min_height, *natural_height);
  }

  g_debug ("%p: Returning minimum height = %d, natural height = %d",
           self, *minimum_height, *natural_height);
}


static void
phosh_status_icons_box_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (widget);
  g_autoptr (GArray) array = g_array_new (TRUE, TRUE, sizeof (int));
  int avail_width;
  g_autofree char *align_nick = g_enum_to_string (GTK_TYPE_ALIGN, self->align);
  GtkAllocation rect;
  gboolean rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
  int start, step, end;

  g_debug ("%p: Got allocation x = %d, y = %d, width = %d, height = %d, "
           "spacing = %d, RTL = %d, align = %s",
           self, allocation->x, allocation->y, allocation->width, allocation->height,
           self->spacing, rtl, align_nick);

  gtk_widget_set_allocation (widget, allocation);

  avail_width = allocation->width;
  for (int i = 0; i < self->children->len; i++) {
    GtkWidget *child = g_ptr_array_index (self->children, i);
    int child_width = 0;
    int min_width = 0;
    int nat_width = 0;

    gtk_widget_get_preferred_width_for_height (child, allocation->height, &min_width, &nat_width);

    if (nat_width == 0) {
      child_width = 0;
      g_array_append_val (array, child_width);
      continue;
    }

    if (nat_width <= avail_width)
      child_width = nat_width;
    else if (min_width <= avail_width)
      child_width = avail_width;
    else
      break;

    g_array_append_val (array, child_width);
    avail_width -= (child_width + self->spacing);
  }
  avail_width += self->spacing;
  g_return_if_fail (avail_width >= 0);

  if (self->align == GTK_ALIGN_CENTER)
    allocation->x = allocation->x + avail_width / 2;
  else if ((self->align == GTK_ALIGN_START && rtl) || (self->align == GTK_ALIGN_END && !rtl))
    allocation->x = allocation->x + avail_width;

  g_debug ("%p: Adjusted available width: %d as per RTL: %d and align: %s to get new x = %d",
           self, avail_width, rtl, align_nick, allocation->x);

  if (rtl) {
    start = array->len - 1;
    step = -1;
    end = -1;
  } else {
    start = 0;
    step = 1;
    end = array->len;
  }

  g_debug ("%p: Starting allocation index = %d as RTL = %d", self, start, rtl);
  rect.x = allocation->x;
  rect.y = allocation->y;
  rect.height = allocation->height;
  for (int i = start; i != end; i += step) {
    GtkWidget *child = g_ptr_array_index (self->children, i);
    rect.width = g_array_index (array, int, i);
    g_debug ("%p: Allocating child %d with x = %d, width = %d", self, i, rect.x, rect.width);
    gtk_widget_size_allocate (child, &rect);
    rect.x += rect.width != 0 ? rect.width + self->spacing : 0;
  }

  rect.x = -1;
  rect.y = -1;
  rect.width = 0;
  rect.height = 0;
  for (int i = array->len; i < self->children->len; i++) {
    GtkWidget *child = g_ptr_array_index (self->children, i);
    gtk_widget_size_allocate (child, &rect);
  }

  g_debug ("%p: Allocated space for %d / %d children",
           self, array->len, self->children->len);
}


static void
phosh_status_icons_box_destroy (GtkWidget *widget)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (widget);

  GTK_WIDGET_CLASS (phosh_status_icons_box_parent_class)->destroy (widget);

  g_clear_pointer (&self->table, g_hash_table_unref);
  g_clear_pointer (&self->children, g_ptr_array_unref);
}


static void
container_add (GtkContainer *container, GtkWidget *widget)
{
  phosh_status_icons_box_append (PHOSH_STATUS_ICONS_BOX (container), widget);
}


static void
container_remove (GtkContainer *container, GtkWidget *widget)
{
  phosh_status_icons_box_remove (PHOSH_STATUS_ICONS_BOX (container), widget);
}


static void
container_forall (GtkContainer *container,
                  gboolean      include_internals,
                  GtkCallback   callback,
                  gpointer      data)
{
  PhoshStatusIconsBox *self = PHOSH_STATUS_ICONS_BOX (container);

  if (self->children == NULL)
    return;

  for (int i = self->children->len - 1; i >= 0; i--)
    callback (g_ptr_array_index (self->children, i), data);
}


static void
on_priority_changed (PhoshStatusIconsBox *self, GParamSpec *spec, PhoshStatusIcon *icon)
{
  int i;
  PhoshRevealer *revealer = NULL;
  int low, high, mid;
  int priority;

  for (i = 0; i < self->children->len; i++) {
    revealer = g_ptr_array_index (self->children, i);
    if (phosh_revealer_get_child (revealer) == GTK_WIDGET (icon))
      break;
  }
  g_return_if_fail (revealer != NULL);
  g_ptr_array_remove_index (self->children, i);

  priority = phosh_status_icon_get_priority (icon);

  g_debug ("%p: Priority of revealer %p's icon %p changed to %d", self, revealer, icon, priority);

  low = 0;
  high = self->children->len;
  while (low < high) {
    PhoshRevealer *other_revealer;
    PhoshStatusIcon *other_status_icon;
    int other_priority = 0;

    mid = (high + low) / 2;

    other_revealer = g_ptr_array_index (self->children, mid);
    other_status_icon = PHOSH_STATUS_ICON (phosh_revealer_get_child (other_revealer));
    if (PHOSH_IS_STATUS_ICON (other_status_icon))
      other_priority = phosh_status_icon_get_priority (other_status_icon);

    if (other_priority > priority) low = mid + 1;
    else high = mid;
  }

  g_debug ("%p: Reordering %p from %d to %d", self, revealer, i, low);
  g_ptr_array_insert (self->children, low, revealer);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}


static void
on_child_changed (PhoshStatusIconsBox *self, GParamSpec *spec, PhoshRevealer *revealer)
{
  GtkWidget *old_child;
  GtkWidget *new_child;

  old_child = g_hash_table_lookup (self->table, revealer);
  if (old_child)
    g_signal_handlers_disconnect_by_func (old_child, on_priority_changed, self);

  new_child = phosh_revealer_get_child (revealer);
  g_hash_table_insert (self->table, revealer, new_child);

  if (new_child == NULL)
    return;

  if (!PHOSH_IS_STATUS_ICON (new_child)) {
    g_warning ("%p: %p's child %p is not a status-icon but %s",
               self, revealer, new_child, gtk_widget_get_name (new_child));
    return;
  }

  g_signal_connect_swapped (new_child, "notify::priority", G_CALLBACK (on_priority_changed), self);
  on_priority_changed (self, NULL, PHOSH_STATUS_ICON (new_child));
}


static void
phosh_status_icons_box_class_init (PhoshStatusIconsBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkContainerClass *container_class = GTK_CONTAINER_CLASS (klass);

  object_class->set_property = phosh_status_icons_box_set_property;
  object_class->get_property = phosh_status_icons_box_get_property;

  widget_class->get_request_mode = phosh_status_icons_box_get_request_mode;
  widget_class->get_preferred_width = phosh_status_icons_box_get_preferred_width;
  widget_class->get_preferred_height = phosh_status_icons_box_get_preferred_height;
  widget_class->size_allocate = phosh_status_icons_box_size_allocate;
  widget_class->destroy = phosh_status_icons_box_destroy;

  container_class->add = container_add;
  container_class->remove = container_remove;
  container_class->forall = container_forall;

  /**
   * PhoshStatusIconsBox:spacing:
   *
   * The spacing between children.
   */
  props[PROP_SPACING] =
    g_param_spec_uint ("spacing", "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshStatusIconsBox:align:
   *
   * The horizontal alignment of children on extra space.
   */
  props[PROP_ALIGN] =
    g_param_spec_enum ("align", "", "",
                       GTK_TYPE_ALIGN, GTK_ALIGN_START,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/status-icons-box.ui");
}


static void
phosh_status_icons_box_init (PhoshStatusIconsBox *self)
{
  self->align = GTK_ALIGN_START;
  self->children = g_ptr_array_new ();
  self->table = g_hash_table_new (NULL, NULL);

  gtk_widget_init_template (GTK_WIDGET (self));
  gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
}


GtkWidget *
phosh_status_icons_box_new (guint spacing)
{
  return g_object_new (PHOSH_TYPE_STATUS_ICONS_BOX, "spacing", spacing, NULL);
}


void
phosh_status_icons_box_set_spacing (PhoshStatusIconsBox *self, guint spacing)
{
  g_return_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self));

  if (self->spacing == spacing)
    return;

  self->spacing = spacing;
  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);
}


guint
phosh_status_icons_box_get_spacing (PhoshStatusIconsBox *self)
{
  g_return_val_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self), 0);

  return self->spacing;
}


void
phosh_status_icons_box_set_align (PhoshStatusIconsBox *self, GtkAlign align)
{
  g_return_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self));

  if (align != GTK_ALIGN_START && align != GTK_ALIGN_CENTER && align != GTK_ALIGN_END) {
    g_autofree char *align_name = g_enum_to_string (GTK_TYPE_ALIGN, align);
    g_autofree char *start_align = g_enum_to_string (GTK_TYPE_ALIGN, GTK_ALIGN_START);
    g_warning ("%p: Unsupported align %s; falling back to %s", self, align_name, start_align);
    align = GTK_ALIGN_START;
  }

  if (self->align == align)
    return;

  self->align = align;
  gtk_widget_queue_resize (GTK_WIDGET (self));

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SPACING]);
}


GtkAlign
phosh_status_icons_box_get_align (PhoshStatusIconsBox *self)
{
  g_return_val_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self), GTK_ALIGN_START);

  return self->align;
}


void
phosh_status_icons_box_append (PhoshStatusIconsBox *self, GtkWidget *child)
{
  PhoshRevealer *revealer;
  PhoshStatusIcon *icon;

  g_return_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self));

  if (PHOSH_IS_REVEALER (child)) {
    revealer = PHOSH_REVEALER (child);
    icon = NULL;
  } else if (PHOSH_IS_STATUS_ICON (child)) {
    revealer = phosh_revealer_new ();
    icon = PHOSH_STATUS_ICON (child);
    phosh_revealer_set_child (revealer, child);
    phosh_revealer_set_show_child (revealer, TRUE);
  } else {
    g_critical ("%p: Invalid child %p of type %s", self, child, gtk_widget_get_name (child));
    return;
  }

  g_ptr_array_add (self->children, revealer);
  g_hash_table_insert (self->table, revealer, icon);

  gtk_widget_set_parent (GTK_WIDGET (revealer), GTK_WIDGET (self));

  g_signal_connect_swapped (revealer, "notify::child", G_CALLBACK (on_child_changed), self);
  on_child_changed (self, NULL, revealer);
}


void
phosh_status_icons_box_remove (PhoshStatusIconsBox *self, GtkWidget *child)
{
  int i;
  PhoshRevealer *revealer = NULL;
  PhoshStatusIcon *icon = NULL;

  g_return_if_fail (PHOSH_IS_STATUS_ICONS_BOX (self));

  for (i = 0; i < self->children->len; i++) {
    revealer = g_ptr_array_index (self->children, i);
    icon = PHOSH_STATUS_ICON (phosh_revealer_get_child (revealer));
    if ((PHOSH_IS_REVEALER (child) && revealer == PHOSH_REVEALER (child)) ||
        (PHOSH_IS_STATUS_ICON (child) && icon == PHOSH_STATUS_ICON (child))) {
      break;
    }
  }

  if (!revealer) {
    g_critical ("%p: %p is not a child of the box", self, child);
    return;
  }

  if (icon)
    g_signal_handlers_disconnect_by_func (icon, on_priority_changed, self);
  g_signal_handlers_disconnect_by_func (revealer, on_child_changed, self);

  gtk_widget_unparent (GTK_WIDGET (revealer));

  g_hash_table_remove (self->table, revealer);
  g_ptr_array_remove_index (self->children, i);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}
