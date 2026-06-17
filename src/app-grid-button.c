/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phosh-app-grid-button"

#include "phosh-config.h"
#include "app-tracker.h"
#include "app-grid-button.h"
#include "clamp.h"
#include "fading-label.h"
#include "metainfo-cache.h"
#include "phosh-enums.h"
#include "favorite-list-model.h"
#include "util.h"

#include "shell-priv.h"
#include "util.h"

/**
 * PhoshAppGridButton:
 *
 * An app-grid button to represent an application launcher or favorite.
 */

typedef struct _PhoshAppGridButtonPrivate PhoshAppGridButtonPrivate;
struct _PhoshAppGridButtonPrivate {
  GAppInfo              *info;
  gboolean               is_favorite;
  PhoshAppGridButtonMode mode;
  PhoshFolderInfo       *folder_info;

  gulong favorite_changed_watcher;

  GtkWidget             *icon;
  GtkWidget             *popover;
  GtkGesture            *long_gesture;
  GtkGesture            *right_gesture;

  GMenu      *menu;
  GMenu      *actions;
  GMenu      *folders;

  GActionMap *action_map;
};

G_DEFINE_TYPE_WITH_PRIVATE (PhoshAppGridButton, phosh_app_grid_button,
                            PHOSH_TYPE_APP_GRID_BASE_BUTTON)

enum {
  PROP_0,
  PROP_APP_INFO,
  PROP_IS_FAVORITE,
  PROP_MODE,
  PROP_FOLDER_INFO,
  LAST_PROP
};
static GParamSpec *props[LAST_PROP];

