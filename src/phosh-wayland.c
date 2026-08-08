/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#define G_LOG_DOMAIN "phosh-wayland"

#include "phosh-config.h"
#include "phosh-enums.h"
#include "phosh-wayland.h"

#include <gdk/gdkwayland.h>

/**
 * PhoshWayland:
 *
 * A wayland registry listener
 *
 * The #PhoshWayland singleton is responsible for listening to wayland
 * registry events registering the objects that show up there to make
 * them available to Phosh's other classes.
 */

enum {
  PHOSH_WAYLAND_PROP_0,
  PHOSH_WAYLAND_PROP_WL_OUTPUTS,
  PHOSH_WAYLAND_PROP_SEAT_CAPABILITIES,
  PHOSH_WAYLAND_PROP_LAST_PROP,
};
static GParamSpec *props[PHOSH_WAYLAND_PROP_LAST_PROP];


#define WL_SEAT_CAPS(caps)      ((caps) & 0x00FF)
#define DEVICE_STATE_CAPS(caps) ((caps) & 0xFF00)
#define DEVICE_STATE_SHIFT      8

struct _PhoshWayland {
  GObject                                  parent;

  struct ext_idle_notifier_v1             *ext_idle_notifier_v1;
  struct phosh_private                    *phosh_private;
  uint32_t                                 phosh_private_version;
  struct zwp_virtual_keyboard_manager_v1  *zwp_virtual_keyboard_manager_v1;
  struct wl_display                       *display;
  struct wl_registry                      *registry;
  struct wl_seat                          *wl_seat;
  struct xdg_wm_base                      *xdg_wm_base;
  struct zwlr_foreign_toplevel_manager_v1 *zwlr_foreign_toplevel_manager_v1;
  struct zwlr_gamma_control_manager_v1    *zwlr_gamma_control_manager_v1;
  struct zwlr_layer_shell_v1              *layer_shell;
  struct zwlr_output_manager_v1           *zwlr_output_manager_v1;
  struct zwlr_output_power_manager_v1     *zwlr_output_power_manager_v1;
  struct zxdg_output_manager_v1           *zxdg_output_manager_v1;
  struct zwlr_screencopy_manager_v1       *zwlr_screencopy_manager_v1;
  struct zphoc_layer_shell_effects_v1     *zphoc_layer_shell_effects_v1;
  struct zphoc_device_state_v1            *zphoc_device_state_v1;
  struct wl_shm                           *wl_shm;
  GHashTable                              *wl_outputs;
  PhoshWaylandSeatCapabilities             seat_capabilities;
};

G_DEFINE_TYPE (PhoshWayland, phosh_wayland, G_TYPE_OBJECT)


