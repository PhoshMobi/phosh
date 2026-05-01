/*
 * Copyright (C) 2019 Zander Brown
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Authors: Zander Brown <zbrown@gnome.org>
 *          Gotam Gorabh <gautamy672@gmail.com>
 */

#include "search-client.h"
#include "search-result-meta.h"
#include "phosh-searchd.h"

static GMainLoop *loop = NULL;
gboolean searching_new = FALSE;

struct SearchData {
  const char *search_term;
  const char *source_id;
  const char *result_id;
};


static void
print_meta_info (PhoshSearchClient *client, PhoshSearchResultMeta *meta)
{
  g_autofree char *title = phosh_search_client_markup_string (client, phosh_search_result_meta_get_title (meta));
  g_autofree char *desc = phosh_search_client_markup_string (client, phosh_search_result_meta_get_description (meta));

  g_print (" Result Id - %s\n", phosh_search_result_meta_get_id (meta));
  g_print ("             Title: %s\n", title);
  g_print ("             Description: %s\n", desc);
  if (phosh_search_result_meta_get_icon (meta))
    g_print ("             Icon Type: %s\n", G_OBJECT_TYPE_NAME (phosh_search_result_meta_get_icon (meta)));

  if (phosh_search_result_meta_get_clipboard_text (meta))
    g_print ("             Clipboard Text: %s\n", phosh_search_result_meta_get_clipboard_text (meta));
}


static void
on_source_results_changed (PhoshSearchClient *client, const char *source_id, GPtrArray *results)
{
  g_print ("Source Id: %s\n", source_id);

  if (results->len == 0) {
    g_print ("           - No result found\n\n");
    return;
  }

  for (int i = 0; i < results->len; i++) {
    PhoshSearchResultMeta *result = g_ptr_array_index (results, i);

    print_meta_info (client, result);
  }

  g_print ("\n");
}


static void
got_last_results (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) results = NULL;
  g_autofree const char *source_id = NULL;
  g_autoptr (GVariant) meta_array = NULL;
  GVariantIter iter;

  results = phosh_search_client_get_last_results_finish (PHOSH_SEARCH_CLIENT (source),
                                                         result,
                                                         &error);

  if (!results) {
    g_warning ("Failed to get last results: %s", error->message);
    g_main_loop_quit (loop);
    return;
  }

  g_variant_iter_init (&iter, results);
  while (g_variant_iter_next (&iter, "{s@aa{sv}}", &source_id, &meta_array)) {
    GVariantIter array_iter;
    GVariant *item;

    g_print ("Source Id: %s\n", source_id);

    if (g_variant_n_children (meta_array) == 0)
      g_print ("           - No result found\n");

    g_variant_iter_init (&array_iter, meta_array);
    while ((item = g_variant_iter_next_value (&array_iter))) {
      g_autoptr (PhoshSearchResultMeta) meta_result = NULL;

      meta_result = phosh_search_result_meta_deserialise (item);

      print_meta_info (PHOSH_SEARCH_CLIENT (source), meta_result);

      g_clear_pointer (&item, g_variant_unref);
    }

    g_print ("\n");
  }

  g_print ("Query finished.\n");
  g_main_loop_quit (loop);
}


static void
on_query_finished (PhoshSearchClient *client, gpointer user_data)
{
  if (searching_new) {
    g_print ("Query finished.\n");
    g_main_loop_quit (loop);
    return;
  }

  phosh_search_client_get_last_results (PHOSH_SEARCH_CLIENT (client), got_last_results, NULL);
}


static void
got_query (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  gboolean out_searching;

  out_searching = phosh_search_client_query_finish (PHOSH_SEARCH_CLIENT (source), result, &error);

  if (error) {
    g_warning ("Search failed: %s", error->message);
    g_main_loop_quit (loop);
  }

  if (out_searching) {
    g_print ("Searching for new term, wait...\n\n");
    searching_new = TRUE;
  } else {
    g_print ("Results are already fetched for this term, re-displaying the results\n\n");
  }
}


