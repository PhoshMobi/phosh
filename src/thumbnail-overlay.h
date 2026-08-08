/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "layersurface.h"
#include "thumbnail.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_thumbnail_overlay (phosh_thumbnail_overlay_get_type ())

G_DECLARE_FINAL_TYPE (PhoshThumbnailOverlay, phosh_thumbnail_overlay, PHOSH, THUMBNAIL_OVERLAY,
                      PhoshLayerSurface)

PhoshThumbnailOverlay *phosh_thumbnail_overlay_new (void);
void                   phosh_thumbnail_overlay_set_thumbnail (PhoshThumbnailOverlay *self,
                                                              PhoshThumbnail        *thumbnail);
void                   phosh_thumbnail_overlay_set_progress (PhoshThumbnailOverlay *self,
                                                              double                 progress);
void                   phosh_thumbnail_overlay_set_thumbnail_y (PhoshThumbnailOverlay *self,
                                                                 double                 y);
void                   phosh_thumbnail_overlay_set_thumbnail_scale (PhoshThumbnailOverlay *self,
                                                                     double                 scale);

G_END_DECLS
