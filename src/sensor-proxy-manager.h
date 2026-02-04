/*
 * Copyright (C) 2020 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "dbus/iio-sensor-proxy-dbus.h"

#define PHOSH_TYPE_SENSOR_PROXY_MANAGER     (phosh_sensor_proxy_manager_get_type ())

G_DECLARE_FINAL_TYPE (PhoshSensorProxyManager, phosh_sensor_proxy_manager,
                      PHOSH, SENSOR_PROXY_MANAGER, PhoshDBusSensorProxyProxy)

PhoshSensorProxyManager *phosh_sensor_proxy_manager_new (GError **err);