enum {
  APP_LAUNCHED,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

static void
phosh_app_grid_button_set_property (GObject      *object,
                                    guint         property_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (object);

  switch (property_id) {
  case PROP_APP_INFO:
    phosh_app_grid_button_set_app_info (self, g_value_get_object (value));
    break;
  case PROP_MODE:
    phosh_app_grid_button_set_mode (self, g_value_get_enum (value));
    break;
  case PROP_FOLDER_INFO:
    phosh_app_grid_button_set_folder_info (self, g_value_get_object (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_app_grid_button_get_property (GObject    *object,
                                    guint       property_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (object);

  switch (property_id) {
  case PROP_APP_INFO:
    g_value_set_object (value, phosh_app_grid_button_get_app_info (self));
    break;
  case PROP_IS_FAVORITE:
    g_value_set_boolean (value, phosh_app_grid_button_is_favorite (self));
    break;
  case PROP_MODE:
    g_value_set_enum (value, phosh_app_grid_button_get_mode (self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_app_grid_button_finalize (GObject *object)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (object);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);

  g_clear_object (&priv->info);
  g_clear_object (&priv->menu);
  g_clear_object (&priv->actions);
  g_clear_object (&priv->action_map);
  g_clear_object (&priv->folder_info);

  g_clear_signal_handler (&priv->favorite_changed_watcher,
                          phosh_favorite_list_model_get_default ());

  G_OBJECT_CLASS (phosh_app_grid_button_parent_class)->finalize (object);
}


static void
populate_folders_menu (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  g_autoptr (GSettings) settings = NULL;
  g_auto (GStrv) folder_paths = NULL;
  g_autoptr (GMenu) submenu = g_menu_new ();
  g_autoptr (GMenuItem) submenu_item = g_menu_item_new_submenu (_("Add to Folder"),
                                                                G_MENU_MODEL (submenu));
  g_autoptr (GMenu) folders_section = g_menu_new ();
  g_autoptr (GMenu) new_folder_section = g_menu_new ();

  settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  folder_paths = g_settings_get_strv (settings, "folder-children");

  for (int i = 0; folder_paths[i] != NULL; i++) {
    g_autoptr (PhoshFolderInfo) folder = phosh_folder_info_new_from_folder_path (folder_paths[i]);
    g_autofree char *detailed_action = NULL;

    if (!g_app_info_should_show (G_APP_INFO (folder)))
      continue;

    if (priv->folder_info != NULL &&
        g_app_info_equal (G_APP_INFO (folder), G_APP_INFO (priv->folder_info)))
      continue;

    detailed_action = g_strdup_printf ("app-btn.folder-add::%s", folder_paths[i]);
    g_menu_append (folders_section, phosh_folder_info_get_name (folder), detailed_action);
  }

  g_menu_append_section (submenu, NULL, G_MENU_MODEL (folders_section));
  g_menu_append (new_folder_section, _("Create new folder"), "app-btn.folder-new");
  g_menu_append_section (submenu, NULL, G_MENU_MODEL (new_folder_section));

  g_menu_append_item (priv->folders, submenu_item);
}


static void
context_menu (GtkWidget *widget,
              GdkEvent  *event)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (widget);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);

  g_menu_remove_all (priv->folders);
  if (!priv->is_favorite && priv->folder_info == NULL)
    populate_folders_menu (self);
  gtk_popover_popup (GTK_POPOVER (priv->popover));
}


static void
on_right_pressed (GtkWidget *self, int n_press, double x, double y, GtkGesture *gesture)
{
  const GdkEvent *event = gtk_gesture_get_last_event (gesture, NULL);
  if (gdk_event_triggers_context_menu (event))
    context_menu (self, (GdkEvent *) event);
}

static void
on_long_pressed (GtkWidget *self, double x, double y, GtkGesture *gesture)
{
  context_menu (self, NULL);
}



static void
activate_cb (PhoshAppGridButton *self)
{
  PhoshAppTracker *app_tracker = phosh_shell_get_app_tracker (phosh_shell_get_default ());
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);

  g_return_if_fail (PHOSH_IS_APP_TRACKER (app_tracker));

  phosh_app_tracker_launch_app_info (app_tracker, priv->info);
  g_signal_emit (self, signals[APP_LAUNCHED], 0, priv->info);
}


static void
phosh_app_grid_button_class_init (PhoshAppGridButtonClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->set_property = phosh_app_grid_button_set_property;
  object_class->get_property = phosh_app_grid_button_get_property;
  object_class->finalize = phosh_app_grid_button_finalize;

  props[PROP_APP_INFO] =
    g_param_spec_object ("app-info", "App", "App Info",
                         G_TYPE_APP_INFO,
                         G_PARAM_STATIC_STRINGS |
                         G_PARAM_READWRITE |
                         G_PARAM_EXPLICIT_NOTIFY);

  /**
   * PhoshAppGridButton:is-favorite:
   *
   * %TRUE when the application is currently favorited
   *
   * Stability: Private
   */
  props[PROP_IS_FAVORITE] =
    g_param_spec_boolean ("is-favorite", "Favorite", "Is a favorite app",
                          FALSE,
                          G_PARAM_STATIC_STRINGS |
                          G_PARAM_READABLE |
                          G_PARAM_EXPLICIT_NOTIFY);

  /**
   * PhoshAppGridButton:mode:
   *
   * The #PhoshAppGridButtonMode of the button
   *
   * In %PHOSH_APP_GRID_BUTTON_FAVORITES the label is
   * hidden
   *
   * Stability: Private
   */
  props[PROP_MODE] =
    g_param_spec_enum ("mode", "Mode", "Button mode",
                       PHOSH_TYPE_APP_GRID_BUTTON_MODE,
                       PHOSH_APP_GRID_BUTTON_LAUNCHER,
                       G_PARAM_STATIC_STRINGS |
                       G_PARAM_READWRITE |
                       G_PARAM_EXPLICIT_NOTIFY);

  /**
   * PhoshAppGridButton:folder-info:
   *
   * The folder-info to which the button is a part of. Can be NULL.
   *
   * Stability: Private
   */
  props[PROP_FOLDER_INFO] =
    g_param_spec_object ("folder-info", "", "",
                         PHOSH_TYPE_FOLDER_INFO,
                         G_PARAM_WRITABLE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/app-grid-button.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, icon);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, popover);

  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, long_gesture);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, right_gesture);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, menu);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, actions);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGridButton, folders);

  gtk_widget_class_bind_template_callback (widget_class, on_long_pressed);
  gtk_widget_class_bind_template_callback (widget_class, on_right_pressed);
  gtk_widget_class_bind_template_callback (widget_class, activate_cb);

  signals[APP_LAUNCHED] = g_signal_new ("app-launched",
                                        G_TYPE_FROM_CLASS (klass),
                                        G_SIGNAL_RUN_LAST,
                                        0, NULL, NULL, NULL,
                                        G_TYPE_NONE, 1, G_TYPE_APP_INFO);

  gtk_widget_class_set_css_name (widget_class, "phosh-app-grid-button");
}


