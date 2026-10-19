/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "layersurface.h"
#include "phoc-layer-shell-effects-unstable-v1-client-protocol.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_DRAG_SURFACE (phosh_drag_surface_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhoshDragSurface, phosh_drag_surface, PHOSH, DRAG_SURFACE, PhoshLayerSurface)

/**
 * PhoshDragSurfaceState:
 * @PHOSH_DRAG_SURFACE_STATE_FOLDED: Surface is folded.
 * @PHOSH_DRAG_SURFACE_STATE_UNFOLDED: Surface is unfolded.
 * @PHOSH_DRAG_SURFACE_STATE_DRAGGED: Surface is being dragged.
 * @PHOSH_DRAG_SURFACE_STATE_ANIMATED: Surface is being animated by the compositor.
 *
 * The state of the drag surface.
 */
typedef enum _PhoshDragSurfaceState {
  PHOSH_DRAG_SURFACE_STATE_FOLDED,
  PHOSH_DRAG_SURFACE_STATE_UNFOLDED,
  PHOSH_DRAG_SURFACE_STATE_DRAGGED,
  PHOSH_DRAG_SURFACE_STATE_ANIMATED,
} PhoshDragSurfaceState;

/**
 * PhoshDragSurfaceDragMode:
 * @PHOSH_DRAG_SURFACE_DRAG_MODE_FULL: Full surface is draggable
 * @PHOSH_DRAG_SURFACE_DRAG_MODE_HANDLE: Handle area is draggable.
 * @PHOSH_DRAG_SURFACE_DRAG_MODE_NONE: Surface is not draggable.
 *
 * The drag mode of the drag surface. Specifies how and where
 * the surface is draggable.
 */
typedef enum _PhoshDragSurfaceDragMode {
  PHOSH_DRAG_SURFACE_DRAG_MODE_FULL,
  PHOSH_DRAG_SURFACE_DRAG_MODE_HANDLE,
  PHOSH_DRAG_SURFACE_DRAG_MODE_NONE,
} PhoshDragSurfaceDragMode;

/**
 * PhoshDragSurfaceClass:
 * @parent_class: The parent class
 * @dragged: invoked when a surface is being dragged
 */
struct _PhoshDragSurfaceClass {
  PhoshLayerSurfaceClass parent_class;

  void                   (*dragged) (PhoshDragSurface *self, int margin);
};

void                  phosh_drag_surface_set_margin      (PhoshDragSurface        *self,
                                                          int                      margin_folded,
                                                          int                      margin_unfolded);
double                phosh_drag_surface_get_threshold   (PhoshDragSurface        *self);
void                  phosh_drag_surface_set_threshold   (PhoshDragSurface        *self,
                                                          double                   threshold);
PhoshDragSurfaceState phosh_drag_surface_get_drag_state  (PhoshDragSurface        *self);
void                  phosh_drag_surface_set_drag_state  (PhoshDragSurface        *self,
                                                          PhoshDragSurfaceState    state);
void                  phosh_drag_surface_set_exclusive   (PhoshDragSurface        *self,
                                                          guint                    exclusive);
PhoshDragSurfaceDragMode phosh_drag_surface_get_drag_mode (PhoshDragSurface       *self);
void                  phosh_drag_surface_set_drag_mode   (PhoshDragSurface        *self,
                                                          PhoshDragSurfaceDragMode mode);
guint                 phosh_drag_surface_get_drag_handle (PhoshDragSurface        *self);
void                  phosh_drag_surface_set_drag_handle (PhoshDragSurface        *self,
                                                          guint                    handle);
gboolean              phosh_drag_surface_is_moving       (PhoshDragSurface        *self);

G_END_DECLS
