/*
 * Copyright (C) 2019 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-polkit-auth-agent"

#include "phosh-config.h"

#include "polkit-auth-agent.h"
#include "polkit-auth-prompt.h"
#include "shell-priv.h"

#include <sys/types.h>
#include <pwd.h>

#ifdef PHOSH_POLKIT_AUTH_DEBUG
#define auth_debug(...) g_debug(__VA_ARGS__)
#else
static void auth_debug(const char *str, ...) {}
#endif

/**
 * PhoshPolkitAuthAgent:
 *
 * PolicyKit Authentication Agent
 *
 * The #PhoshPolkitAuthAgent is responsible for handling policy kit
 * interaction so the shell can work as a authentication agent.
 */

typedef struct {
  /* not holding ref */
  PhoshPolkitAuthAgent *agent;
  GCancellable *cancellable;
  gulong handler_id;

  /* copies */
  char           *action_id;
  char           *message;
  char           *icon_name;
  PolkitDetails  *details;
  char           *cookie;
  GList          *identities;

  GTask *simple;
} AuthRequest;

struct _PhoshPolkitAuthAgent
{
  PolkitAgentListener parent;

  GList *scheduled_requests;
  AuthRequest *current_request;
  PhoshPolkitAuthPrompt *current_prompt;

  gpointer handle;
  GCancellable *cancellable;
};
G_DEFINE_TYPE (PhoshPolkitAuthAgent, phosh_polkit_auth_agent, POLKIT_AGENT_TYPE_LISTENER);

static void auth_request_complete (AuthRequest *request, gboolean dismissed);


static void
agent_register_thread (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  PolkitAgentListener *self = POLKIT_AGENT_LISTENER (source_object);
  gpointer handle;
  GError *err = NULL;
  g_autoptr (PolkitSubject) subject = NULL;

  subject = polkit_unix_session_new_for_process_sync (getpid (),
                                                      cancellable,
                                                      &err);
  if (subject == NULL) {
    if (g_str_has_prefix (err->message, "No session for pid"))
      g_message ("PolKit failed to properly get our session: %s", err->message);
    else
      g_warning ("PolKit failed to properly get our session: %s", err->message);

    g_task_return_error (task, err);
    return;
  }

  handle = polkit_agent_listener_register (POLKIT_AGENT_LISTENER (self),
                                           POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                           subject,
                                           NULL, /* use default object path */
                                           cancellable,
                                           &err);

  if (handle)
    g_task_return_pointer (task, handle, polkit_agent_listener_unregister);
  else
    g_task_return_error (task, err);

}

static gpointer
agent_register_finish (PhoshPolkitAuthAgent *self,
                       GAsyncResult         *res,
                       GError              **error)
{
  return g_task_propagate_pointer (G_TASK (res), error);
}

static void
agent_register_async (PhoshPolkitAuthAgent *self,
                      GCancellable         *cancellable,
                      GAsyncReadyCallback   callback,
                      gpointer              user_data)
{
  g_autoptr (GTask) task = g_task_new (self, cancellable, callback, user_data);

  g_task_run_in_thread (task, agent_register_thread);
}

static void
on_agent_registered (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  PhoshPolkitAuthAgent *self = PHOSH_POLKIT_AUTH_AGENT (source_object);
  g_autoptr (GError) err = NULL;

  self->handle = agent_register_finish (self, res, &err);

  g_clear_object (&self->cancellable);

  if (!self->handle) {
    g_warning ("Auth agent failed to register: %s", err->message);
    return;
  }

  g_debug ("Polkit auth agent registered");
}

static void
auth_request_free (AuthRequest *request)
{
  g_free (request->action_id);
  g_free (request->message);
  g_free (request->icon_name);
  g_object_unref (request->details);
  g_free (request->cookie);
  g_list_foreach (request->identities, (GFunc) g_object_unref, NULL);
  g_list_free (request->identities);
  g_object_unref (request->simple);
  g_free (request);
}


static void
close_prompt (PhoshPolkitAuthAgent *self)
{
  g_clear_pointer ((PhoshSystemModalDialog**)&self->current_prompt,
                   phosh_system_modal_dialog_close);
}


