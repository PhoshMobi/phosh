/*
 * Copyright © 2019 Zander Brown <zbrown@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#pragma once

G_BEGIN_DECLS

#define PHOSH_TYPE_APP_LIST_MODEL phosh_app_list_model_get_type()
G_DECLARE_DERIVABLE_TYPE (PhoshAppListModel, phosh_app_list_model, PHOSH, APP_LIST_MODEL, GObject)

struct _PhoshAppListModelClass
{
  GObjectClass parent_class;
};

PhoshAppListModel *phosh_app_list_model_get_default (void);
GDesktopAppInfo *  phosh_app_list_model_lookup_by_startup_wm_class (PhoshAppListModel *self,
                                                                    const char        *class);
GDesktopAppInfo *  phosh_app_list_model_lookup_by_exec (PhoshAppListModel *self, const char *exec);
void               phosh_app_list_model_add_exec (PhoshAppListModel *self,
                                                  const char        *exec,
                                                  GAppInfo          *info);

G_END_DECLS
