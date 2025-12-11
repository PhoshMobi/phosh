/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "phosh-app-grid"

#define _GNU_SOURCE
#include <string.h>

#include "app-grid.h"
#include "app-grid-button.h"
#include "app-grid-folder-button.h"
#include "app-list-model.h"
#include "favorite-list-model.h"
#include "shell-priv.h"
#include "util.h"

#include "gtk-list-models/gtksortlistmodel.h"
#include "gtk-list-models/gtkfilterlistmodel.h"

#include <gmobile.h>

#define ACTIVE_SEARCH_CLASS "search-active"

#define SEARCH_DEBOUNCE 350
#define DEFAULT_GTK_DEBOUNCE 150

enum {
  PROP_0,
  PROP_FILTER_ADAPTIVE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  APP_LAUNCHED,
  N_SIGNALS
};
static guint signals[N_SIGNALS] = { 0 };

typedef struct _PhoshAppGridPrivate PhoshAppGridPrivate;
struct _PhoshAppGridPrivate {
  GtkFilterListModel *model;

  GtkWidget *deck;
  GtkWidget *search;
  GtkWidget *apps;
  GtkWidget *favs;
  GtkWidget *favs_revealer;
  GtkWidget *scrolled_window;
  GtkWidget *btn_adaptive;
  GtkWidget *btn_adaptive_img;
  GtkWidget *btn_adaptive_lbl;
  GtkWidget *empty_folder_label;
  GtkWidget *folder_stack;
  GtkWidget *folder_name_btn;
  GtkWidget *folder_name_img;
  GtkWidget *folder_name_entry;
  GtkWidget *folder_name_label;
  GtkWidget *folder_apps;

  PhoshFolderInfo *open_folder;
  int              open_folder_idx;
  GListModel      *folder_model;

  char *search_string;
  gboolean filter_adaptive;
  GSettings *settings;
  GStrv force_adaptive;
  GSimpleActionGroup *actions;
  PhoshAppFilterModeFlags filter_mode;
  guint debounce;
};

G_DEFINE_TYPE_WITH_PRIVATE (PhoshAppGrid, phosh_app_grid, GTK_TYPE_BOX)

