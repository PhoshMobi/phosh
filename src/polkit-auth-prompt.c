/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 *
 * Based on gnome-shell's shell-keyring-prompt.c
 * Author: Stef Walter <stefw@gnome.org>
 */

#define G_LOG_DOMAIN "phosh-polkit-auth-prompt"

#include "phosh-config.h"

#include "accounts-dbus.h"
#include "accounts-user-dbus.h"
#include "polkit-auth-prompt.h"

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>

#include <glib/gi18n.h>
#include <handy.h>

/**
 * PhoshPolkitAuthPrompt:
 *
 * A modal prompt for policy kit authentication
 *
 * The #PhoshPolkitAuthPrompt is used to ask policy kit authentication.
 * This handles the interaction with PolkitAgentSession.
 */

enum {
  PROP_0,
  PROP_ACTION_ID,
  PROP_COOKIE,
  PROP_MESSAGE,
  PROP_ICON_NAME,
  PROP_USER_NAMES,
  PROP_LAST_PROP,
};
static GParamSpec *props[PROP_LAST_PROP];

enum {
  DONE,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _PhoshPolkitAuthPrompt {
  PhoshSystemModalDialog parent;

  GtkWidget      *lbl_message;
  GtkWidget      *lbl_user_name;
  GtkWidget      *lbl_password;
  GtkWidget      *lbl_info;
  GtkWidget      *img_icon;
  GtkWidget      *btn_authenticate;
  GtkWidget      *spinner_authenticate;
  GtkWidget      *btn_cancel;
  GtkWidget      *entry_password;
  GtkEntryBuffer *password_buffer;

  char *action_id;
  char *message;
  char *icon_name;
  char *cookie;
  GStrv user_names;

  const char *user_name;
  GCancellable *cancel;

  PolkitIdentity     *identity;
  PolkitAgentSession *session;

  gboolean done_emitted;
};

G_DEFINE_TYPE (PhoshPolkitAuthPrompt, phosh_polkit_auth_prompt, PHOSH_TYPE_SYSTEM_MODAL_DIALOG);


static void phosh_polkit_auth_prompt_initiate (PhoshPolkitAuthPrompt *self);


static void
set_action_id (PhoshPolkitAuthPrompt *self, const char *action_id)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  if (!g_strcmp0 (self->action_id, action_id))
    return;

  g_clear_pointer (&self->action_id, g_free);
  self->action_id = g_strdup (action_id);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ACTION_ID]);
}


static void
set_cookie (PhoshPolkitAuthPrompt *self, const char *cookie)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  if (!g_strcmp0 (self->cookie, cookie))
    return;

  g_clear_pointer (&self->cookie, g_free);
  self->cookie = g_strdup (cookie);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_COOKIE]);
}


static void
set_message (PhoshPolkitAuthPrompt *self, const char *message)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  if (!g_strcmp0 (self->message, message))
    return;

  g_clear_pointer (&self->message, g_free);
  self->message = g_strdup (message);
  gtk_label_set_label (GTK_LABEL (self->lbl_message), message ?: "");

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_MESSAGE]);
}


static void
set_icon_name (PhoshPolkitAuthPrompt *self, const char *icon_name)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  if (!g_strcmp0 (self->icon_name, icon_name))
    return;

  g_clear_pointer (&self->icon_name, g_free);
  self->icon_name = g_strdup (icon_name);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_ICON_NAME]);
}


static void
on_accounts_user_proxy_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshPolkitAuthPrompt *self = data;
  g_autoptr (GError) error = NULL;
  g_autoptr (PhoshDBusAccountsUser) user = NULL;
  const char *real_name = NULL;
  const char *icon_path = NULL;
  g_autoptr (GFile) icon_file = NULL;
  g_autoptr (GIcon) icon = NULL;

  user = phosh_dbus_accounts_user_proxy_new_finish (result, &error);
  if (error) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_debug ("Skipping create user proxy: %s", error->message);
    else
      g_warning ("Failed to create user proxy %s: %s", self->user_name, error->message);
    return;
  }
  g_debug ("Created proxy for user name: %s", self->user_name);

  real_name = phosh_dbus_accounts_user_get_real_name (user);
  hdy_avatar_set_text (HDY_AVATAR (self->img_icon), real_name);
  gtk_label_set_text (GTK_LABEL (self->lbl_user_name), real_name);

  icon_path = phosh_dbus_accounts_user_get_icon_file (user);
  icon_file = g_file_new_for_path (icon_path);
  icon = g_file_icon_new (icon_file);
  hdy_avatar_set_loadable_icon (HDY_AVATAR (self->img_icon), G_LOADABLE_ICON (icon));
}