static void
registry_handle_global (void               *data,
                        struct wl_registry *registry,
                        uint32_t            name,
                        const char         *interface,
                        uint32_t            version)
{
  PhoshWayland *self = data;
  struct wl_output *output;

  if (!strcmp (interface, "phosh_private")) {
    self->phosh_private = wl_registry_bind (registry,
                                            name,
                                            &phosh_private_interface,
                                            7);
    self->phosh_private_version = version;
  } else if (!strcmp (interface, zphoc_layer_shell_effects_v1_interface.name)) {
    self->zphoc_layer_shell_effects_v1 = wl_registry_bind (registry,
                                                           name,
                                                           &zphoc_layer_shell_effects_v1_interface,
                                                           MIN (3, version));
  } else if (!strcmp (interface, zphoc_device_state_v1_interface.name)) {
    self->zphoc_device_state_v1 = wl_registry_bind (registry,
                                                    name,
                                                    &zphoc_device_state_v1_interface,
                                                    1);
  } else if (!strcmp (interface, zwlr_layer_shell_v1_interface.name)) {
    self->layer_shell = wl_registry_bind (registry,
                                          name,
                                          &zwlr_layer_shell_v1_interface,
                                          2);
  } else if (!strcmp (interface, "wl_output")) {
    output = wl_registry_bind (registry, name, &wl_output_interface, 2);
    g_debug ("Got new output %p", output);
    g_hash_table_insert (self->wl_outputs, GINT_TO_POINTER (name), output);
    g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_WAYLAND_PROP_WL_OUTPUTS]);
  } else if (!strcmp (interface, "wl_seat")) {
    self->wl_seat = wl_registry_bind (registry, name, &wl_seat_interface, 1);
  } else if (!strcmp (interface, "wl_shm")) {
    self->wl_shm = wl_registry_bind (registry, name, &wl_shm_interface, 1);
  } else if (!strcmp (interface, xdg_wm_base_interface.name)) {
    self->xdg_wm_base = wl_registry_bind (registry, name, &xdg_wm_base_interface, 1);
  } else if (!strcmp (interface, zwlr_gamma_control_manager_v1_interface.name)) {
    self->zwlr_gamma_control_manager_v1 = wl_registry_bind (registry,
                                                            name,
                                                            &zwlr_gamma_control_manager_v1_interface,
                                                            1);
  } else if (!strcmp (interface, zxdg_output_manager_v1_interface.name)) {
    self->zxdg_output_manager_v1 = wl_registry_bind (registry,
                                                     name,
                                                     &zxdg_output_manager_v1_interface,
                                                     3);
  } else if (!strcmp (interface, zwlr_output_manager_v1_interface.name)) {
    self->zwlr_output_manager_v1 = wl_registry_bind (registry,
                                                     name,
                                                     &zwlr_output_manager_v1_interface,
                                                     MIN (2, version));
  } else if (!strcmp (interface, zwlr_output_power_manager_v1_interface.name)) {
    self->zwlr_output_power_manager_v1 = wl_registry_bind (registry,
                                                           name,
                                                           &zwlr_output_power_manager_v1_interface,
                                                           1);
  } else if (!strcmp (interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
    self->zwlr_foreign_toplevel_manager_v1 =
      wl_registry_bind (registry,
                        name,
                        &zwlr_foreign_toplevel_manager_v1_interface,
                        3);
  } else if (!strcmp (interface, zwlr_screencopy_manager_v1_interface.name)) {
    self->zwlr_screencopy_manager_v1 = wl_registry_bind (registry,
                                                         name,
                                                         &zwlr_screencopy_manager_v1_interface,
                                                         2);
  } else if (!strcmp (interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
    self->zwp_virtual_keyboard_manager_v1 =
      wl_registry_bind (registry,
                        name,
                        &zwp_virtual_keyboard_manager_v1_interface,
                        1);
  } else if (!strcmp (interface, ext_idle_notifier_v1_interface.name)) {
    self->ext_idle_notifier_v1 = wl_registry_bind (registry,
                                                   name,
                                                   &ext_idle_notifier_v1_interface,
                                                   1);
  }
}


static void
registry_handle_global_remove (void               *data,
                               struct wl_registry *registry,
                               uint32_t            name)
{
  PhoshWayland *self = data;
  struct wl_output *wl_output;

  wl_output = g_hash_table_lookup (self->wl_outputs, GINT_TO_POINTER (name));
  if (wl_output) {
    g_debug ("Output %d removed", name);
    g_hash_table_remove (self->wl_outputs, GINT_TO_POINTER (name));
    g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_WAYLAND_PROP_WL_OUTPUTS]);
  } else {
    g_warning ("Global %d removed but not handled", name);
  }
}


static const struct wl_registry_listener registry_listener = {
  registry_handle_global,
  registry_handle_global_remove
};


static void
phosh_wayland_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  PhoshWayland *self = PHOSH_WAYLAND (object);

  switch (property_id) {
  case PHOSH_WAYLAND_PROP_WL_OUTPUTS:
    g_value_set_boxed (value, self->wl_outputs);
    break;
  case PHOSH_WAYLAND_PROP_SEAT_CAPABILITIES:
    g_value_set_flags (value, self->seat_capabilities);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
seat_handle_capabilities (void *data, struct wl_seat *wl_seat, uint32_t capabilities)
{
  PhoshWayland *self = PHOSH_WAYLAND (data);

  if (WL_SEAT_CAPS (self->seat_capabilities) != capabilities) {
    g_debug ("Seat capabilities: 0x%x", capabilities);
    self->seat_capabilities = DEVICE_STATE_CAPS (self->seat_capabilities) | capabilities;
    g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_WAYLAND_PROP_SEAT_CAPABILITIES]);
  }
}


static void
seat_handle_name (void *data, struct wl_seat *wl_seat, const char *name)
{
  /* nothing to do */
}


static const struct wl_seat_listener seat_listener = {
  seat_handle_capabilities,
  seat_handle_name,
};


static void
device_state_handle_capabilities (void                         *data,
                                  struct zphoc_device_state_v1 *zphoc_device_state_v1,
                                  uint32_t                      capabilities)
{
  PhoshWayland *self = PHOSH_WAYLAND (data);

  g_debug ("Device state capabilities: 0x%x", capabilities);

  g_return_if_fail (PHOSH_IS_WAYLAND (self));

  if ((DEVICE_STATE_CAPS (self->seat_capabilities) >> DEVICE_STATE_SHIFT) != capabilities) {
    self->seat_capabilities =
      WL_SEAT_CAPS (self->seat_capabilities) | (capabilities << DEVICE_STATE_SHIFT);
    g_object_notify_by_pspec (G_OBJECT (self), props[PHOSH_WAYLAND_PROP_SEAT_CAPABILITIES]);
  }
}