static void
phosh_app_grid_set_property (GObject      *object,
                             guint         property_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  PhoshAppGrid *self = PHOSH_APP_GRID (object);

  switch (property_id) {
  case PROP_FILTER_ADAPTIVE:
    phosh_app_grid_set_filter_adaptive (self, g_value_get_boolean (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_app_grid_get_property (GObject    *object,
                             guint       property_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  PhoshAppGrid *self = PHOSH_APP_GRID (object);
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  switch (property_id) {
  case PROP_FILTER_ADAPTIVE:
    g_value_set_boolean (value, priv->filter_adaptive);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
show_main_grid (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GtkFlowBoxChild *button = NULL;
  hdy_deck_set_visible_child_name (HDY_DECK (priv->deck), "main_grid");

  if (priv->open_folder == NULL)
    return;

  g_signal_handlers_disconnect_by_data (priv->folder_model, self);
  priv->folder_model = NULL;
  g_clear_object (&priv->open_folder);

  if (priv->open_folder_idx == -1)
    return;

  /* Focus the first valid button from current index to 0 */
  for (int idx = priv->open_folder_idx; idx >= 0; idx--) {
    button = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (priv->apps), idx);
    if (button != NULL)
      break;
  }

  gtk_widget_grab_focus (GTK_WIDGET (button));
}


static void
show_folder_page (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  const char *page_name;
  GtkFlowBoxChild *button = NULL;

  if (g_app_info_should_show (G_APP_INFO (priv->open_folder))) {
    page_name = "folder_grid";
    button = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (priv->folder_apps), 0);
  } else {
    g_autofree char *label;
    page_name = "empty_folder";
    label = g_strdup_printf ("%s folder is empty", phosh_folder_info_get_name (priv->open_folder));
    gtk_label_set_label (GTK_LABEL (priv->empty_folder_label), label);
  }

  gtk_stack_set_visible_child_name (GTK_STACK (priv->folder_stack), page_name);

  if (button != NULL)
    gtk_widget_grab_focus (GTK_WIDGET (button));
}


static void
app_launched_cb (GtkWidget *widget, GAppInfo *info, PhoshAppGrid *self)
{
  g_signal_emit (self, signals[APP_LAUNCHED], 0, info);
}


static GtkWidget *
create_folder_app_launcher (gpointer item, gpointer self)
{
  GtkWidget *btn;
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  btn = phosh_app_grid_button_new (G_APP_INFO (item));
  phosh_app_grid_button_set_folder_info (PHOSH_APP_GRID_BUTTON (btn), priv->open_folder);
  g_signal_connect (btn, "app-launched", G_CALLBACK (app_launched_cb), self);

  gtk_widget_set_visible (btn, TRUE);

  return btn;
}


static int
get_app_info_index (PhoshAppGrid *self, GAppInfo *app_info)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  for (int idx = 0;; idx++) {
    g_autoptr (GAppInfo) info = g_list_model_get_item (G_LIST_MODEL (priv->model), idx);

    if (info == NULL)
      return -1;

    if (g_app_info_equal (info, app_info))
      return idx;
  }

  return -1;
}


static void
folder_launched_cb (GtkWidget       *widget,
                    PhoshFolderInfo *info,
                    PhoshAppGrid    *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GListModel *model = phosh_folder_info_get_app_infos (info);

  hdy_deck_set_visible_child_name (HDY_DECK (priv->deck), "folder_page");
  g_object_bind_property (info, "name", priv->folder_name_label, "label", G_BINDING_SYNC_CREATE);
  priv->folder_model = model;
  g_set_object (&priv->open_folder, info);
  priv->open_folder_idx = get_app_info_index (self, G_APP_INFO (info));
  g_signal_connect_object (model,
                           "items-changed",
                           G_CALLBACK (show_folder_page),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_entry_set_text (GTK_ENTRY (priv->folder_name_entry), "");
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->folder_name_btn), FALSE);
  show_folder_page (self);

  gtk_flow_box_bind_model (GTK_FLOW_BOX (priv->folder_apps),
                           model,
                           create_folder_app_launcher,
                           self,
                           NULL);
}


static int
sort_apps (gconstpointer a, gconstpointer b, gpointer data)
{
  const char *empty = "";
  GAppInfo *info1 = G_APP_INFO (a);
  GAppInfo *info2 = G_APP_INFO (b);

  g_autofree char *s1 = g_utf8_casefold (g_app_info_get_name (info1), -1);
  g_autofree char *s2 = g_utf8_casefold (g_app_info_get_name (info2), -1);

  return g_utf8_collate (s1 ?: empty, s2 ?: empty);
}


static void
update_filter_adaptive_button (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv;
  const char *label, *icon_name;

  priv = phosh_app_grid_get_instance_private (self);
  if (priv->filter_adaptive) {
    label = _("Show All Apps");
    icon_name = "eye-open-negative-filled-symbolic";
  } else {
    label = _("Show Only Mobile Friendly Apps");
    icon_name = "eye-not-looking-symbolic";
  }

  gtk_label_set_label (GTK_LABEL (priv->btn_adaptive_lbl), label);
  gtk_image_set_from_icon_name (GTK_IMAGE (priv->btn_adaptive_img), icon_name, -1);
}


static void
on_filter_setting_changed (PhoshAppGrid *self,
                           GParamSpec   *pspec,
                           gpointer     *unused)
{
  PhoshAppGridPrivate *priv;
  gboolean show;

  g_return_if_fail (PHOSH_IS_APP_GRID (self));

  priv = phosh_app_grid_get_instance_private (self);

  g_strfreev (priv->force_adaptive);
  priv->force_adaptive = g_settings_get_strv (priv->settings, "force-adaptive");
  priv->filter_mode = g_settings_get_flags (priv->settings, "app-filter-mode");

  show = !!(priv->filter_mode & PHOSH_APP_FILTER_MODE_FLAGS_ADAPTIVE);
  gtk_widget_set_visible (priv->btn_adaptive, show);

  gtk_filter_list_model_refilter (priv->model);
}


static gboolean
filter_adaptive (PhoshAppGrid *self, GDesktopAppInfo *info)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  g_autofree char *mobile = NULL;
  const char *id;

  if (!(priv->filter_mode & PHOSH_APP_FILTER_MODE_FLAGS_ADAPTIVE))
    return TRUE;

  if (!priv->filter_adaptive)
    return TRUE;

  mobile = g_desktop_app_info_get_string (G_DESKTOP_APP_INFO (info), "X-Purism-FormFactor");
  if (mobile && strcasestr (mobile, "mobile;"))
    return TRUE;

  g_free (mobile);
  mobile = g_desktop_app_info_get_string (G_DESKTOP_APP_INFO (info), "X-KDE-FormFactor");
  if (mobile && strcasestr (mobile, "handset;"))
    return TRUE;

  id = g_app_info_get_id (G_APP_INFO (info));
  if (id && g_strv_contains ((const char * const*)priv->force_adaptive, id))
    return TRUE;

  return FALSE;
}


static gboolean
search_apps (gpointer item, gpointer data)
{
  PhoshAppGrid *self = data;
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GAppInfo *info = item;
  const char *search = NULL;

  g_return_val_if_fail (priv != NULL, TRUE);
  g_return_val_if_fail (priv->search != NULL, TRUE);

  search = priv->search_string;

  if (G_IS_DESKTOP_APP_INFO (info)) {
    if (!filter_adaptive (self, G_DESKTOP_APP_INFO (info)))
      return FALSE;
  }

  /* filter out favorites when not searching */
  if (gm_str_is_null_or_empty (search)) {
    if (PHOSH_IS_FOLDER_INFO (info))
      return phosh_folder_info_refilter (PHOSH_FOLDER_INFO (info), search);
    if (phosh_favorite_list_model_app_is_favorite (NULL, info))
      return FALSE;

    return TRUE;
  }

  if (PHOSH_IS_FOLDER_INFO (info))
    return phosh_folder_info_refilter (PHOSH_FOLDER_INFO (info), search);

  return phosh_util_matches_app_info (info, search);
}


static GtkWidget *
create_favorite_launcher (gpointer item, gpointer self)
{
  GtkWidget *btn = phosh_app_grid_button_new_favorite (G_APP_INFO (item));

  g_signal_connect (btn, "app-launched", G_CALLBACK (app_launched_cb), self);

  gtk_widget_set_visible (btn, TRUE);

  return btn;
}

static void
toggle_favorites_revealer (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  PhoshFavoriteListModel *favorites = phosh_favorite_list_model_get_default ();
  gboolean criteria;

  /* Hide favorites when there are none or a search is in progress */
  criteria = ((g_list_model_get_n_items (G_LIST_MODEL (favorites)) == 0) ||
              !gm_str_is_null_or_empty (priv->search_string));

  gtk_revealer_set_reveal_child (GTK_REVEALER (priv->favs_revealer), !criteria);
}


static void
favorites_changed (GListModel *list, guint pos, guint removed, guint added, PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  toggle_favorites_revealer (self);

  /* We don't show favorites in the main list, filter them out */
  gtk_filter_list_model_refilter (priv->model);
}


static GtkWidget *
create_launcher (gpointer item, gpointer self)
{
  GtkWidget *btn;

  if (PHOSH_IS_FOLDER_INFO (item)) {
    btn = phosh_app_grid_folder_button_new_from_folder_info (item);
    g_signal_connect (btn, "folder-launched", G_CALLBACK (folder_launched_cb), self);
  } else {
    btn = phosh_app_grid_button_new (G_APP_INFO (item));
    g_signal_connect (btn, "app-launched", G_CALLBACK (app_launched_cb), self);
  }

  gtk_widget_set_visible (btn, TRUE);

  return btn;
}


static void
phosh_app_grid_init (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GtkSortListModel *sorted;
  PhoshFavoriteListModel *favorites;
  g_autoptr (GAction) action = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  favorites = phosh_favorite_list_model_get_default ();

  gtk_flow_box_bind_model (GTK_FLOW_BOX (priv->favs),
                           G_LIST_MODEL (favorites),
                           create_favorite_launcher,
                           self,
                           NULL);
  g_signal_connect (favorites,
                    "items-changed",
                    G_CALLBACK (favorites_changed),
                    self);

  /* fill the grid with apps */
  sorted = gtk_sort_list_model_new (G_LIST_MODEL (phosh_app_list_model_get_default ()),
                                    sort_apps,
                                    NULL,
                                    NULL);
  priv->model = gtk_filter_list_model_new (G_LIST_MODEL (sorted),
                                           search_apps,
                                           self,
                                           NULL);
  g_object_unref (sorted);
  gtk_flow_box_bind_model (GTK_FLOW_BOX (priv->apps),
                           G_LIST_MODEL (priv->model),
                           create_launcher,
                           self,
                           NULL);

  priv->settings = g_settings_new ("sm.puri.phosh");
  g_object_connect (priv->settings,
                    "swapped-signal::changed::force-adaptive", on_filter_setting_changed, self,
                    "swapped-signal::changed::app-filter-mode", on_filter_setting_changed, self,
                    NULL);
  on_filter_setting_changed (self, NULL, NULL);

  priv->actions = g_simple_action_group_new ();
  gtk_widget_insert_action_group (GTK_WIDGET (self), "app-grid", G_ACTION_GROUP (priv->actions));
  action = G_ACTION (g_property_action_new ("filter-adaptive", self, "filter-adaptive"));
  g_action_map_add_action (G_ACTION_MAP (priv->actions), action);

  toggle_favorites_revealer (self);
}


static void
phosh_app_grid_dispose (GObject *object)
{
  PhoshAppGrid *self = PHOSH_APP_GRID (object);
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  g_clear_object (&priv->open_folder);
  g_clear_object (&priv->actions);
  g_clear_object (&priv->model);
  g_clear_object (&priv->settings);
  g_clear_handle_id (&priv->debounce, g_source_remove);

  G_OBJECT_CLASS (phosh_app_grid_parent_class)->dispose (object);
}


static void
phosh_app_grid_finalize (GObject *object)
{
  PhoshAppGrid *self = PHOSH_APP_GRID (object);
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  g_clear_pointer (&priv->search_string, g_free);
  g_strfreev (priv->force_adaptive);

  G_OBJECT_CLASS (phosh_app_grid_parent_class)->finalize (object);
}


static gboolean
phosh_app_grid_key_press_event (GtkWidget   *widget,
                              GdkEventKey *event)
{
  PhoshAppGrid *self = PHOSH_APP_GRID (widget);
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  /* Don't search when folder is open */
  if (priv->open_folder != NULL)
    return GDK_EVENT_PROPAGATE;

  return gtk_search_entry_handle_event (GTK_SEARCH_ENTRY (priv->search),
                                        (GdkEvent *) event);
}


static void
do_search (gpointer data)
{
  PhoshAppGrid *self = data;
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GtkAdjustment *adjustment;
  gboolean search_active = TRUE;

  if (gm_str_is_null_or_empty (priv->search_string)) {
    adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolled_window));
    gtk_adjustment_set_value (adjustment, 0);
    search_active = FALSE;
  }

  phosh_util_toggle_style_class (GTK_WIDGET (priv->apps), ACTIVE_SEARCH_CLASS, search_active);
  toggle_favorites_revealer (self);
  gtk_filter_list_model_refilter (priv->model);

  priv->debounce = 0;
}