static void
on_find_user_by_name_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  g_autoptr (PhoshDBusAccounts) accounts = PHOSH_DBUS_ACCOUNTS (source);
  PhoshPolkitAuthPrompt *self = data;
  g_autoptr (GError) error = NULL;
  g_autofree char *user_path = NULL;

  phosh_dbus_accounts_call_find_user_by_name_finish (accounts, &user_path, result, &error);
  if (error) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_debug ("Skipping find user: %s", error->message);
    else
      g_warning ("Failed to find user %s: %s", self->user_name, error->message);
    return;
  }
  g_debug ("Found user for user name: %s at %s", self->user_name, user_path);

  g_debug ("Creating proxy for user name: %s", self->user_name);
  phosh_dbus_accounts_user_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                              G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                              "org.freedesktop.Accounts",
                                              user_path,
                                              self->cancel,
                                              on_accounts_user_proxy_ready,
                                              self);
}


static void
on_accounts_proxy_ready (GObject *source, GAsyncResult *result, gpointer data)
{
  PhoshDBusAccounts *accounts;
  PhoshPolkitAuthPrompt *self = data;
  g_autoptr (GError) error = NULL;

  accounts = phosh_dbus_accounts_proxy_new_finish (result, &error);
  if (error) {
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_debug ("Skipping create Accounts proxy: %s", error->message);
    else
      g_warning ("Failed to create Accounts proxy: %s", error->message);
    return;
  }
  g_debug ("Created Accounts proxy for user name: %s", self->user_name);

  g_debug ("Finding user for user name: %s", self->user_name);
  phosh_dbus_accounts_call_find_user_by_name (accounts,
                                              self->user_name,
                                              self->cancel,
                                              on_find_user_by_name_ready,
                                              self);
}


static void
set_user_names (PhoshPolkitAuthPrompt *self, const GStrv user_names)
{
  const char *user_name = NULL;
  GError *err = NULL;
  int len;

  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  /* if both are NULL */
  if (self->user_names == user_names)
    return;

  g_strfreev (self->user_names);
  self->user_names = g_strdupv (user_names);

  len = g_strv_length (self->user_names);
  if (len == 0) {
    user_name = "unknown";
  } else if (len == 1) {
    user_name = self->user_names[0];
  } else {
    g_debug ("Received %d user names, only using one", len);
    user_name = g_get_user_name ();

    if (!g_strv_contains ((const char *const *)user_names, user_name))
      user_name = "root";

    if (!g_strv_contains ((const char *const *)user_names, user_name))
      user_name = user_names[0];
  }

  self->identity = polkit_unix_user_new_for_name (user_name, &err);
  if (!self->identity) {
    g_warning ("Failed to create identity: %s", err->message);
    g_clear_error (&err);
    return;
  }

  hdy_avatar_set_text (HDY_AVATAR (self->img_icon), user_name);
  gtk_label_set_text (GTK_LABEL (self->lbl_user_name), user_name);

  self->user_name = user_name;
  g_debug ("Creating Accounts proxy for user name: %s", self->user_name);
  phosh_dbus_accounts_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                                         (G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS),
                                         "org.freedesktop.Accounts",
                                         "/org/freedesktop/Accounts",
                                         self->cancel,
                                         on_accounts_proxy_ready,
                                         self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_USER_NAMES]);
}