static const struct zphoc_device_state_v1_listener device_state_listener = {
  .capabilities = device_state_handle_capabilities,
};


static void
phosh_wayland_constructed (GObject *object)
{
  PhoshWayland *self = PHOSH_WAYLAND (object);
  guint num_outputs;
  GdkDisplay *gdk_display;

  G_OBJECT_CLASS (phosh_wayland_parent_class)->constructed (object);

  gdk_set_allowed_backends ("wayland");
  gdk_display = gdk_display_get_default ();
  self->display = gdk_wayland_display_get_wl_display (gdk_display);

  if (self->display == NULL)
    g_error ("Failed to get display: %m\n");

  self->registry = wl_display_get_registry (self->display);
  wl_registry_add_listener (self->registry, &registry_listener, self);

  /* Wait until we have been notified about the wayland globals we require */
  phosh_wayland_roundtrip (self);
  num_outputs = g_hash_table_size (self->wl_outputs);
  if (!num_outputs || !self->layer_shell || !self->ext_idle_notifier_v1 ||
      !self->xdg_wm_base ||
      !self->zxdg_output_manager_v1 ||
      !self->zwlr_output_power_manager_v1 ||
      !self->zphoc_layer_shell_effects_v1) {
    g_error ("Wayland compositor lacks needed globals\n"
             "outputs: %d, layer_shell: %p, idle_manager: %p, "
             "xdg_wm: %p, "
             "xdg_output: %p, wlr_output_manager: %p, "
             "wlr_foreign_toplevel_manager: %p, "
             "zwlr_output_power_manager_v1: %p, "
             "zphoc_layer_shell_effects_v1: %p"
             "\n",
             num_outputs, self->layer_shell, self->ext_idle_notifier_v1,
             self->xdg_wm_base,
             self->zxdg_output_manager_v1,
             self->zwlr_output_manager_v1,
             self->zwlr_foreign_toplevel_manager_v1,
             self->zwlr_output_power_manager_v1,
             self->zphoc_layer_shell_effects_v1);
  }
  if (!self->phosh_private)
    g_info ("Could not find phosh private interface, disabling some features");

  wl_seat_add_listener (self->wl_seat, &seat_listener, self);

  if (self->zphoc_device_state_v1)
    zphoc_device_state_v1_add_listener (self->zphoc_device_state_v1, &device_state_listener, self);
  else
    g_info ("Phoc doesn't support zphoc_device_state_v1 - please upgrade");
}


static void
phosh_wayland_dispose (GObject *object)
{
  PhoshWayland *self = PHOSH_WAYLAND (object);

  g_clear_pointer (&self->ext_idle_notifier_v1, ext_idle_notifier_v1_destroy);
  g_clear_pointer (&self->layer_shell, &zwlr_layer_shell_v1_destroy);
  g_clear_pointer (&self->phosh_private, phosh_private_destroy);
  g_clear_pointer (&self->registry, wl_registry_destroy);
  g_clear_pointer (&self->wl_seat, wl_seat_destroy);
  g_clear_pointer (&self->wl_shm, wl_shm_destroy);
  g_clear_pointer (&self->xdg_wm_base, xdg_wm_base_destroy);
  g_clear_pointer (&self->zwlr_foreign_toplevel_manager_v1,
                   zwlr_foreign_toplevel_manager_v1_destroy);
  g_clear_pointer (&self->zwlr_gamma_control_manager_v1, zwlr_gamma_control_manager_v1_destroy);
  g_clear_pointer (&self->zwlr_output_manager_v1, zwlr_output_manager_v1_destroy);
  g_clear_pointer (&self->zwlr_output_power_manager_v1, zwlr_output_power_manager_v1_destroy);
  g_clear_pointer (&self->zwlr_screencopy_manager_v1, zwlr_screencopy_manager_v1_destroy);
  g_clear_pointer (&self->zwp_virtual_keyboard_manager_v1, zwp_virtual_keyboard_manager_v1_destroy);
  g_clear_pointer (&self->zxdg_output_manager_v1, zxdg_output_manager_v1_destroy);
  g_clear_pointer (&self->zphoc_layer_shell_effects_v1, zphoc_layer_shell_effects_v1_destroy);
  g_clear_pointer (&self->zphoc_device_state_v1, zphoc_device_state_v1_destroy);

  g_clear_pointer (&self->wl_outputs, g_hash_table_destroy);

  G_OBJECT_CLASS (phosh_wayland_parent_class)->dispose (object);
}