static void
on_folder_edit_toggled (PhoshAppGrid *self, GtkToggleButton *toggle_btn)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  gboolean active = gtk_toggle_button_get_active (toggle_btn);

  if (active) {
    const char *folder_name = phosh_folder_info_get_name (priv->open_folder);
    gtk_entry_set_text (GTK_ENTRY (priv->folder_name_entry), folder_name);
    gtk_widget_grab_focus (priv->folder_name_entry);
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->folder_name_img), "checkmark-symbolic", -1);
  } else {
    const char *folder_name = gtk_entry_get_text (GTK_ENTRY (priv->folder_name_entry));
    if (gm_str_is_null_or_empty (folder_name))
      return;
    phosh_folder_info_set_name (priv->open_folder, folder_name);
    gtk_entry_set_text (GTK_ENTRY (priv->folder_name_entry), "");
    gtk_image_set_from_icon_name (GTK_IMAGE (priv->folder_name_img), "document-edit-symbolic", -1);
  }
}


static void
on_folder_entry_activated (PhoshAppGrid *self, GtkEntry *entry)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->folder_name_btn), FALSE);
}


static void
on_search_changed (GtkSearchEntry *entry,
                   PhoshAppGrid   *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  const char *search = gtk_entry_get_text (GTK_ENTRY (entry));

  g_clear_pointer (&priv->search_string, g_free);

  g_clear_handle_id (&priv->debounce, g_source_remove);

  if (search && *search != '\0') {
    priv->search_string = g_utf8_casefold (search, -1);

    /* GtkSearchEntry already adds 150ms of delay, but it's too little
     * so add a bit more until searching is faster and/or non-blocking */
    priv->debounce = g_timeout_add_once (SEARCH_DEBOUNCE, do_search, self);
    g_source_set_name_by_id (priv->debounce, "[phosh] debounce app grid search (search-changed)");
  } else {
    /* don't add the delay when the entry got cleared */
    do_search (self);
  }
}


