/*
 * Copyright (C) 2026 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: johnwa <johnwa@loopgain.net>
 */

#include "phosh-plugin.h"
#include "load-meter-status-icon.h"

char **g_io_phosh_plugin_load_meter_status_icon_query (void);


void
g_io_module_load (GIOModule *module)
{
  g_type_module_use (G_TYPE_MODULE (module));

  g_io_extension_point_implement (PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET,
                                  PHOSH_TYPE_LOAD_METER_STATUS_ICON,
                                  PLUGIN_NAME,
                                  10);
}


void
g_io_module_unload (GIOModule *module)
{
}


char **
g_io_phosh_plugin_load_meter_status_icon_query (void)
{
  char *extension_points[] = {PHOSH_PLUGIN_EXTENSION_POINT_STATUS_ICON_WIDGET, NULL};

  return g_strdupv (extension_points);
}
