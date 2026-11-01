/*
 * Copyright (C) 2020 Purism SPC
 *               2024 Tether Operations Limited
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Julian Sparber <julian.sparber@puri.sm>
 *          Arun Mani J <arun.mani@tether.to>
 */

#define G_LOG_DOMAIN "phosh-quick-setting"

#include "phosh-config.h"

#include "quick-setting.h"
#include "quick-setting-selection-row.h"

/**
 * PhoshQuickSetting:
 *
 * A `PhoshQuickSetting` represents a state of an entity (like Wi-Fi, Bluetooth) using an icon
 * and label. It should be added to a PhoshQuickSettingsBox for better integration.
 *
 * A quick-setting displays the state using an icon and label. The state is set by
 * [class@Phosh.StatusIcon], which must be set as a [property@Phosh.QuickSetting:status-icon]. It
 * can also have a status-page, which can be used to expose additional features. For example, a
 * Wi-Fi quick-setting can show available Wi-Fi hotspots as an extra option. When a status widget is
 * set, the quick-setting displays an arrow at the right end.
 *
 * A quick-setting itself does not have any provision to display its status-page. It is
 * completely upto the user to display and hide the status-pages as required. However the
 * quick-setting can aid in the task with its [property@Phosh.QuickSetting:showing-status] property.
 * When `showing-status` is false, clicking the arrow will cause the quick-setting to emit
 * [signal@Phosh.QuickSetting::show-status]. If `showing-status` is true, then it will emit
 * [signal@Phosh.QuickSetting::hide-status]. The user of the quick-setting is expected to follow
 * this convention and set `showing-status` based on whether they are displaying the status-page
 * or not.
 *
 * A quick-setting might be temporarily prevented from showing its status-page using
 * [property@Phosh.QuickSetting:can-show-status]. Again, `PhoshQuickSettingsBox` can take care of
 * this property, such that once you tell the box if showing status-page is allowed, it will ensure
 * that the children's `can-show-status` are synchronized with it.
 *
 * A quick-setting can be in an active or inactive state. However clicking the quick-setting does
 * not toggle its state. The user must set the state using [property@Phosh.QuickSetting:active]. If
 * the status-icon [class@StatusIcon] has an `enabled` property it will be automatically bound to
 * the [property@Phosh.QuickSetting:active] property.
 *
 * When a quick-setting is clicked, [signal@Phosh.QuickSetting::clicked] is emitted. When it is
 * long-pressed or right-clicked, [signal@PhoshQuickSetting::long-pressed] is emitted.
 *
 * The common usecase of `long-pressed` is to launch an action (like `panel.launch-panel`). So to
 * avoid duplicating this process for each quick-setting, the user can set
 * [property@Phosh.QuickSetting:long-press-action-name] and
 * [property@Phosh.QuickSetting:long-press-action-target]. The quick-setting then launches that
 * appropriate action.
 */

enum {
  PROP_0,
  PROP_ACTIVE,
  PROP_SHOWING_STATUS,
  PROP_CAN_SHOW_STATUS,
  PROP_STATUS_ICON,
  PROP_STATUS_PAGE,
  PROP_LONG_PRESS_ACTION_NAME,
  PROP_LONG_PRESS_ACTION_TARGET,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  CLICKED,
  LONG_PRESSED,
  SHOW_STATUS,
  HIDE_STATUS,
  N_SIGNALS,
};
static guint signals[N_SIGNALS];

typedef struct {
  GtkBox          *box;
  GtkLabel        *label;
  GtkButton       *arrow_btn;
  GtkImage        *arrow;
  GtkGesture      *long_press;
  GtkGesture      *multi_press;

  gboolean         active;
  gboolean         showing_status;
  gboolean         can_show_status;
  PhoshStatusPage *status_page;
  PhoshStatusIcon *status_icon;
  char            *long_press_action_name;
  char            *long_press_action_target;

  GBinding        *active_binding;
  GBinding        *label_binding;
} PhoshQuickSettingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshQuickSetting, phosh_quick_setting, GTK_TYPE_BOX);


