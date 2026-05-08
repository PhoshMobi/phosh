/*
 * Copyright (C) 2026 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <handy.h>
#include "syncbus.h"

G_BEGIN_DECLS

#define PHOSH_TYPE_SYNCTHING_FOLDER_ROW phosh_syncthing_folder_row_get_type ()

G_DECLARE_FINAL_TYPE (PhoshSyncthingFolderRow, phosh_syncthing_folder_row, PHOSH,
                      SYNCTHING_FOLDER_ROW, HdyActionRow)

GtkWidget *phosh_syncthing_folder_row_new (SyncbusImplFolder *folder);
void phosh_syncthing_folder_row_set_folder (PhoshSyncthingFolderRow *self,
                                            SyncbusImplFolder *folder);

G_END_DECLS
