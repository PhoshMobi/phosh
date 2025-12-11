/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gtk/gtk.h>
#include "layersurface.h"

#include <gio/gdesktopappinfo.h>

#define PHOSH_TYPE_SPLASH (phosh_splash_get_type ())

G_DECLARE_DERIVABLE_TYPE (PhoshSplash, phosh_splash, PHOSH, SPLASH, PhoshLayerSurface)

/**
 * PhoshSplashClass
 * @parent_class: The parent class
 */
struct _PhoshSplashClass {
  PhoshLayerSurfaceClass parent_class;
};


GtkWidget *phosh_splash_new (GDesktopAppInfo *info, gboolean prefer_dark);
void phosh_splash_hide (PhoshSplash *self);
void phosh_splash_lower (PhoshSplash *self);
void phosh_splash_raise (PhoshSplash *self);