static void
on_prompt_done (PhoshPolkitAuthPrompt *prompt, gboolean cancelled, AuthRequest *request)
{
  g_return_if_fail (PHOSH_IS_POLKIT_AUTH_PROMPT (prompt));

  close_prompt (request->agent);
  auth_request_complete (request, cancelled);
}


static void
auth_request_initiate (AuthRequest *request)
{
  g_auto(GStrv) user_names = NULL;
  GPtrArray *p;
  GList *l;

  p = g_ptr_array_new ();
  for (l = request->identities; l != NULL; l = l->next) {
      if (POLKIT_IS_UNIX_USER (l->data)) {
          PolkitUnixUser *user = POLKIT_UNIX_USER (l->data);
          int uid;
          char buf[4096];
          struct passwd pwd;
          struct passwd *ppwd;
          int ret;

          uid = polkit_unix_user_get_uid (user);
          ret = getpwuid_r (uid, &pwd, buf, sizeof (buf), &ppwd);
          if (!ret) {
            if (!g_utf8_validate (pwd.pw_name, -1, NULL))
              g_warning ("Invalid UTF-8 in username for uid %d. Skipping", uid);
            else
              g_ptr_array_add (p, g_strdup (pwd.pw_name));
          } else {
            g_warning ("Error looking up user name for uid %d: %d", uid, ret);
          }
      } else {
        g_warning ("Unsupporting identity of GType %s", g_type_name (G_TYPE_FROM_INSTANCE (l->data)));
      }
  }

  g_ptr_array_add (p, NULL);
  user_names = (char **) g_ptr_array_free (p, FALSE);

  g_debug("New prompt for %s", request->message);
  /* We must not issue a new prompt when there's one already */
  g_return_if_fail (!request->agent->current_prompt);
  request->agent->current_prompt = PHOSH_POLKIT_AUTH_PROMPT (
    phosh_polkit_auth_prompt_new (
      request->action_id,
      request->message,
      request->icon_name,
      request->cookie,
      user_names));

  g_signal_connect (request->agent->current_prompt,
                    "done",
                    G_CALLBACK (on_prompt_done),
                    request);

  /* Show widget when not locked and keep that in sync */
  g_object_bind_property (phosh_shell_get_default (), "locked",
                          request->agent->current_prompt, "visible",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
}

static void
maybe_process_next_request (PhoshPolkitAuthAgent *self)
{
  auth_debug ("cur=%p len(scheduled)=%d",
              self->current_request,
              g_list_length (self->scheduled_requests));

  if (self->current_request == NULL && self->scheduled_requests != NULL) {
    AuthRequest *request;

    request = self->scheduled_requests->data;

    self->current_request = request;
    self->scheduled_requests = g_list_remove (self->scheduled_requests, request);

    auth_debug ("initiating %s cookie %s", request->action_id, request->cookie);
    auth_request_initiate (request);
  }
}


static void
auth_request_complete (AuthRequest *request, gboolean dismissed)
{
  PhoshPolkitAuthAgent *self = request->agent;
  gboolean is_current = self->current_request == request;

  auth_debug ("completing %s %s cookie %s", is_current ? "current" : "scheduled",
              request->action_id, request->cookie);

  if (!is_current)
    self->scheduled_requests = g_list_remove (self->scheduled_requests, request);
  g_cancellable_disconnect (request->cancellable, request->handler_id);

  if (dismissed) {
    g_task_return_new_error (request->simple,
                             POLKIT_ERROR,
                             POLKIT_ERROR_CANCELLED,
                             _("Authentication dialog was dismissed by the user"));
  } else {
    g_task_return_boolean (request->simple, TRUE);
  }

  auth_request_free (request);

  if (is_current) {
    self->current_request = NULL;
    maybe_process_next_request (self);
  }
}

static gboolean
handle_cancelled_in_idle (gpointer user_data)
{
  AuthRequest *request = user_data;

  auth_debug ("cancelled %s cookie %s", request->action_id, request->cookie);
  if (request == request->agent->current_request)
    close_prompt (request->agent);

  auth_request_complete (request, FALSE);
  return FALSE;
}

/*
 * on_request_cancelled:
 *
 * This happens when the application requesting authentication
 * is closed.
 */
static void
on_request_cancelled (GCancellable *cancellable,
                      gpointer      user_data)
{
  AuthRequest *request = user_data;
  guint id;

  /*
   * post-pone to idle to handle GCancellable deadlock in
   * https://bugzilla.gnome.org/show_bug.cgi?id=642968
   * https://gitlab.gnome.org/GNOME/glib/issues/740
   */
  id = g_idle_add (handle_cancelled_in_idle, request);
  g_source_set_name_by_id (id, "[phosh] handle_cancelled_in_idle");
}


static void
initiate_authentication (PolkitAgentListener  *listener,
                         const char           *action_id,
                         const char           *message,
                         const char           *icon_name,
                         PolkitDetails        *details,
                         const char           *cookie,
                         GList                *identities,
                         GCancellable         *cancellable,
                         GAsyncReadyCallback   callback,
                         gpointer              user_data)
{
  PhoshPolkitAuthAgent *self = PHOSH_POLKIT_AUTH_AGENT (listener);
  AuthRequest *request;

  request = g_new0 (AuthRequest, 1);
  request->agent = self;
  request->action_id = g_strdup (action_id);
  request->message = g_strdup (message);
  request->icon_name = g_strdup (icon_name);
  request->details = g_object_ref (details);
  request->cookie = g_strdup (cookie);
  request->identities = g_list_copy (identities);
  g_list_foreach (request->identities, (GFunc) g_object_ref, NULL);
  request->simple = g_task_new (listener, NULL, callback, user_data);
  request->cancellable = cancellable;
  request->handler_id = g_cancellable_connect (request->cancellable,
                                               G_CALLBACK (on_request_cancelled),
                                               request,
                                               NULL); /* GDestroyNotify for request */

  auth_debug ("scheduling %s cookie %s", request->action_id, request->cookie);
  self->scheduled_requests = g_list_append (self->scheduled_requests, request);

  maybe_process_next_request (self);
}

static gboolean
initiate_authentication_finish (PolkitAgentListener  *listener,
                                GAsyncResult         *res,
                                GError              **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}


static void
phosh_polkit_auth_agent_constructed (GObject *object)
{
  PhoshPolkitAuthAgent *self = PHOSH_POLKIT_AUTH_AGENT (object);

  G_OBJECT_CLASS (phosh_polkit_auth_agent_parent_class)->constructed (object);

  self->cancellable = g_cancellable_new ();
  agent_register_async (self, self->cancellable, on_agent_registered, NULL);
}


static void
auth_request_dismiss (AuthRequest *request)
{
  auth_request_complete (request, TRUE);
}


static void
phosh_polkit_auth_agent_unregister (PhoshPolkitAuthAgent *self)
{
  if (self->scheduled_requests != NULL) {
    g_list_foreach (self->scheduled_requests, (GFunc)auth_request_dismiss, NULL);
    self->scheduled_requests = NULL;
  }

  if (self->current_request != NULL)
    auth_request_dismiss (self->current_request);

  polkit_agent_listener_unregister (self->handle);
  self->handle = NULL;
}


static void
phosh_polkit_auth_agent_dispose (GObject *object)
{
  PhoshPolkitAuthAgent *self = PHOSH_POLKIT_AUTH_AGENT (object);

  if (self->handle)
    phosh_polkit_auth_agent_unregister (self);

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (phosh_polkit_auth_agent_parent_class)->dispose (object);
}


static void
phosh_polkit_auth_agent_class_init (PhoshPolkitAuthAgentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PolkitAgentListenerClass *listener_class;

  object_class->constructed = phosh_polkit_auth_agent_constructed;
  object_class->dispose = phosh_polkit_auth_agent_dispose;

  listener_class = POLKIT_AGENT_LISTENER_CLASS (klass);
  listener_class->initiate_authentication = initiate_authentication;
  listener_class->initiate_authentication_finish = initiate_authentication_finish;
}


static void
phosh_polkit_auth_agent_init (PhoshPolkitAuthAgent *self)
{
}


PhoshPolkitAuthAgent *
phosh_polkit_auth_agent_new (void)
{
  return g_object_new (PHOSH_TYPE_POLKIT_AUTH_AGENT, NULL);
}