static void
action_activated (GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  g_autoptr (GdkAppLaunchContext) context = NULL;
  const char *action_name;

  action_name = g_variant_get_string (parameter, NULL);

  g_debug ("Launching %s->%s", g_app_info_get_id (priv->info), action_name);

  g_return_if_fail (action_name != NULL);
  g_return_if_fail (G_IS_DESKTOP_APP_INFO (priv->info));

  context = gdk_display_get_app_launch_context (gtk_widget_get_display (GTK_WIDGET (self)));

  g_desktop_app_info_launch_action (G_DESKTOP_APP_INFO (priv->info),
                                    action_name,
                                    G_APP_LAUNCH_CONTEXT (context));

  g_signal_emit (self, signals[APP_LAUNCHED], 0, priv->info);
}


static void
favorite_remove_activated (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);

  phosh_favorite_list_model_remove_app (NULL, priv->info);
}


static void
favorite_add_activated (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);

  phosh_favorite_list_model_add_app (NULL, priv->info);
}


static void
spawn_gnome_software (const char *action, const char *app_id)
{
  const char *argv[] = { "gnome-software", action, app_id, NULL };
  g_autoptr (GError) err = NULL;

  if (!g_spawn_async (NULL, (char **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err))
    g_warning ("Failed to run 'gnome-software %s' for '%s': %s", action, app_id, err->message);
}


static void
view_details_activated (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  const char *app_id = g_app_info_get_id (priv->info);
  g_return_if_fail (app_id);

  spawn_gnome_software ("--details", app_id);
}


static void
uninstall_activated (GSimpleAction *action,
                     GVariant      *parameter,
                     gpointer       data)
{
  PhoshShell *shell = phosh_shell_get_default ();
  g_autofree char *appstream_id = NULL;
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  const char *app_id = g_app_info_get_id (priv->info);
  g_return_if_fail (app_id);

  appstream_id = phosh_metainfo_get_data_id (phosh_metainfo_cache_get_default (), app_id);
  if (!appstream_id) {
    phosh_shell_show_notification_for_app (shell,
                                           priv->info,
                                           _("Can't uninstall application"));
    g_warning ("Failed to get appstream-id for '%s'", app_id);
    return;
  }

  spawn_gnome_software ("--uninstall", appstream_id);
}


static void
undo_hide_activated (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  PhoshShellNotification *noti = PHOSH_SHELL_NOTIFICATION (data);
  char *filename = g_object_get_data (G_OBJECT (noti), "filename");

  g_return_if_fail (filename);
  g_debug ("Undoing hide operation for %s", filename);

  phosh_util_unhide_app (filename);
}


static GActionEntry hide_noti_entries[] =
{
  { .name = "noti.undo", .activate = undo_hide_activated },
};


static void
hide_activated (GSimpleAction *action, GVariant *parameter, gpointer data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshShell *shell = phosh_shell_get_default ();
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  g_autofree char *filename = NULL;

  filename = phosh_util_hide_app (priv->info);
  if (filename) {
    g_autoptr (PhoshShellNotification) noti = NULL;
    g_autoptr (GSimpleActionGroup) action_group = g_simple_action_group_new ();
    char *actions[] = { "noti.undo", _("Undo"), NULL };
    const char *msg = _("Application is now permanently hidden and disabled");

    noti = phosh_shell_create_notification_for_app (shell, priv->info, msg);
    g_action_map_add_action_entries (G_ACTION_MAP (action_group),
                                     hide_noti_entries,
                                     G_N_ELEMENTS (hide_noti_entries),
                                     noti);
    phosh_shell_notification_set_actions (noti, actions, G_ACTION_GROUP (action_group));
    g_object_set_data_full (G_OBJECT (noti),
                            "filename",
                            g_steal_pointer (&filename),
                            (GDestroyNotify)g_free);
    phosh_shell_show_notification (shell, noti, -1);

  } else {
    phosh_shell_show_notification_for_app (shell,
                                           priv->info,
                                           _("Failed to hide application"));
  }
}


static void
add_to_folder_children (char *folder_path)
{
  g_autoptr (GSettings) settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  g_auto (GStrv) folders = NULL;
  g_auto (GStrv) new_folders = NULL;

  folders = g_settings_get_strv (settings, "folder-children");
  new_folders = phosh_util_append_to_strv (folders, folder_path);
  g_settings_set_strv (settings, "folder-children", (const char *const *) new_folders);
}


static void
remove_from_folder_children (char *folder_path)
{
  g_autoptr (GSettings) settings = g_settings_new (PHOSH_FOLDERS_SCHEMA_ID);
  g_auto (GStrv) folders = NULL;
  g_auto (GStrv) new_folders = NULL;

  folders = g_settings_get_strv (settings, "folder-children");
  new_folders = phosh_util_remove_from_strv (folders, folder_path);
  g_settings_set_strv (settings, "folder-children", (const char *const *) new_folders);
}


static void
remove_from_folder (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  g_autofree char *folder_path;

  g_object_get (priv->folder_info, "path", &folder_path, NULL);

  if (!phosh_folder_info_remove_app_info (priv->folder_info, priv->info))
    remove_from_folder_children (folder_path);
}


static void
add_to_folder (PhoshAppGridButton *self, PhoshFolderInfo *folder_info)
{
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  /* If the app-info gets removed, then the change causes the grid to be filled with new buttons.
   * This would cause the button and in-turn the app-info to be disposed. Take a reference to avoid
   * that and clear it when we are done. */
  g_autoptr (GAppInfo) info = g_object_ref (priv->info);

  if (priv->folder_info)
    remove_from_folder (self);
  phosh_folder_info_add_app_info (folder_info, info);
}


static void
folder_add_activated (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  char *folder_path = (char *) g_variant_get_string (parameter, NULL);
  g_autoptr (PhoshFolderInfo) folder_info = phosh_folder_info_new_from_folder_path (folder_path);

  add_to_folder (self, folder_info);
}


static void
folder_new_activated (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  g_autofree char *folder_path = g_uuid_string_random ();
  g_autoptr (PhoshFolderInfo) folder_info = phosh_folder_info_new_from_folder_path (folder_path);

  phosh_folder_info_set_name (folder_info, g_app_info_get_name (priv->info));
  add_to_folder (self, folder_info);
  add_to_folder_children (folder_path);
}


static void
folder_remove_activated (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       data)
{
  PhoshAppGridButton *self = PHOSH_APP_GRID_BUTTON (data);
  remove_from_folder (self);
}


static GActionEntry entries[] =
{
  { .name = "action", .activate = action_activated, .parameter_type = "s" },
  { .name = "favorite-remove", .activate = favorite_remove_activated },
  { .name = "favorite-add", .activate = favorite_add_activated },
  { .name = "view-details", .activate = view_details_activated },
  { .name = "uninstall", .activate = uninstall_activated },
  { .name = "hide", .activate = hide_activated },
  { .name = "folder-add", .activate = folder_add_activated, .parameter_type = "s" },
  { .name = "folder-new", .activate = folder_new_activated },
  { .name = "folder-remove", .activate = folder_remove_activated },
};


static void
phosh_app_grid_button_init (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv = phosh_app_grid_button_get_instance_private (self);
  PhoshMetainfoCache *metainfo_cache = phosh_metainfo_cache_get_default ();
  GAction *act;
  gboolean have_gnome_software = FALSE;

  have_gnome_software = phosh_util_have_gnome_software (FALSE);

  priv->is_favorite = FALSE;
  priv->mode = PHOSH_APP_GRID_BUTTON_LAUNCHER;
  priv->favorite_changed_watcher = 0;

  priv->action_map = G_ACTION_MAP (g_simple_action_group_new ());
  g_action_map_add_action_entries (priv->action_map,
                                   entries,
                                   G_N_ELEMENTS (entries),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "app-btn",
                                  G_ACTION_GROUP (priv->action_map));

  act = g_action_map_lookup_action (priv->action_map, "favorite-add");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), TRUE);
  act = g_action_map_lookup_action (priv->action_map, "favorite-remove");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  act = g_action_map_lookup_action (priv->action_map, "view-details");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), have_gnome_software);
  act = g_action_map_lookup_action (priv->action_map, "folder-remove");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);

  /* Uninstall is hidden initially until we're sure the AppStream cache has loaded. */
  act = g_action_map_lookup_action (priv->action_map, "uninstall");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);

  act = g_action_map_lookup_action (priv->action_map, "hide");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);

  if (have_gnome_software) {
    act = g_action_map_lookup_action (priv->action_map, "uninstall");
    g_object_bind_property (metainfo_cache, "ready", act, "enabled", G_BINDING_SYNC_CREATE);
  } else {
    act = g_action_map_lookup_action (priv->action_map, "hide");
    g_simple_action_set_enabled (G_SIMPLE_ACTION (act), TRUE);
  }

  g_type_ensure (PHOSH_TYPE_CLAMP);
  g_type_ensure (PHOSH_TYPE_FADING_LABEL);

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_popover_bind_model (GTK_POPOVER (priv->popover),
                          G_MENU_MODEL (priv->menu),
                          NULL);
}