static void
on_search_preedit_changed (GtkSearchEntry *entry,
                           const char     *preedit,
                           PhoshAppGrid   *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  g_clear_pointer (&priv->search_string, g_free);

  if (preedit && *preedit != '\0')
    priv->search_string = g_utf8_casefold (preedit, -1);

  g_clear_handle_id (&priv->debounce, g_source_remove);

  priv->debounce = g_timeout_add_once (SEARCH_DEBOUNCE + DEFAULT_GTK_DEBOUNCE, do_search, self);
  g_source_set_name_by_id (priv->debounce, "[phosh] debounce app grid search (preedit-changed)");
}


static void
on_search_activated (GtkSearchEntry *entry,
                     PhoshAppGrid   *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);
  GtkFlowBoxChild *child;

  if (!gtk_widget_has_focus (GTK_WIDGET (entry)))
    return;

  /* Don't activate when there isn't an active search */
  if (!priv->search_string || *priv->search_string == '\0')
    return;

  child = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (priv->apps), 0);

  /* No results */
  if (child == NULL)
    return;

  if (G_LIKELY (PHOSH_IS_APP_GRID_BUTTON (child))) {
    gtk_widget_activate (GTK_WIDGET (child));
  } else {
    g_critical ("Unexpected child type, %s",
                g_type_name (G_TYPE_FROM_INSTANCE (child)));
  }
}


