/*
 * Copyright (C) 2018-2019 Sébastien Wilmet
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DH_DCONF_MIGRATION_H
#define DH_DCONF_MIGRATION_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _DhDconfMigration DhDconfMigration;

G_GNUC_INTERNAL
DhDconfMigration *      _dh_dconf_migration_new                 (void);

G_GNUC_INTERNAL
void                    _dh_dconf_migration_migrate_key         (DhDconfMigration *migration,
                                                                 const gchar      *new_key_path,
                                                                 const gchar      *first_old_key_path,
                                                                 ...);

G_GNUC_INTERNAL
void                    _dh_dconf_migration_sync_and_free       (DhDconfMigration *migration);

G_END_DECLS

#endif /* DH_DCONF_MIGRATION_H */