static void
phosh_wayland_class_init (PhoshWaylandClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = phosh_wayland_constructed;
  object_class->dispose = phosh_wayland_dispose;

  object_class->get_property = phosh_wayland_get_property;

  props[PHOSH_WAYLAND_PROP_WL_OUTPUTS] =
    g_param_spec_boxed ("wl-outputs",
                        "Wayland Outputs",
                        "The currently known wayland outputs",
                        G_TYPE_HASH_TABLE,
                        G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PHOSH_WAYLAND_PROP_SEAT_CAPABILITIES] =
    g_param_spec_flags ("seat-capabilities",
                        "Seat capabilities",
                        "The current seat capabilities",
                        PHOSH_TYPE_WAYLAND_SEAT_CAPABILITIES,
                        PHOSH_WAYLAND_SEAT_CAPABILITY_NONE,
                        G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PHOSH_WAYLAND_PROP_LAST_PROP, props);
}


static void
output_destroy (gpointer data)
{
  struct wl_output *output = data;

  wl_output_destroy (output);
}


static void
phosh_wayland_init (PhoshWayland *self)
{
  self->wl_outputs = g_hash_table_new_full (g_direct_hash,g_direct_equal, NULL, output_destroy);
}

/**
 * phosh_wayland_get_default:
 *
 * Get the Wayland singleton for handling Wayland protocol
 * interactions
 *
 * Returns:(transfer none): The Wayland singleton
 */
PhoshWayland *
phosh_wayland_get_default (void)
{
  static PhoshWayland *instance;

  if (instance == NULL) {
    instance = g_object_new (PHOSH_TYPE_WAYLAND, NULL);
    g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
  }
  return instance;
}


struct zwlr_layer_shell_v1 *
phosh_wayland_get_zwlr_layer_shell_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->layer_shell;
}


struct zwlr_gamma_control_manager_v1*
phosh_wayland_get_zwlr_gamma_control_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwlr_gamma_control_manager_v1;
}


struct wl_seat*
phosh_wayland_get_wl_seat (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->wl_seat;
}


struct xdg_wm_base*
phosh_wayland_get_xdg_wm_base (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->xdg_wm_base;
}


struct ext_idle_notifier_v1 *
phosh_wayland_get_ext_idle_notifier_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->ext_idle_notifier_v1;
}


struct phosh_private*
phosh_wayland_get_phosh_private (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->phosh_private;
}


uint32_t
phosh_wayland_get_phosh_private_version (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), 0);

  return self->phosh_private_version;
}


struct wl_shm*
phosh_wayland_get_wl_shm (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->wl_shm;
}


struct zxdg_output_manager_v1*
phosh_wayland_get_zxdg_output_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zxdg_output_manager_v1;
}


struct zwlr_output_manager_v1*
phosh_wayland_get_zwlr_output_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwlr_output_manager_v1;
}

struct zwlr_output_power_manager_v1*
phosh_wayland_get_zwlr_output_power_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwlr_output_power_manager_v1;
}

struct zwlr_foreign_toplevel_manager_v1*
phosh_wayland_get_zwlr_foreign_toplevel_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwlr_foreign_toplevel_manager_v1;
}


struct zwlr_screencopy_manager_v1*
phosh_wayland_get_zwlr_screencopy_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwlr_screencopy_manager_v1;
}

struct zwp_virtual_keyboard_manager_v1*
phosh_wayland_get_zwp_virtual_keyboard_manager_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zwp_virtual_keyboard_manager_v1;
}


/**
 * phosh_wayland_get_wl_outputs:
 * @self: The #PhoshWayland singleton
 *
 * Returns: (transfer none): A list of outputs as a #GHashTable
 * keyed by the output's name with wl_output's as values.
 */
GHashTable *
phosh_wayland_get_wl_outputs (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->wl_outputs;
}

gboolean
phosh_wayland_has_wl_output (PhoshWayland *self, struct wl_output *wl_output)
{
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), FALSE);

  g_hash_table_iter_init (&iter, self->wl_outputs);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if ((struct wl_output *) value == wl_output)
      return TRUE;
  }
  return FALSE;
}


void
phosh_wayland_roundtrip (PhoshWayland *self)
{
  g_return_if_fail (PHOSH_IS_WAYLAND (self));

  wl_display_roundtrip (self->display);
}


PhoshWaylandSeatCapabilities
phosh_wayland_get_seat_capabilities (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), PHOSH_WAYLAND_SEAT_CAPABILITY_NONE);

  return self->seat_capabilities;
}


struct zphoc_layer_shell_effects_v1 *
phosh_wayland_get_zphoc_layer_shell_effects_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zphoc_layer_shell_effects_v1;
}


struct zphoc_device_state_v1 *
phosh_wayland_get_zphoc_device_state_v1 (PhoshWayland *self)
{
  g_return_val_if_fail (PHOSH_IS_WAYLAND (self), NULL);

  return self->zphoc_device_state_v1;
}
