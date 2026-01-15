/*
 * Copyright (C) 2018 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */
#pragma once

#include "dbus/phosh-display-dbus.h"
#include "monitor/monitor.h"

#include "sensor-proxy-manager.h"

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PhoshMonitorManagerConfigMethod:
 * @PHOSH_MONITOR_MANAGER_CONFIG_METHOD_VERIFY: verify the configuration
 * @PHOSH_MONITOR_MANAGER_CONFIG_METHOD_TEMPORARY: configuration is temporary
 * @PHOSH_MONITOR_MANAGER_CONFIG_METHOD_PERSISTENT: configuration is permanent
 *
 * Equivalent to the 'method' enum in org.gnome.Mutter.DisplayConfig
 */
typedef enum _MetaMonitorsConfigMethod
{
  PHOSH_MONITOR_MANAGER_CONFIG_METHOD_VERIFY = 0,
  PHOSH_MONITOR_MANAGER_CONFIG_METHOD_TEMPORARY = 1,
  PHOSH_MONITOR_MANAGER_CONFIG_METHOD_PERSISTENT = 2,
} PhoshMonitorManagerConfigMethod;

#define PHOSH_TYPE_MONITOR_MANAGER                 (phosh_monitor_manager_get_type ())
G_DECLARE_FINAL_TYPE (PhoshMonitorManager, phosh_monitor_manager, PHOSH, MONITOR_MANAGER,
                      PhoshDBusDisplayConfigSkeleton)

PhoshMonitorManager * phosh_monitor_manager_new                       (PhoshSensorProxyManager *proxy);
PhoshMonitor        * phosh_monitor_manager_get_monitor               (PhoshMonitorManager *self,
                                                                       guint                num);
guint                 phosh_monitor_manager_get_num_monitors          (PhoshMonitorManager *self);
PhoshMonitor        * phosh_monitor_manager_find_monitor              (PhoshMonitorManager *self,
                                                                       const char          *name);
void                  phosh_monitor_manager_set_monitor_transform     (PhoshMonitorManager *self,
                                                                       PhoshMonitor        *monitor,
                                                                       PhoshMonitorTransform transform);
void                  phosh_monitor_manager_set_monitor_scale         (PhoshMonitorManager *self,
                                                                       PhoshMonitor        *monitor,
                                                                       double               scale);
void                  phosh_monitor_manager_apply_monitor_config      (PhoshMonitorManager *self);
void                  phosh_monitor_manager_set_sensor_proxy_manager  (PhoshMonitorManager     *self,
                                                                       PhoshSensorProxyManager *manager);
gboolean              phosh_monitor_manager_enable_fallback           (PhoshMonitorManager *self);
void                  phosh_monitor_manager_set_power_save_mode       (PhoshMonitorManager *self,
                                                                       PhoshMonitorPowerSaveMode mode);
gboolean              phosh_monitor_manager_get_night_light_supported (PhoshMonitorManager *self);
guint32               phosh_monitor_manager_get_night_light_temp      (PhoshMonitorManager *self);

G_END_DECLS