static gboolean
on_search_lost_focus (GtkWidget    *widget,
                      GdkEvent     *event,
                      PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  phosh_util_toggle_style_class (GTK_WIDGET (priv->apps), ACTIVE_SEARCH_CLASS, FALSE);

  return GDK_EVENT_PROPAGATE;
}


static gboolean
on_search_gained_focus (GtkWidget    *widget,
                        GdkEvent     *event,
                        PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv = phosh_app_grid_get_instance_private (self);

  if (!gm_str_is_null_or_empty (priv->search_string))
    phosh_util_toggle_style_class (GTK_WIDGET (priv->apps), ACTIVE_SEARCH_CLASS, TRUE);

  return GDK_EVENT_PROPAGATE;
}


static void
phosh_app_grid_class_init (PhoshAppGridClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = phosh_app_grid_dispose;
  object_class->finalize = phosh_app_grid_finalize;

  object_class->set_property = phosh_app_grid_set_property;
  object_class->get_property = phosh_app_grid_get_property;

  widget_class->key_press_event = phosh_app_grid_key_press_event;

  /**
   * PhoshAppGrid:filter-adaptive:
   *
   * Whether only adaptive apps should be shown
   */
  props[PROP_FILTER_ADAPTIVE] =
    g_param_spec_boolean ("filter-adaptive", "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  signals[APP_LAUNCHED] =
    g_signal_new ("app-launched",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_APP_INFO);

  gtk_widget_class_set_template_from_resource (widget_class, "/mobi/phosh/ui/app-grid.ui");

  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, apps);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, btn_adaptive);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, btn_adaptive_img);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, btn_adaptive_lbl);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, deck);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, empty_folder_label);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, favs);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, favs_revealer);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_apps);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_name_btn);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_name_img);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_name_entry);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_name_label);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, folder_stack);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, scrolled_window);
  gtk_widget_class_bind_template_child_private (widget_class, PhoshAppGrid, search);

  gtk_widget_class_bind_template_callback (widget_class, on_folder_edit_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_folder_entry_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_search_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_search_preedit_changed);
  gtk_widget_class_bind_template_callback (widget_class, on_search_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_search_gained_focus);
  gtk_widget_class_bind_template_callback (widget_class, on_search_lost_focus);
  gtk_widget_class_bind_template_callback (widget_class, show_main_grid);

  gtk_widget_class_set_css_name (widget_class, "phosh-app-grid");
}


