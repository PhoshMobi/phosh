/*
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "dbus/phosh-screenshot-dbus.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_SCREENSHOT_MANAGER     phosh_screenshot_manager_get_type ()

G_DECLARE_FINAL_TYPE (PhoshScreenshotManager, phosh_screenshot_manager,
                      PHOSH, SCREENSHOT_MANAGER, PhoshDBusScreenshotSkeleton)

PhoshScreenshotManager *phosh_screenshot_manager_new (void);
gboolean                phosh_screenshot_manager_take_screenshot (PhoshScreenshotManager *self,
                                                                  const GdkRectangle     *area,
                                                                  const char             *filename,
                                                                  gboolean                copy_to_clipboard,
                                                                  gboolean                include_cursor);

GdkRectangle *phosh_screenshot_manager_select_area_finish (PhoshScreenshotManager *self,
                                                           GAsyncResult           *res,
                                                           GError                **error);
void          phosh_screenshot_manager_select_area_async (PhoshScreenshotManager *self,
                                                          GCancellable           *cancel,
                                                          GAsyncReadyCallback     callback,
                                                          gpointer                user_data);

G_END_DECLS
