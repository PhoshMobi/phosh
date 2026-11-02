/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "screenshot-quick-setting.h"
#include "phosh-plugin.h"


char **g_io_phosh_plugin_screenshot_quick_setting_query (void);


void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET,
                                  PHOSH_TYPE_SCREENSHOT_QUICK_SETTING,
                                  PLUGIN_NAME,
                                  10);
}


void
g_io_module_unload (GIOModule *module)
{
}


char **
g_io_phosh_plugin_screenshot_quick_setting_query (void)
{
  char *eps[] = {PHOSH_PLUGIN_EXTENSION_POINT_QUICK_SETTING_WIDGET, NULL};

  return g_strdupv (eps);
}
