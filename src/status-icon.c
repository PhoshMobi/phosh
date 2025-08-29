/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Julian Sparber <julian.sparber@puri.sm>
 */

#define G_LOG_DOMAIN "phosh-status-icon"

#include "phosh-config.h"

#include "status-icon.h"

/**
 * PhoshStatusIcon:
 *
 * Base class for status icons used in the Phosh's top-bar or in
 * [type@QuickSetting]s. It's very common to have the same status icon
 * class used for both places.
 *
 * If the widget will be used in a [type@QuickSetting] it is
 * recommended (but not required) that derived classes implement a
 * `enabled` property.
 */

enum {
  PHOSH_STATUS_ICON_PROP_0,
  PHOSH_STATUS_ICON_PROP_ICON_NAME,
  PHOSH_STATUS_ICON_PROP_ICON_SIZE,
  PHOSH_STATUS_ICON_PROP_PIXEL_SIZE,
  PHOSH_STATUS_ICON_PROP_EXTRA_WIDGET,
  PHOSH_STATUS_ICON_PROP_INFO,
  PHOSH_STATUS_ICON_PROP_PRIORITY,
  PHOSH_STATUS_ICON_PROP_LAST_PROP
};
static GParamSpec *props[PHOSH_STATUS_ICON_PROP_LAST_PROP];

typedef struct {
  GtkBox      *box;
  GtkWidget   *image;
  GtkWidget   *extra_widget;
  GtkIconSize  icon_size;
  guint        pixel_size;
  char        *info;
  int          priority;

  guint       idle_id;
} PhoshStatusIconPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshStatusIcon, phosh_status_icon, GTK_TYPE_BIN);


