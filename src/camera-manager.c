/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Heavily inspired by gnome-shell's camera-monitor which is
 * Copyright (C) 2023 Collabora Ltd.
 */

#define G_LOG_DOMAIN "phosh-camera-manager"

#include "phosh-config.h"

#include "camera-manager.h"

#include <pipewire/pipewire.h>

#include <errno.h>

#define RECONNECT_DELAY_INITIAL_MS 250
#define RECONNECT_TIMEOUT_MS 10000
/* Avoid flicker when an app re-links its stream */
#define DISABLE_DELAY_MS 500

/**
 * PhoshCameraManager:
 *
 * Tracks whether a camera is in use by watching the PipeWire registry.
 */

enum {
  PROP_0,
  PROP_IN_USE,
  PROP_LAST_PROP
};
static GParamSpec *props[PROP_LAST_PROP];

typedef struct {
  GSource source;
  struct pw_loop *loop;
} PwSource;

typedef struct {
  PhoshCameraManager *manager;
  struct pw_node     *node;
  struct spa_hook     listener;
  gboolean running;
} CameraNode;

struct _PhoshCameraManager {
  GObject             parent;

  PwSource           *pw_source;
  struct pw_context  *context;

  struct pw_core     *core;
  struct spa_hook     core_listener;
  struct pw_registry *registry;
  struct spa_hook     registry_listener;
  GHashTable         *nodes;       /* key: id, value: CameraNode */
  guint    reconnect_id;
  guint    reconnect_delay_ms;
  guint    reconnect_elapsed_ms;
  guint    disable_id;

  gboolean in_use;
};

G_DEFINE_TYPE (PhoshCameraManager, phosh_camera_manager, G_TYPE_OBJECT)


static void
set_in_use (PhoshCameraManager *self, gboolean in_use)
{
  if (self->in_use == in_use)
    return;

  g_debug ("Camera in use: %d", in_use);
  self->in_use = in_use;
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_IN_USE]);
}


static void
on_disable (gpointer data)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (data);

  self->disable_id = 0;
  set_in_use (self, FALSE);
}


static void
update_in_use (PhoshCameraManager *self)
{
  GHashTableIter iter;
  CameraNode *node;
  gboolean in_use = FALSE;

  g_hash_table_iter_init (&iter, self->nodes);
  while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&node)) {
    if (node->running) {
      in_use = TRUE;
      break;
    }
  }

  if (in_use) {
    g_clear_handle_id (&self->disable_id, g_source_remove);
    set_in_use (self, TRUE);
  } else if (self->in_use && self->disable_id == 0) {
    self->disable_id = g_timeout_add_once (DISABLE_DELAY_MS, on_disable, self);
  }
}


static void
on_node_info (void *data, const struct pw_node_info *info)
{
  CameraNode *node = data;

  if (!(info->change_mask & PW_NODE_CHANGE_MASK_STATE))
    return;

  node->running = (info->state == PW_NODE_STATE_RUNNING);
  update_in_use (node->manager);
}


static const struct pw_node_events node_events = {
  PW_VERSION_NODE_EVENTS,
  .info = on_node_info,
};


static void
camera_node_free (CameraNode *node)
{
  spa_hook_remove (&node->listener);
  pw_proxy_destroy ((struct pw_proxy *)node->node);
  g_free (node);
}


static void
reset_reconnect_backoff (PhoshCameraManager *self)
{
  self->reconnect_delay_ms = RECONNECT_DELAY_INITIAL_MS;
  self->reconnect_elapsed_ms = 0;
}