GtkWidget *
phosh_app_grid_new (void)
{
  return g_object_new (PHOSH_TYPE_APP_GRID, NULL);
}


void
phosh_app_grid_reset (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv;
  GtkAdjustment *adjustment;

  g_return_if_fail (PHOSH_IS_APP_GRID (self));

  priv = phosh_app_grid_get_instance_private (self);

  adjustment = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (priv->scrolled_window));

  gtk_adjustment_set_value (adjustment, 0);
  gtk_entry_set_text (GTK_ENTRY (priv->search), "");
  g_clear_pointer (&priv->search_string, g_free);

  /* Do not focus last folder */
  priv->open_folder_idx = -1;
  /* Set duration to 0 to avoid the simultaneous animation of grid sliding up and deck sliding
   * right. If not done, it feels like the deck is spiraling diagonally up. */
  hdy_deck_set_transition_duration (HDY_DECK (priv->deck), 0);
  show_main_grid (self);
  hdy_deck_set_transition_duration (HDY_DECK (priv->deck), 200);
}


void
phosh_app_grid_focus_search (PhoshAppGrid *self)
{
  PhoshAppGridPrivate *priv;

  g_return_if_fail (PHOSH_IS_APP_GRID (self));
  priv = phosh_app_grid_get_instance_private (self);
  gtk_widget_grab_focus (priv->search);
}


gboolean
phosh_app_grid_handle_search (PhoshAppGrid *self, GdkEvent *event)
{
  PhoshAppGridPrivate *priv;
  gboolean ret;

  g_return_val_if_fail (PHOSH_IS_APP_GRID (self), GDK_EVENT_PROPAGATE);
  priv = phosh_app_grid_get_instance_private (self);

  /* Prevent stealing of focus when folder is open and it's name entry is active */
  if (priv->open_folder != NULL)
    return GDK_EVENT_PROPAGATE;

  ret = gtk_search_entry_handle_event (GTK_SEARCH_ENTRY (priv->search), event);
  if (ret == GDK_EVENT_STOP)
    gtk_entry_grab_focus_without_selecting (GTK_ENTRY (priv->search));

  return ret;
}


void
phosh_app_grid_set_filter_adaptive (PhoshAppGrid *self, gboolean enable)
{
  PhoshAppGridPrivate *priv;

  g_debug ("Filter-adaptive: %d", enable);

  g_return_if_fail (PHOSH_IS_APP_GRID (self));
  priv = phosh_app_grid_get_instance_private (self);

  if (priv->filter_adaptive == enable)
    return;

  priv->filter_adaptive = enable;
  update_filter_adaptive_button (self);

  gtk_filter_list_model_refilter (priv->model);
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FILTER_ADAPTIVE]);
}