GtkWidget *
phosh_app_grid_button_new (GAppInfo *info)
{
  return g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                       "app-info", info,
                       NULL);
}


GtkWidget *
phosh_app_grid_button_new_favorite (GAppInfo *info)
{
  return g_object_new (PHOSH_TYPE_APP_GRID_BUTTON,
                       "app-info", info,
                       "mode", PHOSH_APP_GRID_BUTTON_FAVORITES,
                       NULL);
}


static void
favorites_changed (GListModel         *list,
                   guint               position,
                   guint               removed,
                   guint               added,
                   PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv;
  gboolean favorite = FALSE;
  GAction *act;

  g_return_if_fail (PHOSH_IS_APP_GRID_BUTTON (self));
  g_return_if_fail (PHOSH_IS_FAVORITE_LIST_MODEL (list));

  priv = phosh_app_grid_button_get_instance_private (self);

  favorite = phosh_favorite_list_model_app_is_favorite (PHOSH_FAVORITE_LIST_MODEL (list),
                                                        priv->info);

  if (priv->is_favorite == favorite)
    return;

  act = g_action_map_lookup_action (priv->action_map, "favorite-add");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), !favorite);
  act = g_action_map_lookup_action (priv->action_map, "favorite-remove");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (act), favorite);

  priv->is_favorite = favorite;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IS_FAVORITE]);
}