static void
on_registry_global (void                  *data,
                    uint32_t               id,
                    uint32_t               permissions,
                    const char            *type,
                    uint32_t               version,
                    const struct spa_dict *pw_props)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (data);
  CameraNode *node;
  const char *media_class, *media_role;

  reset_reconnect_backoff (self);

  if (g_strcmp0 (type, PW_TYPE_INTERFACE_Node) != 0 || pw_props == NULL)
    return;

  media_class = spa_dict_lookup (pw_props, PW_KEY_MEDIA_CLASS);
  media_role = spa_dict_lookup (pw_props, PW_KEY_MEDIA_ROLE);
  /* See camera portal */
  if (g_strcmp0 (media_class, "Video/Source"))
    return;

  if (g_strcmp0 (media_role, "Camera"))
    return;

  g_debug ("Found camera node %u", id);
  node = g_new0 (CameraNode, 1);
  node->manager = self;
  node->node = pw_registry_bind (self->registry, id, type, PW_VERSION_NODE, 0);
  if (node->node == NULL) {
    g_warning ("Failed to bind camera node %u", id);
    g_free (node);
    return;
  }

  pw_node_add_listener (node->node, &node->listener, &node_events, node);
  g_hash_table_insert (self->nodes, GUINT_TO_POINTER (id), node);
}


static void
on_registry_global_remove (void *data, uint32_t id)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (data);

  if (g_hash_table_remove (self->nodes, GUINT_TO_POINTER (id))) {
    g_debug ("Camera node %u gone", id);
    update_in_use (self);
  }
}


static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = on_registry_global,
  .global_remove = on_registry_global_remove,
};


static void on_reconnect (gpointer data);


static void
schedule_reconnect (PhoshCameraManager *self)
{
  guint delay_ms;

  if (self->reconnect_id)
    return;

  if (self->reconnect_elapsed_ms >= RECONNECT_TIMEOUT_MS) {
    g_message ("Giving up connecting to PipeWire, not tracking cameras");
    return;
  }

  delay_ms = MIN (self->reconnect_delay_ms, RECONNECT_TIMEOUT_MS - self->reconnect_elapsed_ms);
  g_debug ("Reconnecting to PipeWire in %ums", delay_ms);
  self->reconnect_elapsed_ms += delay_ms;
  self->reconnect_delay_ms *= 2;
  self->reconnect_id = g_timeout_add_once (delay_ms, on_reconnect, self);
}


static void
disconnect_pipewire (PhoshCameraManager *self)
{
  g_hash_table_remove_all (self->nodes);
  g_clear_handle_id (&self->disable_id, g_source_remove);

  if (self->registry) {
    spa_hook_remove (&self->registry_listener);
    pw_proxy_destroy ((struct pw_proxy *)self->registry);
    self->registry = NULL;
  }

  if (self->core) {
    spa_hook_remove (&self->core_listener);
    pw_core_disconnect (self->core);
    self->core = NULL;
  }

  /* Without PipeWire we can't know, don't wait */
  set_in_use (self, FALSE);
}


static void
on_core_error (void *data, uint32_t id, int seq, int res, const char *message)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (data);

  g_debug ("PipeWire error on id %u: %s (%s)", id, message, g_strerror (-res));

  /* The server went away */
  if (id == PW_ID_CORE && res == -EPIPE) {
    disconnect_pipewire (self);
    schedule_reconnect (self);
  }
}


static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .error = on_core_error,
};


static gboolean
connect_pipewire (PhoshCameraManager *self)
{
  self->core = pw_context_connect (self->context, NULL, 0);
  if (self->core == NULL) {
    g_debug ("Failed to connect to PipeWire: %s", g_strerror (errno));
    return FALSE;
  }
  pw_core_add_listener (self->core, &self->core_listener, &core_events, self);

  self->registry = pw_core_get_registry (self->core, PW_VERSION_REGISTRY, 0);
  pw_registry_add_listener (self->registry, &self->registry_listener, &registry_events, self);

  g_debug ("Connected to PipeWire");
  return TRUE;
}


static void
on_reconnect (gpointer data)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (data);

  self->reconnect_id = 0;

  if (!connect_pipewire (self))
    schedule_reconnect (self);
}


static gboolean
pw_source_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
  PwSource *pw_source = (PwSource *)source;
  int res;

  res = pw_loop_iterate (pw_source->loop, 0);
  if (res < 0 && res != -EINTR)
    g_warning ("PipeWire loop iteration failed: %s", g_strerror (-res));

  return G_SOURCE_CONTINUE;
}