static void
phosh_status_icon_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  PhoshStatusIcon *self = PHOSH_STATUS_ICON (object);

  switch (property_id) {
  case PHOSH_STATUS_ICON_PROP_ICON_NAME:
    phosh_status_icon_set_icon_name (self, g_value_get_string (value));
    break;
  case PHOSH_STATUS_ICON_PROP_ICON_SIZE:
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    phosh_status_icon_set_icon_size (self, g_value_get_enum (value));
    G_GNUC_END_IGNORE_DEPRECATIONS
    break;
  case PHOSH_STATUS_ICON_PROP_PIXEL_SIZE:
    phosh_status_icon_set_pixel_size (self, g_value_get_uint (value));
    break;
  case PHOSH_STATUS_ICON_PROP_EXTRA_WIDGET:
    phosh_status_icon_set_extra_widget (self, g_value_get_object (value));
    break;
  case PHOSH_STATUS_ICON_PROP_INFO:
    phosh_status_icon_set_info (self, g_value_get_string (value));
    break;
  case PHOSH_STATUS_ICON_PROP_PRIORITY:
    phosh_status_icon_set_priority (self, g_value_get_int (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_status_icon_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  PhoshStatusIcon *self = PHOSH_STATUS_ICON (object);

  switch (property_id) {
  case PHOSH_STATUS_ICON_PROP_ICON_NAME:
    g_value_take_string (value, phosh_status_icon_get_icon_name (self));
    break;
  case PHOSH_STATUS_ICON_PROP_ICON_SIZE:
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_value_set_enum (value, phosh_status_icon_get_icon_size (self));
    G_GNUC_END_IGNORE_DEPRECATIONS
    break;
  case PHOSH_STATUS_ICON_PROP_PIXEL_SIZE:
    g_value_set_uint (value, phosh_status_icon_get_pixel_size (self));
    break;
  case PHOSH_STATUS_ICON_PROP_EXTRA_WIDGET:
    g_value_set_object (value, phosh_status_icon_get_extra_widget (self));
    break;
  case PHOSH_STATUS_ICON_PROP_INFO:
    g_value_set_string (value, phosh_status_icon_get_info (self));
    break;
  case PHOSH_STATUS_ICON_PROP_PRIORITY:
    g_value_set_int (value, phosh_status_icon_get_priority (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static gboolean
on_idle (PhoshStatusIcon *self)
{
  PhoshStatusIconClass *klass = PHOSH_STATUS_ICON_GET_CLASS (self);
  PhoshStatusIconPrivate *priv = phosh_status_icon_get_instance_private (self);

  if (klass->idle_init)
    (*klass->idle_init) (self);

  priv->idle_id = 0;
  return G_SOURCE_REMOVE;
}


static void
phosh_status_icon_constructed (GObject *object)
{
  PhoshStatusIcon *self = PHOSH_STATUS_ICON (object);
  PhoshStatusIconPrivate *priv = phosh_status_icon_get_instance_private (self);
  PhoshStatusIconClass *klass = PHOSH_STATUS_ICON_GET_CLASS (self);

  G_OBJECT_CLASS (phosh_status_icon_parent_class)->constructed (object);

  if (klass->idle_init) {
    priv->idle_id = g_idle_add ((GSourceFunc) on_idle, self);
    g_source_set_name_by_id (priv->idle_id, "[PhoshStatusIcon] idle");
  }
}


static void
phosh_status_icon_dispose (GObject *object)
{
  PhoshStatusIcon *self = PHOSH_STATUS_ICON (object);
  PhoshStatusIconPrivate *priv = phosh_status_icon_get_instance_private (self);

  g_clear_handle_id (&priv->idle_id, g_source_remove);

  G_OBJECT_CLASS (phosh_status_icon_parent_class)->dispose (object);
}


static void
phosh_status_icon_finalize (GObject *gobject)
{
  PhoshStatusIconPrivate *priv =
    phosh_status_icon_get_instance_private (PHOSH_STATUS_ICON (gobject));

  g_clear_pointer (&priv->info, g_free);

  G_OBJECT_CLASS (phosh_status_icon_parent_class)->finalize (gobject);
}


static void
phosh_status_icon_destroy (GtkWidget *widget)
{
  PhoshStatusIcon *self = PHOSH_STATUS_ICON (widget);

  phosh_status_icon_set_extra_widget (self, NULL);

  GTK_WIDGET_CLASS (phosh_status_icon_parent_class)->destroy (widget);
}

static void
phosh_status_icon_class_init (PhoshStatusIconClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_status_icon_set_property;
  object_class->get_property = phosh_status_icon_get_property;
  object_class->constructed = phosh_status_icon_constructed;
  object_class->dispose = phosh_status_icon_dispose;
  object_class->finalize = phosh_status_icon_finalize;

  widget_class->destroy = phosh_status_icon_destroy;

  gtk_widget_class_set_css_name (widget_class, "phosh-status-icon");

  /**
   * PhoshStatusIcon:icon-name:
   *
   * The name of the icon to display in the widget
   */
  props[PHOSH_STATUS_ICON_PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshStatusIcon:icon-size:
   *
   * The size of the icon to display in the widget
   */
  props[PHOSH_STATUS_ICON_PROP_ICON_SIZE] =
    g_param_spec_enum ("icon-size", "", "",
                       GTK_TYPE_ICON_SIZE,
                       GTK_ICON_SIZE_LARGE_TOOLBAR,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshStatusIcon:pixel-size:
   *
   * The size of the icon to display in the widget
   */
  props[PHOSH_STATUS_ICON_PROP_PIXEL_SIZE] =
    g_param_spec_uint ("pixel-size", "", "",
                       0, G_MAXUINT, 24,
                       G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshStatusIcon:extra-widget:
   *
   * An extra widget to display. This is used for extra information when
   * used in PhoshTopPanel. When used in [type@QuickSetting] this
   * is not needed.
   */
  props[PHOSH_STATUS_ICON_PROP_EXTRA_WIDGET] =
    g_param_spec_object ("extra_widget", "", "",
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshStatusIcon:info:
   *
   * Textual information to display. Think of it as the [type@StatusIcon]'s
   * label.
   */
  props[PHOSH_STATUS_ICON_PROP_INFO] =
    g_param_spec_string ("info", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  /**
   * PhoshStatusIcon:priority:
   *
   * The priority of icon. Higher priority icons are more likely to be shown when displayed in
   * space-constrained places.
   */
  props[PHOSH_STATUS_ICON_PROP_PRIORITY] =
    g_param_spec_int ("priority", "", "",
                      0, G_MAXINT, 10,
                      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PHOSH_STATUS_ICON_PROP_LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/status-icon.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PhoshStatusIcon, box);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshStatusIcon, image);
}


static void
phosh_status_icon_init (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv = phosh_status_icon_get_instance_private (self);

  gtk_widget_init_template (GTK_WIDGET (self));

  priv->icon_size = GTK_ICON_SIZE_LARGE_TOOLBAR;
  priv->pixel_size = 24;
  priv->priority = 10;
}


GtkWidget *
phosh_status_icon_new (void)
{
  return g_object_new (PHOSH_TYPE_STATUS_ICON, NULL);
}


void
phosh_status_icon_set_icon_name (PhoshStatusIcon *self, const char *icon_name)
{
  PhoshStatusIconPrivate *priv;
  g_autofree char *old_icon_name = NULL;

  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));

  priv = phosh_status_icon_get_instance_private (self);

  old_icon_name = phosh_status_icon_get_icon_name (self);
  if (!g_strcmp0 (old_icon_name, icon_name))
    return;

  gtk_image_set_from_icon_name (GTK_IMAGE (priv->image), icon_name, -1);

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_ICON_NAME]);
}


char *
phosh_status_icon_get_icon_name (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;
  char *icon_name;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);

  priv = phosh_status_icon_get_instance_private (self);

  g_object_get (priv->image, "icon-name", &icon_name, NULL);

  return icon_name;
}

/**
 * phosh_status_icon_set_icon_size:
 * @self: The status-icon
 * @size: The size of icon
 *
 * Set the size of status-icon.
 *
 * Deprecated: 0.47: Use [method@Phosh.StatusIcon.set_pixel_size].
 */
void
phosh_status_icon_set_icon_size (PhoshStatusIcon *self, GtkIconSize size)
{
  PhoshStatusIconPrivate *priv;
  guint pixel_size;
  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));

  priv = phosh_status_icon_get_instance_private (self);

  if (priv->icon_size == size)
    return;

  priv->icon_size = size;

  switch (size) {
  case GTK_ICON_SIZE_INVALID:
    pixel_size = 0;
    break;
  case GTK_ICON_SIZE_MENU:
    pixel_size = 16;
    break;
  case GTK_ICON_SIZE_SMALL_TOOLBAR:
    pixel_size = 16;
    break;
  case GTK_ICON_SIZE_LARGE_TOOLBAR:
    pixel_size = 24;
    break;
  case GTK_ICON_SIZE_BUTTON:
    pixel_size = 16;
    break;
  case GTK_ICON_SIZE_DND:
    pixel_size = 32;
    break;
  case GTK_ICON_SIZE_DIALOG:
    pixel_size = 48;
    break;
  default:
    g_critical ("Unknown size %d", size);
    return;
  }

  phosh_status_icon_set_pixel_size (self, pixel_size);

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_ICON_SIZE]);
}

/**
 * phosh_status_icon_get_icon_size:
 * @self: The status-icon
 *
 * Return the size of status-icon.
 *
 * Returns: The size of status-icon.
 *
 * Deprecated: 0.47: Use [method@Phosh.StatusIcon.get_pixel_size].
 */
GtkIconSize
phosh_status_icon_get_icon_size (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);

  priv = phosh_status_icon_get_instance_private (self);

  return priv->icon_size;
}