void
phosh_app_grid_button_set_app_info (PhoshAppGridButton *self,
                                    GAppInfo           *info)
{
  PhoshFavoriteListModel *list = NULL;
  PhoshAppGridButtonPrivate *priv;
  GIcon *icon;
  const char *name;

  g_return_if_fail (PHOSH_IS_APP_GRID_BUTTON (self));
  g_return_if_fail (G_IS_APP_INFO (info) || info == NULL);

  priv = phosh_app_grid_button_get_instance_private (self);

  if (priv->info == info)
    return;

  g_clear_object (&priv->info);

  g_menu_remove_all (priv->actions);

  list = phosh_favorite_list_model_get_default ();

  g_clear_signal_handler (&priv->favorite_changed_watcher, list);

  if (info) {
    priv->info = g_object_ref (info);

    priv->favorite_changed_watcher = g_signal_connect (list,
                                                       "items-changed",
                                                       G_CALLBACK (favorites_changed),
                                                       self);
    favorites_changed (G_LIST_MODEL (list), 0, 0, 0, self);

    name = g_app_info_get_name (G_APP_INFO (priv->info));
    phosh_app_grid_base_button_set_label (PHOSH_APP_GRID_BASE_BUTTON (self), name);

    icon = g_app_info_get_icon (priv->info);
    if (G_UNLIKELY (icon == NULL)) {
      gtk_image_set_from_icon_name (GTK_IMAGE (priv->icon), PHOSH_APP_UNKNOWN_ICON, -1);
    } else {
      if (G_IS_THEMED_ICON (icon)) {
        g_themed_icon_append_name (G_THEMED_ICON (icon),
                                   PHOSH_APP_UNKNOWN_ICON);
      }
      gtk_image_set_from_gicon (GTK_IMAGE (priv->icon), icon, -1);
    }

    gtk_widget_set_sensitive (GTK_WIDGET (self), TRUE);

    if (G_IS_DESKTOP_APP_INFO (priv->info)) {
      const char *const *actions = NULL;
      int i = 0;

      actions = g_desktop_app_info_list_actions (G_DESKTOP_APP_INFO (priv->info));

      /*
       * So the dummy GAppInfo for the tests is (for reasons known only to gio)
       * actually a GDesktopAppInfo rather than something like GDummyAppInfo,
       * this means that guarding this block with G_IS_DESKTOP_APP_INFO
       * doesn't actually help much. This seems to surprise even gio as instead
       * of always returning at least an empty array (as the API promises) it
       * returns NULL
       *
       * tl;dr: we do (actions && actions[i]) instead of (actions[i]) otherwise
       *        the tests explode because of a condition that can only exist in
       *        the tests
       */

      while (actions && actions[i]) {
        g_autofree char *detailed_action = NULL;
        g_autofree char *label = NULL;

        detailed_action = g_strdup_printf ("app-btn.action::%s", actions[i]);

        label = g_desktop_app_info_get_action_name (G_DESKTOP_APP_INFO (priv->info),
                                                    actions[i]);

        g_menu_append (priv->actions, label, detailed_action);

        i++;
      }
    }
  } else {
    phosh_app_grid_base_button_set_label (PHOSH_APP_GRID_BASE_BUTTON (self), _("Application"));
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->icon), PHOSH_APP_UNKNOWN_ICON, -1);

    gtk_widget_set_sensitive (GTK_WIDGET (self), FALSE);
  }

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_APP_INFO]);
}