static void
phosh_polkit_auth_prompt_set_property (GObject      *object,
                                       guint         property_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  PhoshPolkitAuthPrompt *self = PHOSH_POLKIT_AUTH_PROMPT (object);

  switch (property_id) {
  case PROP_ACTION_ID:
    set_action_id (self, g_value_get_string (value));
    break;
  case PROP_COOKIE:
    set_cookie (self, g_value_get_string (value));
    break;
  case PROP_MESSAGE:
    set_message (self, g_value_get_string (value));
    break;
  case PROP_ICON_NAME:
    set_icon_name (self, g_value_get_string (value));
    break;
  case PROP_USER_NAMES:
    set_user_names (self, g_value_get_boxed (value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_polkit_auth_prompt_get_property (GObject    *object,
                                       guint       property_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  PhoshPolkitAuthPrompt *self = PHOSH_POLKIT_AUTH_PROMPT (object);

  switch (property_id) {
  case PROP_ACTION_ID:
    g_value_set_string (value, self->action_id ?: "");
    break;
  case PROP_COOKIE:
    g_value_set_string (value, self->cookie ?: "");
    break;
  case PROP_MESSAGE:
    g_value_set_string (value, self->message ?: "");
    break;
  case PROP_ICON_NAME:
    g_value_set_string (value, self->icon_name ?: "");
    break;
  case PROP_USER_NAMES:
    g_value_set_boxed (value, self->user_names ? g_strdupv (self->user_names) : NULL);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
emit_done (PhoshPolkitAuthPrompt *self, gboolean cancelled)
{
  g_debug ("Emitting done. Cancelled: %d", cancelled);

  if (!self->done_emitted) {
    self->done_emitted = TRUE;
    g_signal_emit (self, signals[DONE], 0 /* detail */, cancelled);
  }
}


static void
on_auth_session_request (PhoshPolkitAuthPrompt *self,
                         char                  *request,
                         gboolean               echo_on,
                         gpointer               user_data)
{
  g_debug ("Request: %s, echo: %d", request, echo_on);

  if (self->done_emitted)
    return;

  /* Translate the most common request */
  if (!g_strcmp0 (request, "Password: ") || !g_strcmp0 (request, "Password:"))
    request = _("Password:");

  gtk_label_set_text (GTK_LABEL (self->lbl_password), request);
  gtk_entry_set_visibility (GTK_ENTRY (self->entry_password), echo_on);
  gtk_entry_set_text (GTK_ENTRY (self->entry_password), "");
  gtk_widget_grab_focus (self->entry_password);
}

static void
on_auth_session_show_error (PhoshPolkitAuthPrompt *self,
                            char                  *text,
                            PolkitAgentSession    *session)
{
  g_debug ("%s", text);
  gtk_label_set_text (GTK_LABEL (self->lbl_info), text);
}


static void
on_auth_session_show_info (PhoshPolkitAuthPrompt *self,
                           char                  *text,
                           PolkitAgentSession    *session)
{
  g_debug ("%s", text);
  gtk_label_set_text (GTK_LABEL (self->lbl_info), text);
}


static void
on_auth_session_completed (PhoshPolkitAuthPrompt *self,
                           gboolean               gained_auth,
                           PolkitAgentSession    *session)
{
  g_debug ("Gained auth: %d", gained_auth);
  gtk_spinner_stop (GTK_SPINNER (self->spinner_authenticate));
  gtk_widget_set_visible (self->spinner_authenticate, FALSE);

  if (self->done_emitted)
    return;

  if (gained_auth) {
    emit_done (self, FALSE);
  } else {
    const char *info = gtk_label_get_text (GTK_LABEL (self->lbl_info));

    if (info == NULL || !g_strcmp0 (info, "")) {
      gtk_label_set_text (GTK_LABEL (self->lbl_info),
                          _("Sorry, that didn’t work. Please try again."));
    }
    /* Start new auth session */
    g_signal_handlers_disconnect_by_data (session, self);
    phosh_polkit_auth_prompt_initiate (self);
  }
}


static void
phosh_polkit_auth_prompt_initiate (PhoshPolkitAuthPrompt *self)
{
  g_return_if_fail (self->identity);
  g_return_if_fail (self->cookie);

  self->session = polkit_agent_session_new (self->identity,
                                            self->cookie);

  g_signal_connect_swapped (self->session,
                            "request",
                            G_CALLBACK (on_auth_session_request),
                            self);

  g_signal_connect_swapped (self->session,
                            "show-error",
                            G_CALLBACK (on_auth_session_show_error),
                            self);

  g_signal_connect_swapped (self->session,
                            "show-info",
                            G_CALLBACK (on_auth_session_show_info),
                            self);

  g_signal_connect_swapped (self->session,
                            "completed",
                            G_CALLBACK (on_auth_session_completed),
                            self);

  polkit_agent_session_initiate (self->session);
}


static void
on_dialog_canceled (PhoshPolkitAuthPrompt *self, gpointer unused)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (self));

  polkit_agent_session_cancel (self->session);
  emit_done (self, TRUE);
}


static void
on_btn_authenticate_clicked (PhoshPolkitAuthPrompt *self, GtkButton *btn)
{
  const char *password;

  password = gtk_entry_buffer_get_text (self->password_buffer);

  if (!password || strlen (password) == 0)
    return;

  gtk_label_set_text (GTK_LABEL (self->lbl_info), "");
  gtk_widget_set_visible (self->spinner_authenticate, TRUE);
  gtk_spinner_start (GTK_SPINNER (self->spinner_authenticate));
  polkit_agent_session_response (self->session, password);
}


static void
phosh_polkit_auth_prompt_dispose (GObject *object)
{
  PhoshPolkitAuthPrompt *self = PHOSH_POLKIT_AUTH_PROMPT (object);

  g_cancellable_cancel (self->cancel);
  g_clear_object (&self->cancel);
  self->user_name = NULL;
  g_clear_object (&self->identity);
  g_clear_object (&self->session);

  G_OBJECT_CLASS (phosh_polkit_auth_prompt_parent_class)->dispose (object);
}


static void
phosh_polkit_auth_prompt_finalize (GObject *object)
{
  PhoshPolkitAuthPrompt *self = PHOSH_POLKIT_AUTH_PROMPT (object);

  g_free (self->message);
  g_free (self->icon_name);
  g_free (self->action_id);
  g_free (self->cookie);
  g_strfreev (self->user_names);

  G_OBJECT_CLASS (phosh_polkit_auth_prompt_parent_class)->finalize (object);
}


static void
phosh_polkit_auth_prompt_constructed (GObject *object)
{
  PhoshPolkitAuthPrompt *self = PHOSH_POLKIT_AUTH_PROMPT (object);

  G_OBJECT_CLASS (phosh_polkit_auth_prompt_parent_class)->constructed (object);

  self->cancel = g_cancellable_new ();

  self->password_buffer = gcr_secure_entry_buffer_new ();
  gtk_entry_set_buffer (GTK_ENTRY (self->entry_password),
                        GTK_ENTRY_BUFFER (self->password_buffer));

  phosh_polkit_auth_prompt_initiate (self);
}


static void
phosh_polkit_auth_prompt_class_init (PhoshPolkitAuthPromptClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = phosh_polkit_auth_prompt_get_property;
  object_class->set_property = phosh_polkit_auth_prompt_set_property;
  object_class->constructed = phosh_polkit_auth_prompt_constructed;
  object_class->dispose = phosh_polkit_auth_prompt_dispose;
  object_class->finalize = phosh_polkit_auth_prompt_finalize;

  /**
   * PolkitAuthPrompt:action-id:
   *
   * The prompt's action id
   */
  props[PROP_ACTION_ID] =
    g_param_spec_string ("action-id", "", "",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);
  /**
   * PolkitAuthPrompt:cookie:
   *
   * The prompt's cookie
   */
  props[PROP_COOKIE] =
    g_param_spec_string ("cookie", "", "",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);
  /**
   * PolkitAuthPrompt:message:
   *
   * The prompt's message
   */
  props[PROP_MESSAGE] =
    g_param_spec_string ("message", "", "",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);
  /**
   * PolkitAuthPrompt:icon-name:
   *
   * The prompt's icon name
   */
  props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name", "", "",
                         "",
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);
  /**
   * PolkitAuthPrompt:user-names:
   *
   * The user names to authenticate as
   */
  props[PROP_USER_NAMES] =
    g_param_spec_boxed ("user-names", "", "",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);

  /**
   * PhoshPolkitAuthPrompt::done:
   * @self: The #PhoshPolkitAuthPrompt instance.
   * @cancelled: Whether the dialog was cancelled
   *
   * This signal is emitted when the prompt can be closed. The cancelled
   * argument indicates whether the prompt was cancelled.
   */
  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (klass),
                                G_SIGNAL_RUN_LAST,
                                0, NULL, NULL, NULL,
                                G_TYPE_NONE,
                                1, G_TYPE_BOOLEAN);

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/ui/polkit-auth-prompt.ui");
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, btn_authenticate);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, btn_cancel);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, entry_password);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, img_icon);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, lbl_info);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, lbl_password);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, lbl_user_name);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, spinner_authenticate);
  gtk_widget_class_bind_template_child (widget_class, PhoshPolkitAuthPrompt, lbl_message);

  gtk_widget_class_bind_template_callback (widget_class, on_btn_authenticate_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_dialog_canceled);
}


static void
phosh_polkit_auth_prompt_init (PhoshPolkitAuthPrompt *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}


GtkWidget *
phosh_polkit_auth_prompt_new (const char *action_id,
                              const char *message,
                              const char *icon_name,
                              const char *cookie,
                              GStrv       user_names)
{
  return g_object_new (PHOSH_TYPE_POLKIT_AUTH_PROMPT,
                       "action-id", action_id,
                       "cookie", cookie,
                       "message", message,
                       "icon-name", icon_name,
                       "user-names", user_names,
                       NULL);
}