static void
phosh_quick_setting_set_property (GObject      *object,
                                  guint         property_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  PhoshQuickSetting *self = PHOSH_QUICK_SETTING (object);

  switch (property_id) {
  case PROP_ACTIVE:
    phosh_quick_setting_set_active (self, g_value_get_boolean (value));
    break;
  case PROP_SHOWING_STATUS:
    phosh_quick_setting_set_showing_status (self, g_value_get_boolean (value));
    break;
  case PROP_CAN_SHOW_STATUS:
    phosh_quick_setting_set_can_show_status (self, g_value_get_boolean (value));
    break;
  case PROP_STATUS_ICON:
    phosh_quick_setting_set_status_icon (self, g_value_get_object (value));
    break;
  case PROP_STATUS_PAGE:
    phosh_quick_setting_set_status_page (self, g_value_get_object (value));
    break;
  case PROP_LONG_PRESS_ACTION_NAME:
    phosh_quick_setting_set_long_press_action_name (self, g_value_get_string (value));
    break;
  case PROP_LONG_PRESS_ACTION_TARGET:
    phosh_quick_setting_set_long_press_action_target (self, g_value_get_string (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
phosh_quick_setting_get_property (GObject    *object,
                                  guint       property_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  PhoshQuickSetting *self = PHOSH_QUICK_SETTING (object);

  switch (property_id) {
  case PROP_ACTIVE:
    g_value_set_boolean (value, phosh_quick_setting_get_active (self));
    break;
  case PROP_SHOWING_STATUS:
    g_value_set_boolean (value, phosh_quick_setting_get_showing_status (self));
    break;
  case PROP_CAN_SHOW_STATUS:
    g_value_set_boolean (value, phosh_quick_setting_get_can_show_status (self));
    break;
  case PROP_STATUS_ICON:
    g_value_set_object (value, phosh_quick_setting_get_status_icon (self));
    break;
  case PROP_STATUS_PAGE:
    g_value_set_object (value, phosh_quick_setting_get_status_page (self));
    break;
  case PROP_LONG_PRESS_ACTION_NAME:
    g_value_set_string (value, phosh_quick_setting_get_long_press_action_name (self));
    break;
  case PROP_LONG_PRESS_ACTION_TARGET:
    g_value_set_string (value, phosh_quick_setting_get_long_press_action_target (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}


static void
on_status_icon_destroy (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv = phosh_quick_setting_get_instance_private (self);
  priv->status_icon = NULL;
}


static void
launch_action_else_emit (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv = phosh_quick_setting_get_instance_private (self);
  GActionGroup *group;
  GVariant *param = NULL;
  g_auto (GStrv) str_array = NULL;

  if (priv->long_press_action_name == NULL) {
    g_signal_emit (self, signals[LONG_PRESSED], 0);
    return;
  }

  if (priv->long_press_action_target != NULL)
    param = g_variant_new_parsed (priv->long_press_action_target, NULL);

  str_array = g_strsplit (priv->long_press_action_name, ".", 2);
  if (g_strv_length (str_array) != 2) {
    g_warning ("Malformed action-name %s", priv->long_press_action_name);
    return;
  }

  group = gtk_widget_get_action_group (GTK_WIDGET (self), str_array[0]);
  g_return_if_fail (group);
  g_action_group_activate_action (group, str_array[1], param);
}


static void
on_long_pressed (PhoshQuickSetting *self, GtkGesture *gesture)
{
  launch_action_else_emit (self);
  gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}


static void
on_right_pressed (PhoshQuickSetting *self, int n_press, double x, double y, GtkGesture *gesture)
{
  launch_action_else_emit (self);
  gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_CLAIMED);
}


static void
on_button_clicked (PhoshQuickSetting *self)
{
  g_signal_emit (self, signals[CLICKED], 0);
}


static void
on_arrow_clicked (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv = phosh_quick_setting_get_instance_private (self);

  if (priv->showing_status)
    g_signal_emit (self, signals[HIDE_STATUS], 0);
  else
    g_signal_emit (self, signals[SHOW_STATUS], 0);
}


static void
on_status_destroy (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv = phosh_quick_setting_get_instance_private (self);
  g_clear_object (&priv->status_page);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATUS_PAGE]);
}


static void
on_status_page_done (PhoshQuickSetting *self)
{
  g_signal_emit (self, signals[HIDE_STATUS], 0);
}


static void
phosh_quick_setting_finalize (GObject *object)
{
  PhoshQuickSetting *self = PHOSH_QUICK_SETTING (object);
  PhoshQuickSettingPrivate *priv = phosh_quick_setting_get_instance_private (self);

  g_clear_object (&priv->status_page);
  g_clear_pointer (&priv->long_press_action_name, g_free);
  g_clear_pointer (&priv->long_press_action_target, g_free);

  G_OBJECT_CLASS (phosh_quick_setting_parent_class)->finalize (object);
}


static void
phosh_quick_setting_destroy (GtkWidget *widget)
{
  PhoshQuickSetting *self = PHOSH_QUICK_SETTING (widget);

  phosh_quick_setting_set_status_icon (self, NULL);

  GTK_WIDGET_CLASS (phosh_quick_setting_parent_class)->destroy (widget);
}


static void
phosh_quick_setting_class_init (PhoshQuickSettingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  /* Make sure the selection row's template is available to plugins */
  g_type_ensure (PHOSH_TYPE_QUICK_SETTING_SELECTION_ROW);

  object_class->set_property = phosh_quick_setting_set_property;
  object_class->get_property = phosh_quick_setting_get_property;
  object_class->finalize = phosh_quick_setting_finalize;

  widget_class->destroy = phosh_quick_setting_destroy;

  /**
   * PhoshQuickSetting:active:
   *
   * The active state of the child.
   */
  props[PROP_ACTIVE] =
    g_param_spec_boolean ("active", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:showing-status:
   *
   * If the child is displaying its status.
   */
  props[PROP_SHOWING_STATUS] =
    g_param_spec_boolean ("showing-status", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:can-show-status:
   *
   * If the child can display its status.
   */
  props[PROP_CAN_SHOW_STATUS] =
    g_param_spec_boolean ("can-show-status", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:status-icon:
   *
   * The status-icon.
   */
  props[PROP_STATUS_ICON] =
    g_param_spec_object ("status-icon", "", "",
                         PHOSH_TYPE_STATUS_ICON,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:status-page:
   *
   * The status-page.
   */
  props[PROP_STATUS_PAGE] =
    g_param_spec_object ("status-page", "", "",
                         PHOSH_TYPE_STATUS_PAGE,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:long-press-action-name:
   *
   * Action name to trigger on long-press.
   */
  props[PROP_LONG_PRESS_ACTION_NAME] =
    g_param_spec_string ("long-press-action-name", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshQuickSetting:long-press-action-target:
   *
   * Action target for `long-press-action-name`.
   */
  props[PROP_LONG_PRESS_ACTION_TARGET] =
    g_param_spec_string ("long-press-action-target", "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[CLICKED] = g_signal_new ("clicked",
                                   G_OBJECT_CLASS_TYPE (object_class),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[LONG_PRESSED] = g_signal_new ("long-pressed",
                                        G_OBJECT_CLASS_TYPE (object_class),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[SHOW_STATUS] = g_signal_new ("show-status",
                                       G_OBJECT_CLASS_TYPE (object_class),
                                       G_SIGNAL_RUN_LAST,
                                       0, NULL, NULL, NULL, G_TYPE_NONE, 0);
  signals[HIDE_STATUS] = g_signal_new ("hide-status",
                                       G_OBJECT_CLASS_TYPE (object_class),
                                       G_SIGNAL_RUN_LAST,
                                       0, NULL, NULL, NULL, G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/quick-setting.ui");

  gtk_widget_class_bind_template_callback (widget_class, on_arrow_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_long_pressed);
  gtk_widget_class_bind_template_callback (widget_class, on_right_pressed);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, box);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, label);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, arrow);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, arrow_btn);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, long_press);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshQuickSetting, multi_press);

  gtk_widget_class_set_css_name (widget_class, "phosh-quick-setting");
}


static void
phosh_quick_setting_init (PhoshQuickSetting *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
phosh_quick_setting_new (PhoshStatusPage *status_page)
{
  return GTK_WIDGET (g_object_new (PHOSH_TYPE_QUICK_SETTING, "status-page", status_page, NULL));
}


void
phosh_quick_setting_set_active (PhoshQuickSetting *self, gboolean active)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));

  priv = phosh_quick_setting_get_instance_private (self);

  if (priv->active == active)
    return;

  priv->active = active;

  if (priv->active)
    gtk_widget_set_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_CHECKED, FALSE);
  else
    gtk_widget_unset_state_flags (GTK_WIDGET (self), GTK_STATE_FLAG_CHECKED);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTIVE]);
}


gboolean
phosh_quick_setting_get_active (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), FALSE);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->active;
}


