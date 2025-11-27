/*
 * Copyright (C) 2025 Phosh.mobi e.V.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "status-page.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_WIFI_HOTSPOT_STATUS_PAGE phosh_wifi_hotspot_status_page_get_type ()

G_DECLARE_FINAL_TYPE (PhoshWifiHotspotStatusPage, phosh_wifi_hotspot_status_page, PHOSH,
                      WIFI_HOTSPOT_STATUS_PAGE, PhoshStatusPage)

GtkWidget *phosh_wifi_hotspot_status_page_new (void);

G_END_DECLS