void
phosh_status_icon_set_pixel_size (PhoshStatusIcon *self, guint size)
{
  PhoshStatusIconPrivate *priv;
  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));

  priv = phosh_status_icon_get_instance_private (self);

  if (priv->pixel_size == size)
    return;

  priv->pixel_size = size;
  gtk_image_set_pixel_size (GTK_IMAGE (priv->image), priv->pixel_size);

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_PIXEL_SIZE]);
}


guint
phosh_status_icon_get_pixel_size (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);

  priv = phosh_status_icon_get_instance_private (self);

  return priv->pixel_size;
}


void
phosh_status_icon_set_extra_widget (PhoshStatusIcon *self, GtkWidget *widget)
{
  PhoshStatusIconPrivate *priv;
  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));

  priv = phosh_status_icon_get_instance_private (self);

  if (priv->extra_widget == widget)
    return;

  if (priv->extra_widget != NULL)
    gtk_container_remove (GTK_CONTAINER (priv->box), priv->extra_widget);

  if (widget != NULL)
    gtk_container_add (GTK_CONTAINER (priv->box), widget);

  priv->extra_widget = widget;

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_EXTRA_WIDGET]);
}

/**
 * phosh_status_icon_get_extra_widget:
 * @self: A status icon
 *
 * Get the extra widget or %NULL if there's no extra widget
 *
 * Returns:(transfer none)(nullable): The extra widget
 */
GtkWidget *
phosh_status_icon_get_extra_widget (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);

  priv = phosh_status_icon_get_instance_private (self);

  return priv->extra_widget;
}


char *
phosh_status_icon_get_info (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);

  priv = phosh_status_icon_get_instance_private (self);

  return priv->info;
}


void
phosh_status_icon_set_info (PhoshStatusIcon *self, const char *info)
{
  PhoshStatusIconPrivate *priv;
  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));

  priv = phosh_status_icon_get_instance_private (self);

  if (g_strcmp0 (priv->info, info) == 0)
    return;

  g_clear_pointer (&priv->info, g_free);
  priv->info = g_strdup (info);

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_INFO]);
}


void
phosh_status_icon_set_priority (PhoshStatusIcon *self, int priority)
{
  PhoshStatusIconPrivate *priv;

  g_return_if_fail (PHOSH_IS_STATUS_ICON (self));
  priv = phosh_status_icon_get_instance_private (self);

  if (priv->priority == priority)
    return;

  priv->priority = priority;

  g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_STATUS_ICON_PROP_PRIORITY]);
}


int
phosh_status_icon_get_priority (PhoshStatusIcon *self)
{
  PhoshStatusIconPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_STATUS_ICON (self), 0);
  priv = phosh_status_icon_get_instance_private (self);

  return priv->priority;
}