static void
source_launched (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  gboolean success;

  success = phosh_search_client_launch_source_finish (PHOSH_SEARCH_CLIENT (source), result, &error);

  if (!success)
    g_warning ("Failed to launch source: %s", error->message);

  g_main_loop_quit (loop);
}


static void
result_activated (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  gboolean success;

  success = phosh_search_client_activate_result_finish (PHOSH_SEARCH_CLIENT (source), result, &error);

  if (!success)
    g_warning ("Failed to activate result: %s", error->message);

  g_main_loop_quit (loop);
}


static void
got_client (GObject *source, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (PhoshSearchClient) client = NULL;
  struct SearchData *data = user_data;

  client = phosh_search_client_new_finish (source, result, &error);

  if (!client) {
    g_critical ("Failed to create PhoshSearchClient: %s\n", error->message);
    g_main_loop_quit (loop);
    return;
  }

  if (data->search_term) {
    g_object_connect (client,
                      "signal::source-results-changed", on_source_results_changed, NULL,
                      "signal::query-finished", on_query_finished, NULL,
                      NULL);

    phosh_search_client_query (client, data->search_term, got_query, NULL);
  }

  if (data->source_id && !data->result_id)
    phosh_search_client_launch_source (client, data->source_id, source_launched, NULL);

  if (data->source_id && data->result_id && !data->search_term) {
    phosh_search_client_activate_result (client,
                                         data->source_id,
                                         data->result_id,
                                         result_activated,
                                         NULL);
  }
}


static void
print_command_usage (char **argv)
{
  g_print ("  %s search <search-term> - Perform a search for the given <search-term>\n", argv[0]);
  g_print ("  %s activate <source-id> <result-id> - Activate a specific search result identified by <result-id> from the given <source-id>\n", argv[0]);
  g_print ("  %s launch <source-id> - Launch the specified source to enable further, source-specific searching\n", argv[0]);
}


int
main (int argc, char **argv)
{
  g_autoptr (GOptionContext) opt_context = NULL;
  g_autoptr (GError) err = NULL;
  const char *command = NULL;
  struct SearchData *data;
  const GOptionEntry options [] = {
    { NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL }
  };

  opt_context = g_option_context_new ("- Perform a search using the given term");
  g_option_context_add_main_entries (opt_context, options, NULL);

  if (!g_option_context_parse (opt_context, &argc, &argv, &err)) {
    g_warning ("%s", err->message);
    return 1;
  }

  if (argc < 2) {
    g_print ("Usage:\n");
    print_command_usage (argv);
    return 1;
  }

  command = argv[1];

  /* SearchData initialization */
  data = g_new0 (struct SearchData, 1);

  loop = g_main_loop_new (NULL, FALSE);

  if (g_strcmp0 (command, "search") == 0) {

    if (argc != 3) {
      g_print ("Usage: %s search <search-term>\n", argv[0]);
      return 1;
    }

    data->search_term = argv[2];

    g_print ("Searching for '%s'\n", data->search_term);
    phosh_search_client_new (NULL, got_client, (gpointer)data);
  } else if (g_strcmp0 (command, "activate") == 0) {

    if (argc != 4) {
      g_print ("Usage: %s activate <source-id> <result-id>\n", argv[0]);
      return 1;
    }

    data->source_id = argv[2];
    data->result_id = argv[3];

    g_print ("Activating: '%s'\n", data->result_id);
    phosh_search_client_new (NULL, got_client, (gpointer)data);
  } else if (g_strcmp0 (command, "launch") == 0) {

    if (argc != 3) {
      g_print ("Usage: %s launch <source-id>\n", argv[0]);
      return 1;
    }

    data->source_id = argv[2];

    g_print ("Launching: '%s'\n", data->source_id);
    phosh_search_client_new (NULL, got_client, (gpointer)data);

  } else {
    g_print ("Wrong Command...Usage:\n");
    print_command_usage (argv);
    return 1;
  }

  g_main_loop_run (loop);

  g_main_loop_unref (loop);
  g_free (data);

  return 0;
}