void
phosh_quick_setting_set_showing_status (PhoshQuickSetting *self, gboolean showing_status)
{
  PhoshQuickSettingPrivate *priv;
  const char *icon_name;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));

  priv = phosh_quick_setting_get_instance_private (self);

  if (priv->showing_status == showing_status)
    return;

  priv->showing_status = showing_status;

  if (priv->showing_status)
    icon_name = "go-down-symbolic";
  else
    icon_name = "go-next-symbolic";

  gtk_image_set_from_icon_name (priv->arrow, icon_name, -1);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_SHOWING_STATUS]);
}


gboolean
phosh_quick_setting_get_showing_status (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), FALSE);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->showing_status;
}


void
phosh_quick_setting_set_can_show_status (PhoshQuickSetting *self, gboolean can_show_status)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));

  priv = phosh_quick_setting_get_instance_private (self);

  if (priv->can_show_status == can_show_status)
    return;

  priv->can_show_status = can_show_status;
  gtk_widget_set_visible (GTK_WIDGET (priv->arrow_btn),
                          priv->status_page != NULL && priv->can_show_status);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CAN_SHOW_STATUS]);
}


gboolean
phosh_quick_setting_get_can_show_status (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), FALSE);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->can_show_status;
}

/**
 * phosh_quick_setting_set_status_icon:
 * @self: A quick-setting
 * @status_icon: A status-icon or `NULL`
 *
 * Set the status-icon of the quick-setting. Use `NULL` to remove existing icon.
 */
