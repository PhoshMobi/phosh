/*
 * Copyright (C) 2018-2019 Sébastien Wilmet
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dh-dconf-migration.h"
#include <dconf.h>

/* #DhDconfMigration is a utility class to copy the value of dconf keys from old
 * paths to new paths.
 *
 * You'll probably need to add a "migration-done" #GSettings key to know if the
 * migration has already been done or not (either with a boolean, or an integer
 * if you plan to have other migrations in the future).
 *
 * Use-case examples:
 * - When a project is renamed. The old #GSettings schema may no longer be
 *   installed, so it is not possible to use the #GSettings API to retrieve the
 *   values at the old locations. But the values are still stored in the dconf
 *   database.
 * - Be able to do refactorings in the #GSettings schema without users losing
 *   their settings when *upgrading* to a new version (doesn't work when
 *   downgrading).
 * - When a library uses #GSettings, with parallel-installability for different
 *   major versions, each major version provides a different #GSettings schema,
 *   but when upgrading to a new major version we don't want all the users to
 *   lose their settings. An alternative is for the library to install only
 *   relocatable schemas, relocated to an old common path (if all the keys of
 *   that schema are still compatible). When a schema (or sub-schema) becomes
 *   incompatible, the compatible keys can be migrated individually with
 *   #DhDconfMigration.
 */

/* Tested inside a Flatpak sandbox, works fine. */

struct _DhDconfMigration {
        DConfClient *client;
};

DhDconfMigration *
_dh_dconf_migration_new (void)
{
        DhDconfMigration *migration;

        migration = g_new0 (DhDconfMigration, 1);
        migration->client = dconf_client_new ();

        return migration;
}

/*
 * _dh_dconf_migration_migrate_key:
 * @migration: a #DhDconfMigration.
 * @new_key_path: the dconf path to the new key.
 * @first_old_key_path: the dconf path to the first old key.
 * @...: %NULL-terminated list of strings containing the dconf paths to the old
 *   keys (usually from most recent to the oldest).
 *
 * Copies a value from an old path to a new path. The values on the old paths
 * are not reset, it is just a copy.
 *
 * The function loops on the old key paths, in the same order as provided; as
 * soon as an old key contains a value, that value is copied to @new_key_path
 * and the function returns. Which means that, usually, all the keys must be
 * provided in reverse chronological order.
 */
void
_dh_dconf_migration_migrate_key (DhDconfMigration *migration,
                                 const gchar      *new_key_path,
                                 const gchar      *first_old_key_path,
                                 ...)
{
        va_list old_key_paths;
        GVariant *value;

        g_return_if_fail (migration != NULL);
        g_return_if_fail (new_key_path != NULL);
        g_return_if_fail (first_old_key_path != NULL);

        va_start (old_key_paths, first_old_key_path);

        value = dconf_client_read (migration->client, first_old_key_path);

        while (value == NULL) {
                const gchar *next_old_key_path;

                next_old_key_path = va_arg (old_key_paths, const gchar *);
                if (next_old_key_path == NULL)
                        break;

                value = dconf_client_read (migration->client, next_old_key_path);
        }

        if (value != NULL) {
                GError *error = NULL;

                /* The GVariant type is not checked against the new GSettings
                 * schema. If we write a value with an incompatible type by
                 * mistake, no problem, GSettings will take the default value
                 * from the schema (without printing a warning).
                 */
                dconf_client_write_fast (migration->client, new_key_path, value, &error);

                if (error != NULL) {
                        g_warning ("Error when migrating dconf key %s: %s",
                                   new_key_path,
                                   error->message);
                        g_clear_error (&error);
                }

                g_variant_unref (value);
        }

        va_end (old_key_paths);
}

void
_dh_dconf_migration_sync_and_free (DhDconfMigration *migration)
{
        if (migration == NULL)
                return;

        dconf_client_sync (migration->client);

        g_object_unref (migration->client);
        g_free (migration);
}