/**
 * phosh_app_grid_button_get_app_info:
 * @self: An app grid button
 *
 * Get the app info
 *
 * Returns:(transfer none): The app info
 */
GAppInfo *
phosh_app_grid_button_get_app_info (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_APP_GRID_BUTTON (self), NULL);
  priv = phosh_app_grid_button_get_instance_private (self);

  return priv->info;
}


gboolean
phosh_app_grid_button_is_favorite (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_APP_GRID_BUTTON (self), FALSE);
  priv = phosh_app_grid_button_get_instance_private (self);

  return priv->is_favorite;
}


void
phosh_app_grid_button_set_mode (PhoshAppGridButton    *self,
                                PhoshAppGridButtonMode mode)
{
  PhoshAppGridButtonPrivate *priv;
  const char *name;

  g_return_if_fail (PHOSH_IS_APP_GRID_BUTTON (self));
  priv = phosh_app_grid_button_get_instance_private (self);

  if (priv->mode == mode)
    return;

  name = priv->info == NULL ? _("Application") : g_app_info_get_name (priv->info);

  switch (mode) {
  case PHOSH_APP_GRID_BUTTON_LAUNCHER:
    phosh_app_grid_base_button_set_label (PHOSH_APP_GRID_BASE_BUTTON (self), name);
    break;
  case PHOSH_APP_GRID_BUTTON_FAVORITES:
    phosh_app_grid_base_button_set_label (PHOSH_APP_GRID_BASE_BUTTON (self), NULL);
    break;
  default:
    g_critical ("Invalid mode %i", mode);
    return;
  }

  priv->mode = mode;

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MODE]);
}


PhoshAppGridButtonMode
phosh_app_grid_button_get_mode (PhoshAppGridButton *self)
{
  PhoshAppGridButtonPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_APP_GRID_BUTTON (self), FALSE);
  priv = phosh_app_grid_button_get_instance_private (self);

  return priv->mode;
}


void
phosh_app_grid_button_set_folder_info (PhoshAppGridButton *self, PhoshFolderInfo *folder_info)
{
  PhoshAppGridButtonPrivate *priv;
  GAction *act;

  g_return_if_fail (PHOSH_IS_APP_GRID_BUTTON (self));
  g_return_if_fail ((folder_info == NULL) || PHOSH_IS_FOLDER_INFO (folder_info));
  priv = phosh_app_grid_button_get_instance_private (self);

  g_clear_object (&priv->folder_info);
  act = g_action_map_lookup_action (priv->action_map, "folder-remove");

  if (folder_info) {
    priv->folder_info = g_object_ref (folder_info);
    g_simple_action_set_enabled (G_SIMPLE_ACTION (act), TRUE);
  } else {
    g_simple_action_set_enabled (G_SIMPLE_ACTION (act), FALSE);
  }
}