void
phosh_quick_setting_set_status_icon (PhoshQuickSetting *self, PhoshStatusIcon *status_icon)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));
  g_return_if_fail (status_icon == NULL || PHOSH_IS_STATUS_ICON (status_icon));

  priv = phosh_quick_setting_get_instance_private (self);

  if (priv->status_icon == status_icon)
    return;

  if (priv->status_icon) {
    g_clear_pointer (&priv->label_binding, g_binding_unbind);
    g_clear_pointer (&priv->active_binding, g_binding_unbind);
    gtk_container_remove (GTK_CONTAINER (priv->box), GTK_WIDGET (priv->status_icon));
  }

  priv->status_icon = status_icon;

  if (priv->status_icon) {
    priv->label_binding = g_object_bind_property (status_icon, "info",
                                                  priv->label, "label",
                                                  G_BINDING_SYNC_CREATE);

    if (g_object_class_find_property (G_OBJECT_GET_CLASS (status_icon), "enabled")) {
      priv->active_binding = g_object_bind_property (status_icon, "enabled",
                                                     self, "active",
                                                     G_BINDING_SYNC_CREATE);
    }

    g_signal_connect_object (status_icon, "destroy", G_CALLBACK (on_status_icon_destroy), self,
                             G_CONNECT_SWAPPED);

    gtk_box_pack_start (priv->box, GTK_WIDGET (priv->status_icon), 0, 0, 0);
    gtk_box_reorder_child (priv->box, GTK_WIDGET (priv->status_icon), 0);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATUS_PAGE]);
}

/**
 * phosh_quick_setting_get_status_icon:
 * @self: A quick-setting
 *
 * Get the current status-icon of the quick-setting.
 *
 * Returns:(transfer none): The status-icon or `NULL`.
 */
PhoshStatusIcon *
phosh_quick_setting_get_status_icon (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), NULL);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->status_icon;
}


/**
 * phosh_quick_setting_set_status_page:
 * @self: A quick-setting
 * @status_page: A status-page or `NULL`
 *
 * Set the status-page of the quick-setting.
 */
void
phosh_quick_setting_set_status_page (PhoshQuickSetting *self, PhoshStatusPage *status_page)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));
  g_return_if_fail (status_page == NULL || PHOSH_IS_STATUS_PAGE (status_page));

  priv = phosh_quick_setting_get_instance_private (self);

  if (priv->status_page == status_page)
    return;

  if (priv->status_page != NULL) {
    g_signal_handlers_disconnect_by_data (priv->status_page, self);
    g_clear_object (&priv->status_page);
  }

  priv->status_page = status_page;
  if (priv->status_page != NULL) {
    g_object_ref_sink (priv->status_page);
    g_signal_connect_object (priv->status_page,
                             "destroy",
                             G_CALLBACK (on_status_destroy),
                             self,
                             G_CONNECT_SWAPPED);
    g_signal_connect_object (priv->status_page,
                             "done",
                             G_CALLBACK (on_status_page_done),
                             self,
                             G_CONNECT_SWAPPED);
  }

  gtk_widget_set_visible (GTK_WIDGET (priv->arrow_btn),
                          priv->status_page != NULL && priv->can_show_status);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_STATUS_PAGE]);
}


/**
 * phosh_quick_setting_get_status_page:
 * @self: A quick-setting
 *
 * Get the current status widget of the quick-setting.
 *
 * Returns:(transfer none): The status-page or `NULL`.
 */
PhoshStatusPage *
phosh_quick_setting_get_status_page (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), NULL);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->status_page;
}


void
phosh_quick_setting_set_long_press_action_name (PhoshQuickSetting *self, const char *action_name)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));

  priv = phosh_quick_setting_get_instance_private (self);

  g_clear_pointer (&priv->long_press_action_name, g_free);
  priv->long_press_action_name = g_strdup (action_name);
}


const char *
phosh_quick_setting_get_long_press_action_name (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), NULL);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->long_press_action_name;
}


void
phosh_quick_setting_set_long_press_action_target (PhoshQuickSetting *self, const char *action_target)
{
  PhoshQuickSettingPrivate *priv;

  g_return_if_fail (PHOSH_IS_QUICK_SETTING (self));

  priv = phosh_quick_setting_get_instance_private (self);

  g_clear_pointer (&priv->long_press_action_target, g_free);
  priv->long_press_action_target = g_strdup (action_target);
}


const char *
phosh_quick_setting_get_long_press_action_target (PhoshQuickSetting *self)
{
  PhoshQuickSettingPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_QUICK_SETTING (self), NULL);

  priv = phosh_quick_setting_get_instance_private (self);

  return priv->long_press_action_target;
}