static GSourceFuncs pw_source_funcs = {
  .dispatch = pw_source_dispatch,
};


static PwSource *
pw_source_new (struct pw_loop *loop)
{
  PwSource *pw_source;

  pw_source = (PwSource *)g_source_new (&pw_source_funcs, sizeof (PwSource));
  pw_source->loop = loop;
  g_source_set_name (&pw_source->source, "[phosh] PipeWire");
  pw_loop_enter (loop);
  g_source_add_unix_fd (&pw_source->source,
                        pw_loop_get_fd (loop),
                        G_IO_IN | G_IO_ERR | G_IO_HUP);
  g_source_attach (&pw_source->source, NULL);

  return pw_source;
}


static void
pw_source_destroy (PwSource *pw_source)
{
  struct pw_loop *loop = pw_source->loop;

  g_source_destroy (&pw_source->source);
  g_source_unref (&pw_source->source);
  pw_loop_leave (loop);
  pw_loop_destroy (loop);
}


static void
phosh_camera_manager_get_property (GObject    *object,
                                   guint       property_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (object);

  switch (property_id) {
  case PROP_IN_USE:
    g_value_set_boolean (value, self->in_use);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_camera_manager_dispose (GObject *object)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (object);

  g_clear_handle_id (&self->reconnect_id, g_source_remove);
  disconnect_pipewire (self);

  g_clear_pointer (&self->context, pw_context_destroy);
  if (self->pw_source) {
    pw_source_destroy (self->pw_source);
    self->pw_source = NULL;
  }

  G_OBJECT_CLASS (phosh_camera_manager_parent_class)->dispose (object);
}


static void
phosh_camera_manager_finalize (GObject *object)
{
  PhoshCameraManager *self = PHOSH_CAMERA_MANAGER (object);

  g_clear_pointer (&self->nodes, g_hash_table_unref);
  pw_deinit ();

  G_OBJECT_CLASS (phosh_camera_manager_parent_class)->finalize (object);
}


static void
phosh_camera_manager_class_init (PhoshCameraManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = phosh_camera_manager_get_property;
  object_class->dispose = phosh_camera_manager_dispose;
  object_class->finalize = phosh_camera_manager_finalize;

  /**
   * PhoshCameraManager:in-use:
   *
   * Whether any camera is currently in use.
   */
  props[PROP_IN_USE] =
    g_param_spec_boolean ("in-use", "", "",
                          FALSE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (object_class, PROP_LAST_PROP, props);
}


static void
phosh_camera_manager_init (PhoshCameraManager *self)
{
  struct pw_loop *loop;

  pw_init (NULL, NULL);

  self->nodes = g_hash_table_new_full (g_direct_hash,
                                       g_direct_equal,
                                       NULL,
                                       (GDestroyNotify)camera_node_free);

  loop = pw_loop_new (NULL);
  if (loop == NULL) {
    g_warning ("Failed to create PipeWire loop: %s", g_strerror (errno));
    return;
  }
  self->pw_source = pw_source_new (loop);

  self->context = pw_context_new (loop, NULL, 0);
  if (self->context == NULL) {
    g_warning ("Failed to create PipeWire context: %s", g_strerror (errno));
    return;
  }

  /* PipeWire might not be up yet */
  reset_reconnect_backoff (self);
  if (!connect_pipewire (self))
    schedule_reconnect (self);
}


PhoshCameraManager *
phosh_camera_manager_new (void)
{
  return g_object_new (PHOSH_TYPE_CAMERA_MANAGER, NULL);
}

/**
 * phosh_camera_manager_get_in_use:
 * @self: The camera manager
 *
 * Whether any camera is currently in use.
 *
 * Returns: %TRUE if a camera is in use
 */
gboolean
phosh_camera_manager_get_in_use (PhoshCameraManager *self)
{
  g_return_val_if_fail (PHOSH_IS_CAMERA_MANAGER (self), FALSE);

  return self->in_use;
}
