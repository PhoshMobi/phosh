/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include "ext-idle-notify-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "phoc-device-state-unstable-v1-client-protocol.h"
#include "phoc-layer-shell-effects-unstable-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-gamma-control-unstable-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"
#include "wlr-output-power-management-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"


/* This goes past the other wl protocols since it might need their structs */
#include "phosh-private-client-protocol.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PhoshWaylandSeatCapabilities:
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_NONE: no device detected
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_POINTER: the seat has pointer devices
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_KEYBOARD: the seat has one or more keyboards
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_TOUCH: the seat has touch devices
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_TABLET_MODE_SWITCH: the seat has a tablet mode switch
 * @PHOSH_WAYLAND_SEAT_CAPABILITY_LID_SWITCH: the seat has a lid switch
 *
 * These match wl_seat_capabilities
 */
typedef enum {
  /* From wl_seat */
  PHOSH_WAYLAND_SEAT_CAPABILITY_NONE     = 0,
  PHOSH_WAYLAND_SEAT_CAPABILITY_POINTER  = (1 << 0),
  PHOSH_WAYLAND_SEAT_CAPABILITY_KEYBOARD = (1 << 1),
  PHOSH_WAYLAND_SEAT_CAPABILITY_TOUCH    = (1 << 2),
  /* From device_state */
  PHOSH_WAYLAND_SEAT_CAPABILITY_TABLET_MODE_SWITCH = (1 << 8),
  PHOSH_WAYLAND_SEAT_CAPABILITY_LID_SWITCH = (1 << 9),
} PhoshWaylandSeatCapabilities;

#define PHOSH_TYPE_WAYLAND phosh_wayland_get_type()

G_DECLARE_FINAL_TYPE (PhoshWayland, phosh_wayland, PHOSH, WAYLAND, GObject)

PhoshWayland                         *phosh_wayland_get_default (void);
GHashTable                           *phosh_wayland_get_wl_outputs (PhoshWayland *self);
gboolean                              phosh_wayland_has_wl_output  (PhoshWayland *self,
                                                                    struct wl_output *wl_output);
struct ext_idle_notifier_v1          *phosh_wayland_get_ext_idle_notifier_v1 (PhoshWayland *self);
struct phosh_private                 *phosh_wayland_get_phosh_private (PhoshWayland *self);
uint32_t                              phosh_wayland_get_phosh_private_version (PhoshWayland *self);
struct wl_seat                       *phosh_wayland_get_wl_seat (PhoshWayland *self);
struct wl_shm                        *phosh_wayland_get_wl_shm (PhoshWayland *self);
struct xdg_wm_base                   *phosh_wayland_get_xdg_wm_base (PhoshWayland *self);
struct zwlr_foreign_toplevel_manager_v1 *phosh_wayland_get_zwlr_foreign_toplevel_manager_v1 (PhoshWayland *self);
struct zwlr_layer_shell_v1           *phosh_wayland_get_zwlr_layer_shell_v1 (PhoshWayland *self);
struct zwlr_gamma_control_manager_v1 *phosh_wayland_get_zwlr_gamma_control_manager_v1 (PhoshWayland *self);
struct zwlr_output_manager_v1        *phosh_wayland_get_zwlr_output_manager_v1 (PhoshWayland *self);
struct zwlr_output_power_manager_v1 *phosh_wayland_get_zwlr_output_power_manager_v1 (PhoshWayland *self);
struct zxdg_output_manager_v1        *phosh_wayland_get_zxdg_output_manager_v1 (PhoshWayland *self);
struct zwlr_screencopy_manager_v1    *phosh_wayland_get_zwlr_screencopy_manager_v1 (PhoshWayland *self);
struct zwp_virtual_keyboard_manager_v1 *phosh_wayland_get_zwp_virtual_keyboard_manager_v1 (PhoshWayland *self);
void                                  phosh_wayland_roundtrip (PhoshWayland *self);
PhoshWaylandSeatCapabilities          phosh_wayland_get_seat_capabilities (PhoshWayland *self);
struct zphoc_layer_shell_effects_v1  *phosh_wayland_get_zphoc_layer_shell_effects_v1 (PhoshWayland *self);
uint32_t                              phosh_wayland_get_layer_shell_effects_version (PhoshWayland *self);
struct zphoc_device_state_v1         *phosh_wayland_get_zphoc_device_state_v1 (PhoshWayland *self);
G_END_DECLS
