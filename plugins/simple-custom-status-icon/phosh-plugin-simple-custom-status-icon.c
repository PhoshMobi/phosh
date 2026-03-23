/*
 * Copyright (C) 2025 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Arun Mani J <arun.mani@tether.to>
 */

#include "phosh-plugin.h"
#include "simple-custom-status-icon.h"

char **g_io_phosh_plugin_simple_custom_status_icon_query (void);


void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET,
                                  PHOSH_TYPE_SIMPLE_CUSTOM_STATUS_ICON,
                                  PLUGIN_NAME,
                                  10);
}


void
g_io_module_unload (GIOModule *module)
{
}


char **
g_io_phosh_plugin_simple_custom_status_icon_query (void)
{
  char *extension_points[] = {PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET, NULL};

  return g_strdupv (extension_points);
}
