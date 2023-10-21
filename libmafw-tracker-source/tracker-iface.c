/*
 * This file is a part of MAFW
 *
 * Copyright (C) 2007, 2008, 2009 Nokia Corporation, all rights reserved.
 *
 * Contact: Visa Smolander <visa.smolander@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dbus/dbus-glib.h>
#include <glib.h>
#include <libmafw/mafw.h>
#include <stdlib.h>
#include <string.h>
#include <totem-pl-parser.h>

#include "album-art.h"
#include "key-mapping.h"
#include "mafw-tracker-source-marshal.h"
#include "mafw-tracker-source.h"
#include "tracker-cache.h"
#include "tracker-iface.h"
#include "util.h"

/* ------------------------ Internal types ----------------------- */

/* How many properties to ask for at once - sqlite has a limit of nested
 * queries, see YYSTACKDEPTH compile-time define
 */
#ifndef MAX_SPARQL_OPTIONALS
#define MAX_SPARQL_OPTIONALS 10
#endif

/* Stores information needed to invoke MAFW's callback after getting
   results from tracker */
struct _mafw_query_closure
{
  /* Mafw callback */
  MafwTrackerSongsResultCB callback;
  /* Calback's user_data */
  gpointer user_data;
  /* Cache to store keys and values */
  TrackerCache *cache;
};

struct _mafw_metadata_closure
{
  /* Mafw callback */
  union
  {
    MafwTrackerMetadataResultCB callback;
    MafwTrackerMetadatasResultCB mult_callback;
  };
  /* Callback's user data */
  gpointer user_data;
  /* If the childcount key must be counted instead of aggregated
   * when aggregating data in get_metadata */
  gboolean count_childcount;
  /* Type of service to be used in tracker */
  TrackerObjectType tracker_type;
  /* Cache to store keys and values */
  TrackerCache *cache;
  /* List of paths to the asked items */
  gchar **path_list;
  /* List of keys to the asked items */
  gchar **tracker_keys;
  gchar **uris;
  GPtrArray *results;
};

/* ---------------------------- Globals -------------------------- */

static TrackerSparqlConnection *tc = NULL;
static InfoKeyTable *info_keys = NULL;

/* ------------------------- Private API ------------------------- */

static void
_stats_changed_handler(DBusGProxy *proxy,
                       GPtrArray *change_set,
                       gpointer user_data)
{
  gint i;
  MafwSource *source;
  const gchar **p;
  const gchar *service_type = NULL;

  if (change_set == NULL)
    return;

  source = (MafwSource *)user_data;

  for (i = 0; i < change_set->len; i++)
  {
    p = g_ptr_array_index(change_set, i);
    service_type = p[0];

    if (!service_type)
      continue;

    if (strcmp(service_type, "Music") == 0)
      g_signal_emit_by_name(source, "container-changed", MUSIC_OBJECT_ID);
    else if (strcmp(service_type, "Videos") == 0)
      g_signal_emit_by_name(source, "container-changed", VIDEOS_OBJECT_ID);
    else if (strcmp(service_type, "Playlists") == 0)
      g_signal_emit_by_name(source, "container-changed", PLAYLISTS_OBJECT_ID);
  }
}

static void
_index_state_changed_handler(DBusGProxy *proxy,
                             const gchar *state,
                             gboolean initial_index,
                             gboolean in_merge,
                             gboolean is_paused_manually,
                             gboolean is_paused_for_bat,
                             gboolean is_paused_for_io,
                             gboolean is_indexing_enabled,
                             gpointer user_data)
{
  MafwTrackerSource *source;

  source = MAFW_TRACKER_SOURCE(user_data);

  if ((source->priv->last_progress != 100) &&
      (strcmp(state, "Idle") == 0))
  {
    /* Indexing has finished */
    source->priv->last_progress = 100;
    source->priv->remaining_items = 0;
    source->priv->remaining_time = 0;
    g_signal_emit_by_name(source, "updating", 100,
                          source->priv->processed_items,
                          source->priv->remaining_items,
                          source->priv->remaining_time);
  }
  else if ((source->priv->last_progress == 100) &&
           (strcmp(state, "Indexing") == 0))
  {
    /* Tracker has began to index */
    source->priv->last_progress = 0;
    source->priv->processed_items = 0;
    source->priv->remaining_items = -1;
    source->priv->remaining_items = -1;
    g_signal_emit_by_name(source, "updating", 0,
                          source->priv->processed_items,
                          source->priv->remaining_items,
                          source->priv->remaining_time);
  }
}

static void
_progress_changed_handler (DBusGProxy *proxy,
                           const gchar *service,
                           const gchar *uri,
                           gint items_done,
                           gint items_remaining,
                           gint items_total,
                           gdouble seconds_elapsed,
                           gpointer user_data)
{
  MafwTrackerSource *source;
  gint progress;
  gint time_remaining;

  source = MAFW_TRACKER_SOURCE(user_data);

  /* Reserve 100% when index finishes */
  if (items_total > 0)
    progress = CLAMP(100*items_done/items_total, 0, 99);
  else
    progress = 0;

  if (items_done > 0)
    time_remaining = items_remaining * (seconds_elapsed / items_done);
  else
    time_remaining = -1;

  if ((source->priv->last_progress != progress) ||
      (source->priv->processed_items != items_done) ||
      (source->priv->remaining_items != items_remaining) ||
      (source->priv->remaining_time != time_remaining))
  {
    source->priv->last_progress = progress;
    source->priv->processed_items = items_done;
    source->priv->remaining_items = items_remaining;
    source->priv->remaining_time = time_remaining;
    g_signal_emit_by_name(source, "updating", progress, items_done,
                          items_remaining, time_remaining);
  }
}

static GList *
_build_objectids_from_pathname(TrackerCache *cache)
{
  GList *objectid_list = NULL;
  const GPtrArray *results;
  GValue *value;
  const gchar *uri;
  gchar *pathname;
  gint i;

  results = tracker_cache_values_get_results(cache);

  for (i = 0; i < results->len; i++)
  {
    value = tracker_cache_value_get(cache, MAFW_METADATA_KEY_URI, i);
    uri = g_value_get_string(value);
    pathname = g_filename_from_uri(uri, NULL, NULL);
    objectid_list = g_list_prepend(objectid_list, pathname);
    util_gvalue_free(value);
  }

  objectid_list = g_list_reverse(objectid_list);

  return objectid_list;
}

static GList *
_build_objectids_from_unique_key(TrackerCache *cache)
{
  GList *objectid_list = NULL;
  const GPtrArray *results;
  gchar **tracker_keys;
  gchar *unique_value;
  gint i;
  GValue *value;

  results = tracker_cache_values_get_results(cache);
  tracker_keys = tracker_cache_keys_get_tracker(cache);

  for (i = 0; i < results->len; i++)
  {
    /* Unique key is the first key */
    value = tracker_cache_value_get(cache, tracker_keys[0], i);

    if (G_VALUE_HOLDS_STRING(value))
      unique_value = g_strdup(g_value_get_string(value));
    else if (G_VALUE_HOLDS_INT(value))
      unique_value = g_strdup_printf("%d", g_value_get_int(value));
    else
      unique_value = g_strdup("");

    util_gvalue_free(value);
    objectid_list = g_list_prepend(objectid_list, unique_value);
  }

  g_strfreev(tracker_keys);
  objectid_list = g_list_reverse(objectid_list);

  return objectid_list;
}

static GPtrArray *
_get_sparql_tracker_result(TrackerSparqlCursor *cursor)
{
  GPtrArray *result = g_ptr_array_new();
  gint columns = tracker_sparql_cursor_get_n_columns(cursor);

  while (tracker_sparql_cursor_next(cursor, NULL, NULL))
  {
    int i;
    gchar **row = g_new0(gchar *, columns + 1);

    g_ptr_array_add(result, row);

    for (i = 0; i < columns; i++)
    {
      const gchar *s;

      s = tracker_sparql_cursor_get_string(cursor, i, NULL);

      if (!s)
        s = "";

      row[i] = g_strdup(s);
    }
  }

  return result;
}

static void
_tracker_sparql_query_cb(GObject *object, GAsyncResult *res,
                         gpointer user_data)
{
  MafwResult *mafw_result = NULL;
  struct _mafw_query_closure *mc;
  GError *error = NULL;
  TrackerSparqlCursor *cursor = NULL;

  mc = (struct _mafw_query_closure *)user_data;

  cursor = tracker_sparql_connection_query_finish(
      TRACKER_SPARQL_CONNECTION(object), res, &error);

  if (!error)
  {
    mafw_result = g_new0(MafwResult, 1);
    tracker_cache_values_add_results(mc->cache,
                                     _get_sparql_tracker_result(cursor));
    mafw_result->metadata_values = tracker_cache_build_metadata(mc->cache,
                                                                NULL);
    mafw_result->ids = _build_objectids_from_pathname(mc->cache);

    /* Invoke callback */
    mc->callback(mafw_result, NULL, mc->user_data);
  }
  else
  {
    g_warning("Error while querying: %s\n", error->message);
    mc->callback(NULL, error, mc->user_data);
    g_error_free(error);
  }

  if (cursor)
    g_object_unref(cursor);

  tracker_cache_free(mc->cache);
  g_free(mc);
}

static void
_tracker_sparql_unique_values_cb(GObject *object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
  GError *error = NULL;
  TrackerSparqlCursor *cursor = NULL;
  MafwResult *mafw_result = NULL;
  struct _mafw_query_closure *mc;

  mc = (struct _mafw_query_closure *)user_data;

  cursor = tracker_sparql_connection_query_finish(
      TRACKER_SPARQL_CONNECTION(object), res, &error);

  if (!error)
  {
    mafw_result = g_new0(MafwResult, 1);

    tracker_cache_values_add_results(mc->cache,
                                     _get_sparql_tracker_result(cursor));
    mafw_result->metadata_values =
      tracker_cache_build_metadata(mc->cache, NULL);
    mafw_result->ids = _build_objectids_from_unique_key(mc->cache);

    /* Invoke callback */
    mc->callback(mafw_result, NULL, mc->user_data);
  }
  else
  {
    g_warning("Error while querying: %s\n", error->message);
    mc->callback(NULL, error, mc->user_data);
    g_error_free(error);
  }

  if (cursor)
    g_object_unref(cursor);

  tracker_cache_free(mc->cache);
  g_free(mc);
}

static void
_do_tracker_get_unique_values(gchar **keys,
                              gchar **aggregated_keys,
                              gchar **aggregated_types,
                              char **filters,
                              guint offset,
                              guint count,
                              struct _mafw_query_closure *mc)
{
  gchar *filter = NULL;

  filter = util_build_complex_rdf_filter(filters, NULL);

#ifndef G_DEBUG_DISABLE
  perf_elapsed_time_checkpoint("Ready to query Tracker");
#endif

  gchar *sql = util_build_sparql(TRACKER_TYPE_MUSIC,
                                 TRUE,
                                 keys,
                                 filter,
                                 aggregated_types,
                                 aggregated_keys,
                                 offset,
                                 count,
                                 NULL,
                                 FALSE);

  tracker_sparql_connection_query_async(tc,
                                        sql,
                                        NULL,
                                        _tracker_sparql_unique_values_cb,
                                        mc);
  g_free(sql);
}

static GPtrArray *
_append_sparql_tracker_result(TrackerSparqlCursor *cursor,
                              GPtrArray *results, gint cols)
{
  gint columns = tracker_sparql_cursor_get_n_columns(cursor);
  guint offset;
  guint row_idx = 0;
  gchar **row;

  if (!results)
  {
    results = g_ptr_array_new();
    offset = 0;
  }
  else
  {
    row = g_ptr_array_index(results, 0);
    offset = g_strv_length(row);
  }

  while (tracker_sparql_cursor_next(cursor, NULL, NULL))
  {
    int i;

    if (!offset)
    {
      row = g_new(gchar *, cols + 1);
      g_ptr_array_add(results, row);
    }
    else
      row = g_ptr_array_index(results, row_idx++);

    for (i = 0; i < columns; i++)
    {
      const gchar *s;

      s = tracker_sparql_cursor_get_string(cursor, i, NULL);

      if (!s)
        s = "";

      row[i + offset] = g_strdup(s);
    }

    row[offset + i] = NULL;
  }

  return results;
}

static void
_tracker_sparql_metadata_cb(GObject *object, GAsyncResult *res,
                            gpointer user_data)
{
  GList *metadata_list = NULL;
  struct _mafw_metadata_closure *mc = user_data;
  GError *error = NULL;
  TrackerSparqlCursor *cursor = NULL;

  if (object)
  {
    cursor = tracker_sparql_connection_query_finish(
        TRACKER_SPARQL_CONNECTION(object), res, &error);
  }

  if (!error)
  {
    guint keys_len = g_strv_length(mc->tracker_keys);
    guint columns = 0;

    if (object)
    {
      gchar **row;

      mc->results = _append_sparql_tracker_result(
          cursor, mc->results, keys_len);

      if (mc->results->len)
      {
        row = g_ptr_array_index(mc->results, 0);
        columns = g_strv_length(row);
      }
    }

    if (!mc->results || !mc->results->len || (keys_len == columns))
    {
      /* we have all the chunks */
      tracker_cache_values_add_results(mc->cache,
                                       mc->results);
      mc->results = NULL;
      metadata_list = tracker_cache_build_metadata(
          mc->cache, (const gchar **)mc->path_list);
      mc->mult_callback(metadata_list, NULL, mc->user_data);
      g_list_foreach(metadata_list, (GFunc)mafw_metadata_release, NULL);
      g_list_free(metadata_list);
    }
    else
    {
      /* get another chunk of data */
      gchar **keys = &mc->tracker_keys[columns];
      gchar *sql = util_build_meta_sparql(mc->cache->tracker_type, mc->uris,
                                          keys, MAX_SPARQL_OPTIONALS);

      tracker_sparql_connection_query_async(tc, sql, NULL,
                                            _tracker_sparql_metadata_cb,
                                            mc);
      g_free(sql);
      return;
    }
  }
  else
  {
    g_warning("Error while getting metadata: %s\n",
              error->message);
    mc->mult_callback(NULL, error, mc->user_data);
    g_error_free(error);
  }

  if (cursor)
    g_object_unref(cursor);

  tracker_cache_free(mc->cache);

  if (mc->results)
  {
    g_ptr_array_foreach(mc->results, (GFunc)g_strfreev, NULL);
    g_ptr_array_free(mc->results, TRUE);
  }

  g_strfreev(mc->path_list);
  g_strfreev(mc->tracker_keys);
  g_strfreev(mc->uris);
  g_free(mc);
}

static gboolean
_run_tracker_metadata_cb(gpointer data)
{
  _tracker_sparql_metadata_cb(NULL, NULL, data);

  return FALSE;
}

static gchar **
_uris_to_filenames(gchar **uris)
{
  gchar **filenames;
  gint i;

  filenames = g_new0(gchar *, g_strv_length(uris) + 1);

  for (i = 0; uris[i]; i++)
    filenames[i] = g_filename_from_uri(uris[i], NULL, NULL);

  return filenames;
}

static void
_do_tracker_get_metadata(gchar **uris,
                         gchar **keys,
                         TrackerObjectType tracker_obj_type,
                         MafwTrackerMetadatasResultCB callback,
                         gpointer user_data)
{
  struct _mafw_metadata_closure *mc = NULL;
  gchar **user_keys;
  gchar **pathnames;

  /* Save required information */
  mc = g_new0(struct _mafw_metadata_closure, 1);
  mc->mult_callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(tracker_obj_type,
                                TRACKER_CACHE_RESULT_TYPE_GET_METADATA);

  /* If we have only a URI, add it as a predefined value */
  if (!uris[1])
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_URI,
                                             FALSE,
                                             uris[0]);
  }

  tracker_cache_key_add_several(mc->cache, keys, 1, TRUE);

  user_keys = tracker_cache_keys_get_tracker(mc->cache);

  mc->tracker_keys = keymap_mafw_keys_to_tracker_keys(user_keys,
                                                      tracker_obj_type);
  g_strfreev(user_keys);
  mc->uris = g_strdupv(uris);

  if (g_strv_length(mc->tracker_keys) > 0)
  {
    pathnames = _uris_to_filenames(uris);
    mc->path_list = pathnames;
    gchar *sql = util_build_meta_sparql(tracker_obj_type, uris,
                                        mc->tracker_keys,
                                        MAX_SPARQL_OPTIONALS);

    tracker_sparql_connection_query_async(tc, sql, NULL,
                                          _tracker_sparql_metadata_cb, mc);
    g_free(sql);
  }
  else
    g_idle_add(_run_tracker_metadata_cb, mc);
}

/* ------------------------- Public API ------------------------- */

gchar *
ti_create_filter(const MafwFilter *filter)
{
  GString *clause = NULL;
  gchar *ret_str = NULL;

  if (filter == NULL)
    return NULL;

  /* Convert the filter to RDF */
  clause = g_string_new("");

  if (util_mafw_filter_to_sparql(filter, clause))
    ret_str = g_string_free(clause, FALSE);
  else
  {
    g_warning("Invalid or unsupported filter syntax");
    g_string_free(clause, TRUE);
  }

  return ret_str;
}

gboolean
ti_init(void)
{
  GError *error = NULL;

  if (info_keys == NULL)
    info_keys = keymap_get_info_key_table();

  tc = tracker_sparql_connection_get(NULL, &error);

  if (tc == NULL)
  {
    g_critical("Could not get a connection to Tracker %s. Plugin disabled.",
               error ? error->message : "No error given");
    g_clear_error(&error);
  }

  return tc != NULL;
}

void
ti_init_watch (GObject *source)
{
  DBusGConnection *connection;
  DBusGProxy *proxy;
  GError *error = NULL;
  GType collection_type;

  connection = dbus_g_bus_get(DBUS_BUS_SESSION, &error);

  if (connection == NULL)
  {
    g_warning("Failed to open connection to bus: %s\n", error->message);
    g_error_free(error);
    return;
  }

  proxy = dbus_g_proxy_new_for_name(connection,
                                    "org.freedesktop.Tracker",
                                    "/org/freedesktop/Tracker",
                                    "org.freedesktop.Tracker");

  collection_type = dbus_g_type_get_collection("GPtrArray",
                                               G_TYPE_STRV);
  dbus_g_object_register_marshaller(
    mafw_tracker_source_marshal_VOID__STRING_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN_BOOLEAN,
    G_TYPE_NONE,
    G_TYPE_STRING,
    G_TYPE_BOOLEAN,
    G_TYPE_BOOLEAN,
    G_TYPE_BOOLEAN,
    G_TYPE_BOOLEAN,
    G_TYPE_BOOLEAN,
    G_TYPE_BOOLEAN,
    G_TYPE_INVALID);
  dbus_g_object_register_marshaller(
    mafw_tracker_source_marshal_VOID__STRING_STRING_INT_INT_INT_DOUBLE,
    G_TYPE_NONE,
    G_TYPE_STRING,
    G_TYPE_STRING,
    G_TYPE_INT,
    G_TYPE_INT,
    G_TYPE_INT,
    G_TYPE_DOUBLE,
    G_TYPE_INVALID);

  dbus_g_proxy_add_signal(proxy,
                          "ServiceStatisticsUpdated",
                          collection_type,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(proxy,
                          "IndexStateChange",
                          G_TYPE_STRING,
                          G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN,
                          G_TYPE_BOOLEAN,
                          G_TYPE_INVALID);
  dbus_g_proxy_add_signal(proxy,
                          "IndexProgress",
                          G_TYPE_STRING,
                          G_TYPE_STRING,
                          G_TYPE_INT,
                          G_TYPE_INT,
                          G_TYPE_INT,
                          G_TYPE_DOUBLE,
                          G_TYPE_INVALID);

  dbus_g_proxy_connect_signal(proxy, "ServiceStatisticsUpdated",
                              G_CALLBACK(_stats_changed_handler),
                              source, NULL);
  dbus_g_proxy_connect_signal(proxy, "IndexStateChange",
                              G_CALLBACK(_index_state_changed_handler),
                              source, NULL);
  dbus_g_proxy_connect_signal(proxy, "IndexProgress",
                              G_CALLBACK(_progress_changed_handler),
                              source, NULL);
}

void
ti_deinit()
{
  g_object_unref(tc);
  tc = NULL;
}

void
ti_get_videos(gchar **keys,
              const gchar *rdf_filter,
              gchar **sort_fields,
              guint offset,
              guint count,
              MafwTrackerSongsResultCB callback,
              gpointer user_data)
{
  gchar **tracker_keys;
  gchar **tracker_sort_keys;
  struct _mafw_query_closure *mc;
  gchar **keys_to_query;

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(TRACKER_TYPE_VIDEO,
                                TRACKER_CACHE_RESULT_TYPE_QUERY);

  /* Add requested keys; add also uri, as it will be needed to
   * build object_id list */
  tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, 1, FALSE);
  tracker_cache_key_add_several(mc->cache, keys, 1, TRUE);

  /* Map MAFW keys to Tracker keys */
  keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
  tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
                                                  TRACKER_TYPE_VIDEO);
  g_strfreev(keys_to_query);

  if (sort_fields != NULL)
  {
    tracker_sort_keys = keymap_mafw_sort_keys_to_tracker_keys(
        sort_fields, TRACKER_TYPE_VIDEO);
  }
  else
  {
    tracker_sort_keys = util_create_sort_keys_array(2,
                                                    TRACKER_VKEY_TITLE,
                                                    TRACKER_FKEY_FILENAME);
  }

  /* Query tracker */
  gchar *sql = util_build_sparql(TRACKER_TYPE_VIDEO,
                                 FALSE,
                                 tracker_keys,
                                 rdf_filter,
                                 NULL,
                                 NULL,
                                 offset,
                                 count,
                                 tracker_sort_keys,
                                 FALSE);

  tracker_sparql_connection_query_async(tc,
                                        sql,
                                        NULL,
                                        _tracker_sparql_query_cb,
                                        mc);
  g_free(sql);

  g_strfreev(tracker_keys);
  g_strfreev(tracker_sort_keys);
}

void
ti_get_songs(const gchar *genre,
             const gchar *artist,
             const gchar *album,
             gchar **keys,
             const gchar *user_filter,
             gchar **sort_fields,
             guint offset,
             guint count,
             MafwTrackerSongsResultCB callback,
             gpointer user_data)
{
  gchar **tracker_keys;
  gchar **tracker_sort_keys;
  gchar *sparql_filter = NULL;
  gchar **use_sort_fields;
  struct _mafw_query_closure *mc;
  gchar **keys_to_query = NULL;
  gchar *sql;

  /* Select default sort fields */
  if (!sort_fields)
  {
    if (album)
    {
      use_sort_fields = g_new0(gchar *, 3);
      use_sort_fields[0] = MAFW_METADATA_KEY_TRACK;
      use_sort_fields[1] = MAFW_METADATA_KEY_TITLE;
    }
    else
    {
      use_sort_fields = g_new0(gchar *, 2);
      use_sort_fields[0] = MAFW_METADATA_KEY_TITLE;
    }
  }
  else
  {
    use_sort_fields = sort_fields;
  }

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(TRACKER_TYPE_MUSIC,
                                TRACKER_CACHE_RESULT_TYPE_QUERY);

  /* Save known values */
  if (genre)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_GENRE,
                                             FALSE,
                                             genre);
  }

  if (artist)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST,
                                             FALSE,
                                             artist);
  }

  if (album)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_ALBUM,
                                             FALSE,
                                             album);
  }

  /* Add URI, as it will likely be needed to build ids */
  tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, 1, FALSE);

  /* Add remaining keys */
  tracker_cache_key_add_several(mc->cache, keys, 1, TRUE);

  /* Get the keys to ask tracker */
  keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
  tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
                                                  TRACKER_TYPE_MUSIC);
  g_strfreev(keys_to_query);
  tracker_sort_keys =
    keymap_mafw_sort_keys_to_tracker_keys(use_sort_fields, TRACKER_TYPE_MUSIC);
  sparql_filter = util_create_filter_from_category(
      genre, artist, album, user_filter);

  sql = util_build_sparql(TRACKER_TYPE_MUSIC,
                          FALSE,
                          tracker_keys,
                          sparql_filter,
                          NULL,
                          NULL,
                          offset,
                          count,
                          tracker_sort_keys,
                          FALSE);

  tracker_sparql_connection_query_async(tc,
                                        sql,
                                        NULL,
                                        _tracker_sparql_query_cb,
                                        mc);
  g_free(sql);

  if (sparql_filter)
    g_free(sparql_filter);

  if (use_sort_fields != sort_fields)
    g_free(use_sort_fields);

  g_strfreev(tracker_keys);
  g_strfreev(tracker_sort_keys);
}

void
ti_get_artists(const gchar *genre,
               gchar **keys,
               const gchar *rdf_filter,
               gchar **sort_fields,
               guint offset,
               guint count,
               MafwTrackerSongsResultCB callback,
               gpointer user_data)
{
  const int MAXLEVEL = 2;
  struct _mafw_query_closure *mc;
  gchar *escaped_genre = NULL;
  gchar **filters;
  gchar *tracker_unique_keys[] = { TRACKER_AKEY_ARTIST, NULL };
  gchar **tracker_keys;
  gchar **aggregate_keys;
  gchar *aggregate_types[5] = { 0 };
  gint i;
  MetadataKey *metadata_key;

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(TRACKER_TYPE_MUSIC,
                                TRACKER_CACHE_RESULT_TYPE_UNIQUE);

  filters = g_new0(gchar *, 3);

  /* If genre, retrieve all artists for that genre */
  if (genre)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_GENRE,
                                             FALSE,
                                             genre);
    escaped_genre = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_GENRE,
                                                      TRACKER_TYPE_MUSIC,
                                                      genre);
    filters[0] = g_strdup_printf(SPARQL_QUERY_BY_GENRE, escaped_genre);
    g_free(escaped_genre);
    filters[1] = g_strdup(rdf_filter);
  }
  else
    filters[0] = g_strdup(rdf_filter);

  /* Artist will be used as title */
  tracker_cache_key_add_derived(mc->cache,
                                MAFW_METADATA_KEY_TITLE,
                                FALSE,
                                MAFW_METADATA_KEY_ARTIST);

  /* Insert unique key */
  tracker_cache_key_add_unique(mc->cache, MAFW_METADATA_KEY_ARTIST);

  tracker_cache_key_add_several(mc->cache, keys, MAXLEVEL, TRUE);

  /* Concat albums if requested */
  if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ALBUM))
    tracker_cache_key_add_concat(mc->cache, MAFW_METADATA_KEY_ALBUM);

  /* Get the list of keys to use with tracker */
  tracker_keys = tracker_cache_keys_get_tracker(mc->cache);

  /* Create the array for aggregate keys and their types; skip unique
   * key */
  aggregate_keys = g_new0(gchar *, 5);

  for (i = 1; tracker_keys[i]; i++)
  {
    metadata_key = keymap_get_metadata(tracker_keys[i]);

    switch (metadata_key->special)
    {
      case SPECIAL_KEY_DURATION:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_SUM;
        break;
      }

      case SPECIAL_KEY_CHILDCOUNT:
      {
        /* What is the level requested? */
        if (tracker_keys[i][11] == '1')
          aggregate_keys[i-1] = g_strdup(TRACKER_AKEY_ALBUM);
        else
          aggregate_keys[i-1] = g_strdup("*");

        aggregate_types[i-1] = AGGREGATED_TYPE_COUNT;
        break;
      }

      default:
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_CONCAT;
    }
  }

  g_strfreev(tracker_keys);

  _do_tracker_get_unique_values(tracker_unique_keys,
                                aggregate_keys,
                                aggregate_types,
                                filters,
                                offset,
                                count,
                                mc);

  g_strfreev(filters);
  g_strfreev(aggregate_keys);
}

void
ti_get_genres(gchar **keys,
              const gchar *rdf_filter,
              gchar **sort_fields,
              guint offset,
              guint count,
              MafwTrackerSongsResultCB callback,
              gpointer user_data)
{
  const int MAXLEVEL = 3;
  struct _mafw_query_closure *mc;
  gchar **filters;
  gchar *tracker_unique_keys[] = { TRACKER_AKEY_GENRE, NULL };
  gchar **tracker_keys;
  gchar **aggregate_keys;
  gchar *aggregate_types[6] = { 0 };
  gint i;
  MetadataKey *metadata_key;

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(TRACKER_TYPE_MUSIC,
                                TRACKER_CACHE_RESULT_TYPE_UNIQUE);

  /* Genre will be used as title */
  tracker_cache_key_add_derived(mc->cache,
                                MAFW_METADATA_KEY_TITLE,
                                FALSE,
                                MAFW_METADATA_KEY_GENRE);

  /* Insert unique key */
  tracker_cache_key_add_unique(mc->cache, MAFW_METADATA_KEY_GENRE);
  tracker_cache_key_add_several(mc->cache, keys, MAXLEVEL, TRUE);

  /* Concat artists if requested */
  if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ARTIST))
    tracker_cache_key_add_concat(mc->cache, MAFW_METADATA_KEY_ARTIST);

  filters = g_new0(gchar *, 2);
  filters[0] = g_strdup(rdf_filter);

  /* Get the list of keys to use with tracker */
  tracker_keys = tracker_cache_keys_get_tracker(mc->cache);

  /* Create the array for aggregate keys and their types; skip unique
   * key */
  aggregate_keys = g_new0(gchar *, 6);

  for (i = 1; tracker_keys[i]; i++)
  {
    metadata_key = keymap_get_metadata(tracker_keys[i]);

    switch (metadata_key->special)
    {
      case SPECIAL_KEY_DURATION:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_SUM;
        break;
      }

      case SPECIAL_KEY_CHILDCOUNT:
      {
        /* What is the level requested? */
        if (tracker_keys[i][11] == '1')
          aggregate_keys[i-1] = g_strdup(TRACKER_AKEY_ARTIST);
        else if (tracker_keys[i][11] == '2')
          aggregate_keys[i-1] = g_strdup(TRACKER_AKEY_ALBUM);
        else
          aggregate_keys[i-1] = g_strdup("*");

        aggregate_types[i-1] = AGGREGATED_TYPE_COUNT;
        break;
      }

      default:
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_CONCAT;
    }
  }

  g_strfreev(tracker_keys);

  /* Query tracker */
  _do_tracker_get_unique_values(tracker_unique_keys,
                                aggregate_keys,
                                aggregate_types,
                                filters,
                                offset,
                                count,
                                mc);

  g_strfreev(filters);
  g_strfreev(aggregate_keys);
}

void
ti_get_playlists(gchar **keys,
                 const gchar *user_filter,
                 gchar **sort_fields,
                 guint offset,
                 guint count,
                 MafwTrackerSongsResultCB callback,
                 gpointer user_data)
{
  struct _mafw_query_closure *mc;
  gchar *sparql_filter = NULL;
  gchar **use_sort_fields;
  gchar **tracker_sort_keys;
  gchar **tracker_keys;
  gchar **keys_to_query;

  /* Select default sort fields */
  if (!sort_fields)
  {
    use_sort_fields = g_new0(gchar *, 2);
    use_sort_fields[0] = MAFW_METADATA_KEY_FILENAME;
  }
  else
  {
    use_sort_fields = sort_fields;
  }

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->cache = tracker_cache_new(TRACKER_TYPE_PLAYLIST,
                                TRACKER_CACHE_RESULT_TYPE_QUERY);

  /* Add URI, as it will likely be needed to build ids */
  tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, 1, FALSE);

  tracker_cache_key_add_several(mc->cache, keys, 1, TRUE);

  /* Map to tracker keys */
  keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
  tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
                                                  TRACKER_TYPE_PLAYLIST);
  g_strfreev(keys_to_query);
  tracker_sort_keys =
    keymap_mafw_sort_keys_to_tracker_keys(use_sort_fields, TRACKER_TYPE_MUSIC);
  sparql_filter = util_build_complex_rdf_filter(NULL, user_filter);

  /* Execute query */
  gchar *sql = util_build_sparql(TRACKER_TYPE_PLAYLIST,
                                 FALSE,
                                 tracker_keys,
                                 sparql_filter,
                                 NULL,
                                 NULL,
                                 offset,
                                 count,
                                 tracker_sort_keys,
                                 FALSE);

  tracker_sparql_connection_query_async(tc,
                                        sql,
                                        NULL,
                                        _tracker_sparql_query_cb,
                                        mc);
  g_free(sql);

  if (use_sort_fields != sort_fields)
    g_free(use_sort_fields);

  g_free(sparql_filter);
  g_strfreev(tracker_keys);
  g_strfreev(tracker_sort_keys);
}

void
ti_get_albums(const gchar *genre,
              const gchar *artist,
              gchar **keys,
              const gchar *rdf_filter,
              gchar **sort_fields,
              guint offset,
              guint count,
              MafwTrackerSongsResultCB callback,
              gpointer user_data)
{
  const int MAXLEVEL = 1;
  gchar *escaped_genre;
  gchar *escaped_artist;
  gchar **filters;
  gchar *tracker_unique_keys[] = { TRACKER_AKEY_ALBUM, NULL };
  gchar **tracker_keys;
  gchar **aggregate_keys;
  gchar *aggregate_types[4] = { 0 };
  gint i;
  MetadataKey *metadata_key;
  struct _mafw_query_closure *mc;

  /* Prepare mafw closure struct */
  mc = g_new0(struct _mafw_query_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;

  mc->cache = tracker_cache_new(TRACKER_TYPE_MUSIC,
                                TRACKER_CACHE_RESULT_TYPE_UNIQUE);

  filters = g_new0(gchar *, 4);

  /* If genre and/or artist, retrieve all albums for that genre
   * and/or artist */
  i = 0;

  if (genre)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_GENRE,
                                             FALSE,
                                             genre);
    escaped_genre = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_GENRE,
                                                      TRACKER_TYPE_MUSIC,
                                                      genre);
    filters[i] = g_strdup_printf(SPARQL_QUERY_BY_GENRE, escaped_genre);
    g_free(escaped_genre);
    i++;
  }

  if (artist)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST,
                                             FALSE,
                                             artist);
    escaped_artist = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_ARTIST,
                                                       TRACKER_TYPE_MUSIC,
                                                       artist);
    filters[i] = g_strdup_printf(SPARQL_QUERY_BY_ARTIST, escaped_artist);
    g_free(escaped_artist);
    i++;
  }

  filters[i] = g_strdup(rdf_filter);

  /* Album will be used as title */
  tracker_cache_key_add_derived(mc->cache,
                                MAFW_METADATA_KEY_TITLE,
                                FALSE,
                                MAFW_METADATA_KEY_ALBUM);

  /* Insert unique key */
  tracker_cache_key_add_unique(mc->cache, MAFW_METADATA_KEY_ALBUM);

  /* Add user keys */
  tracker_cache_key_add_several(mc->cache, keys, MAXLEVEL, TRUE);

  /* Concat artists, if requested */
  if (!artist &&
      tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ARTIST))
  {
    tracker_cache_key_add_concat(mc->cache,
                                 MAFW_METADATA_KEY_ARTIST);
  }

  /* Get the list of keys to use with tracker */
  tracker_keys = tracker_cache_keys_get_tracker(mc->cache);

  /* Create the array for aggregate keys and their types; skip unique
   * key */
  aggregate_keys = g_new0(gchar *, 4);

  for (i = 1; tracker_keys[i]; i++)
  {
    metadata_key = keymap_get_metadata(tracker_keys[i]);

    switch (metadata_key->special)
    {
      case SPECIAL_KEY_DURATION:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_SUM;
        break;
      }

      case SPECIAL_KEY_CHILDCOUNT:
      {
        aggregate_keys[i-1] = g_strdup("*");
        aggregate_types[i-1] = AGGREGATED_TYPE_COUNT;
        break;
      }

      default:
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_CONCAT;
    }
  }

  g_strfreev(tracker_keys);

  _do_tracker_get_unique_values(tracker_unique_keys,
                                aggregate_keys,
                                aggregate_types,
                                filters,
                                offset,
                                count,
                                mc);

  g_strfreev(filters);
  g_strfreev(aggregate_keys);
}

static void
_tracker_metadata_from_container_cb(GPtrArray *tracker_result,
                                    GError *error,
                                    gpointer user_data)
{
  GHashTable *metadata;
  struct _mafw_metadata_closure *mc = user_data;

  if (!error)
  {
    tracker_cache_values_add_results(mc->cache, tracker_result);
    metadata =
      tracker_cache_build_metadata_aggregated(mc->cache, mc->count_childcount);
    mc->callback(metadata, NULL, mc->user_data);
  }
  else
    mc->callback(NULL, error, mc->user_data);

  tracker_cache_free(mc->cache);
  g_free(mc);
}

static void
_tracker_sparql_metadata_from_container_cb(GObject *object,
                                           GAsyncResult *res,
                                           gpointer user_data)
{
  GError *error = NULL;
  TrackerSparqlCursor *cursor = NULL;
  struct _mafw_metadata_closure *mc =
    (struct _mafw_metadata_closure *)user_data;

  cursor = tracker_sparql_connection_query_finish(
      TRACKER_SPARQL_CONNECTION(object), res, &error);

  if (!error)
  {
    GHashTable *metadata;

    tracker_cache_values_add_results(mc->cache,
                                     _get_sparql_tracker_result(cursor));
    metadata = tracker_cache_build_metadata_aggregated(mc->cache,
                                                       mc->count_childcount);
    mc->callback(metadata, NULL, mc->user_data);
  }
  else
  {
    mc->callback(NULL, error, mc->user_data);
    g_error_free(error);
  }

  if (cursor)
    g_object_unref(cursor);

  tracker_cache_free(mc->cache);
  g_free(mc);
}

static gboolean
_run_tracker_metadata_from_container_cb(gpointer data)
{
  GPtrArray *results = g_ptr_array_sized_new(0);

  _tracker_metadata_from_container_cb(results, NULL, data);

  return FALSE;
}

static void
_do_tracker_get_metadata_from_service(gchar **keys,
                                      const gchar *title,
                                      TrackerObjectType tracker_type,
                                      MafwTrackerMetadataResultCB callback,
                                      gpointer user_data)
{
  gchar *aggregate_types[3] = { 0 };
  gchar *aggregate_keys[3] = { 0 };
  gchar **unique_keys;
  gchar **tracker_keys;
  MetadataKey *metadata_key;
  gint i;
  struct _mafw_metadata_closure *mc = NULL;

  mc = g_new0(struct _mafw_metadata_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;
  mc->tracker_type = tracker_type;
  mc->count_childcount = FALSE;
  unique_keys = g_new0(gchar *, 2);
  unique_keys[0] = g_strdup(TRACKER_FKEY_MIME);

  mc->cache = tracker_cache_new(tracker_type, TRACKER_CACHE_RESULT_TYPE_UNIQUE);

  tracker_cache_key_add_unique(mc->cache, MAFW_METADATA_KEY_MIME);

  if (title)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_TITLE,
                                             FALSE,
                                             title);
  }

  tracker_cache_key_add_several(mc->cache, keys, 1, TRUE);

  /* Get the list of keys to use with tracker */
  tracker_keys = tracker_cache_keys_get_tracker(mc->cache);

  /* Create the array for aggregate keys and their types; skip unique
   * key */
  for (i = 1; tracker_keys[i]; i++)
  {
    metadata_key = keymap_get_metadata(tracker_keys[i]);

    switch (metadata_key->special)
    {
      case SPECIAL_KEY_DURATION:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], tracker_type);
        aggregate_types[i-1] = AGGREGATED_TYPE_SUM;
        break;
      }

      case SPECIAL_KEY_CHILDCOUNT:
      {
        aggregate_keys[i-1] = g_strdup("*");
        aggregate_types[i-1] = AGGREGATED_TYPE_COUNT;
        break;
      }

      default:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], tracker_type);
        aggregate_types[i-1] = AGGREGATED_TYPE_CONCAT;
        break;
      }
    }
  }

  g_strfreev(tracker_keys);

  if (aggregate_keys[0])
  {
    gchar *sql = util_build_sparql(tracker_type,
                                   TRUE,
                                   unique_keys,
                                   NULL,
                                   aggregate_types,
                                   aggregate_keys,
                                   0,
                                   0,
                                   NULL,
                                   FALSE);

    tracker_sparql_connection_query_async(
      tc,
      sql,
      NULL,
      _tracker_sparql_metadata_from_container_cb,
      mc);
    g_free(sql);
  }
  else
    g_idle_add(_run_tracker_metadata_from_container_cb, mc);

  g_strfreev(unique_keys);
  g_free(aggregate_keys[0]);
  g_free(aggregate_keys[1]);
  g_free(aggregate_keys[2]);
}

void
ti_get_metadata_from_videos(gchar **keys,
                            const gchar *title,
                            MafwTrackerMetadataResultCB callback,
                            gpointer user_data)
{
  _do_tracker_get_metadata_from_service(keys, title, TRACKER_TYPE_VIDEO,
                                        callback, user_data);
}

void
ti_get_metadata_from_music(gchar **keys,
                           const gchar *title,
                           MafwTrackerMetadataResultCB callback,
                           gpointer user_data)
{
  _do_tracker_get_metadata_from_service(keys, title, TRACKER_TYPE_MUSIC,
                                        callback, user_data);
}

void
ti_get_metadata_from_playlists(gchar **keys,
                               const gchar *title,
                               MafwTrackerMetadataResultCB callback,
                               gpointer user_data)
{
  _do_tracker_get_metadata_from_service(keys, title,
                                        TRACKER_TYPE_PLAYLIST,
                                        callback, user_data);
}

void
ti_get_metadata_from_category(const gchar *genre,
                              const gchar *artist,
                              const gchar *album,
                              const gchar *default_count_key,
                              const gchar *title,
                              gchar **keys,
                              MafwTrackerMetadataResultCB callback,
                              gpointer user_data)
{
  gchar *filter;
  gint MAXLEVEL;
  struct _mafw_metadata_closure *mc;
  const gchar *ukey;
  gchar *tracker_ukeys[2] = { 0 };
  gchar *aggregate_types[7] = { 0 };
  gchar **aggregate_keys;
  gchar **tracker_keys;
  gint i;
  MetadataKey *metadata_key;
  const gchar *count_keys[] = { TRACKER_AKEY_GENRE, TRACKER_AKEY_ARTIST,
                                TRACKER_AKEY_ALBUM, "*" };
  gint level;
  gint start_to_look;

  mc = g_new0(struct _mafw_metadata_closure, 1);
  mc->callback = callback;
  mc->user_data = user_data;

  /* Create cache */
  mc->cache =
    tracker_cache_new(TRACKER_TYPE_MUSIC, TRACKER_CACHE_RESULT_TYPE_UNIQUE);

  /* Preset metadata that we know already */
  if (genre)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_GENRE,
                                             FALSE,
                                             genre);
  }

  if (artist)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST,
                                             FALSE,
                                             artist);
  }

  if (album)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_ALBUM,
                                             FALSE,
                                             album);
  }

  /* Select the key that will be used as title */
  if (album)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_TITLE,
                                             FALSE,
                                             album);
  }
  else if (artist)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_TITLE,
                                             FALSE,
                                             artist);
  }
  else if (genre)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_TITLE,
                                             FALSE,
                                             genre);
  }
  else if (title)
  {
    tracker_cache_key_add_precomputed_string(mc->cache,
                                             MAFW_METADATA_KEY_TITLE,
                                             FALSE,
                                             title);
  }

  /* Select unique key to use, and other data */
  if (album)
  {
    ukey = MAFW_METADATA_KEY_ALBUM;
    MAXLEVEL = 1;
    start_to_look = 3;
    mc->count_childcount = FALSE;
  }
  else if (artist)
  {
    ukey = MAFW_METADATA_KEY_ARTIST;
    MAXLEVEL = 2;
    start_to_look = 2;
    mc->count_childcount = FALSE;
  }
  else if (genre)
  {
    ukey = MAFW_METADATA_KEY_GENRE;
    MAXLEVEL = 3;
    start_to_look = 1;
    mc->count_childcount = FALSE;
  }
  else
  {
    ukey = default_count_key;
    mc->count_childcount = TRUE;

    if (strcmp(default_count_key, "genre") == 0)
    {
      MAXLEVEL = 4;
      start_to_look = 0;
    }
    else if (strcmp(default_count_key, "artist") == 0)
    {
      MAXLEVEL = 3;
      start_to_look = 1;
    }
    else if (strcmp(default_count_key, "album") == 0)
    {
      MAXLEVEL = 2;
      start_to_look = 2;
    }
    else
    {
      MAXLEVEL = 1;
      start_to_look = 3;
    }
  }

  /* Add required keys to the cache (beware: order is important) */
  tracker_cache_key_add_unique(mc->cache, ukey);

  tracker_cache_key_add_several(mc->cache, keys, MAXLEVEL, TRUE);

  /* Check if we have to concat keys */
  if (artist && !album &&
      tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ALBUM))
  {
    /* Concatenate albums if requesting metadata from an artist */
    tracker_cache_key_add_concat(mc->cache, MAFW_METADATA_KEY_ALBUM);
  }
  else if (!artist && album &&
           tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ARTIST))
  {
    /* Concatenate artist if requesting metadata from an album */
    tracker_cache_key_add_concat(mc->cache, MAFW_METADATA_KEY_ARTIST);
  }

  /* Compute tracker filter and tracker keys */
  filter = util_create_filter_from_category(genre, artist, album, NULL);

  tracker_ukeys[0] = keymap_mafw_key_to_tracker_key(ukey, TRACKER_TYPE_MUSIC);

  /* Get the list of keys to use with tracker */
  tracker_keys = tracker_cache_keys_get_tracker(mc->cache);

  /* Create the array for aggregate keys and their types; skip unique
   * key */
  aggregate_keys = g_new0(gchar *, 7);

  for (i = 1; tracker_keys[i]; i++)
  {
    metadata_key = keymap_get_metadata(tracker_keys[i]);

    switch (metadata_key->special)
    {
      case SPECIAL_KEY_DURATION:
      {
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_SUM;
        break;
      }

      case SPECIAL_KEY_CHILDCOUNT:
      {
        level = g_ascii_digit_value(tracker_keys[i][11]);
        aggregate_keys[i-1] = g_strdup(count_keys[start_to_look + level - 1]);
        aggregate_types[i-1] = AGGREGATED_TYPE_COUNT;
        break;
      }

      default:
        aggregate_keys[i-1] =
          keymap_mafw_key_to_tracker_key(tracker_keys[i], TRACKER_TYPE_MUSIC);
        aggregate_types[i-1] = AGGREGATED_TYPE_CONCAT;
    }
  }

  g_strfreev(tracker_keys);

  if (aggregate_keys[0])
  {
    gchar *sql = util_build_sparql(TRACKER_TYPE_MUSIC,
                                   TRUE,
                                   tracker_ukeys,
                                   filter,
                                   aggregate_types,
                                   aggregate_keys,
                                   0,
                                   0,
                                   NULL,
                                   FALSE);

    tracker_sparql_connection_query_async(
      tc,
      sql,
      NULL,
      _tracker_sparql_metadata_from_container_cb,
      mc);
    g_free(sql);
  }
  else
    g_idle_add(_run_tracker_metadata_from_container_cb, mc);

  g_free(filter);
  g_free(tracker_ukeys[0]);
  g_strfreev(aggregate_keys);
}

void
ti_get_metadata_from_videoclip(gchar **uris,
                               gchar **keys,
                               MafwTrackerMetadatasResultCB callback,
                               gpointer user_data)
{
  _do_tracker_get_metadata(uris, keys, TRACKER_TYPE_VIDEO, callback, user_data);
}

void
ti_get_metadata_from_audioclip(gchar **uris,
                               gchar **keys,
                               MafwTrackerMetadatasResultCB callback,
                               gpointer user_data)
{
  _do_tracker_get_metadata(uris, keys, TRACKER_TYPE_MUSIC, callback, user_data);
}

void
ti_get_metadata_from_playlist(gchar **uris,
                              gchar **keys,
                              MafwTrackerMetadatasResultCB callback,
                              gpointer user_data)
{
  _do_tracker_get_metadata(
    uris, keys, TRACKER_TYPE_PLAYLIST, callback, user_data);
}

gchar **
ti_set_metadata(const gchar *uri, GHashTable *metadata, CategoryType category,
                gboolean *updated, GError **error)
{
  GList *keys = NULL;
  GList *running_key;
  gint count_keys;
  gint i_key, u_key;
  gchar **keys_array = NULL;
  gchar **values_array = NULL;
  gchar **unsupported_array = NULL;
  gchar *mafw_key;
  GValue *value;
  gboolean updatable;
  TrackerObjectType tracker_type;
  static InfoKeyTable *t = NULL;
  TrackerKey *tracker_key;

  *error = NULL;

  if (!t)
    t = keymap_get_info_key_table();

  /* We have not updated anything yet */
  if (updated)
    *updated = FALSE;

  /* Get list of keys */
  keys = g_hash_table_get_keys(metadata);

  count_keys = g_list_length(keys);
  keys_array = g_new0(gchar *, count_keys + 1);
  values_array = g_new0(gchar *, count_keys + 1);

  if (category == CATEGORY_VIDEO)
    tracker_type = TRACKER_TYPE_VIDEO;
  else
    tracker_type = TRACKER_TYPE_MUSIC;

  /* Convert lists to arrays */
  running_key = keys;
  i_key = 0;
  u_key = 0;

  while (running_key)
  {
    mafw_key = (gchar *)running_key->data;
    updatable = TRUE;

    /* Get only supported keys, and convert values to
     * strings */
    if (keymap_mafw_key_is_writable(mafw_key))
    {
      /* Special case: some keys should follow ISO-8601
       * spec */
      tracker_key = keymap_get_tracker_info(mafw_key, tracker_type);

      if (tracker_key && (tracker_key->value_type == G_TYPE_DATE))
      {
        value = mafw_metadata_first(metadata, mafw_key);

        if (value && G_VALUE_HOLDS_LONG(value))
          values_array[i_key] = util_epoch_to_iso8601(g_value_get_long(value));
        else
          updatable = FALSE;
      }
      else
      {
        value = mafw_metadata_first(metadata, mafw_key);

        if (value)
        {
          switch (G_VALUE_TYPE(value))
          {
            case G_TYPE_STRING:
            {
              values_array[i_key] = g_value_dup_string(value);
              break;
            }
            default:
            {
              values_array[i_key] = g_strdup_value_contents(value);
              break;
            }
          }
        }
      }
    }
    else
      updatable = FALSE;

    if (updatable)
    {
      keys_array[i_key] =
        keymap_mafw_key_to_tracker_key(mafw_key, tracker_type);
      i_key++;
    }
    else
    {
      if (!unsupported_array)
        unsupported_array = g_new0(gchar *, count_keys+1);

      unsupported_array[u_key] = g_strdup(mafw_key);
      u_key++;
    }

    running_key = g_list_next(running_key);
  }

  g_list_free(keys);

  /* If there are some updatable keys, call tracker to update them */
  if (keys_array[0] != NULL)
  {
    TrackerSparqlCursor *cursor;
    gchar *sparql;
    gboolean object_exists = FALSE;

    sparql = util_build_update_sparql(tracker_type, uri, NULL, NULL, TRUE);

    cursor = tracker_sparql_connection_query(tc, sparql, NULL, error);
    g_free(sparql);

    if (!*error)
    {
      if (tracker_sparql_cursor_next(cursor, NULL, error))
        object_exists = TRUE;

      g_object_unref(cursor);

      if (object_exists)
      {
        sparql = util_build_update_sparql(tracker_type, 
                                          uri,
                                          keys_array,
                                          values_array,
                                          FALSE);
        tracker_sparql_connection_update(tc, sparql, 0, NULL, error);

        g_free(sparql);
      }
      else if (!*error)
      {
        *error = g_error_new(MAFW_SOURCE_ERROR,
                             MAFW_SOURCE_ERROR_OBJECT_ID_NOT_AVAILABLE,
                             "There is no object with such id");
      }
    }

    if (*error || !object_exists)
    {
      /* Tracker_metadata_set is an atomic operation; so
       * none of the keys were updated */
      i_key = 0;

      while (keys_array[i_key])
      {
        if (!unsupported_array)
          unsupported_array = g_new0(gchar *, count_keys+1);

        unsupported_array[u_key] = g_strdup(keys_array[i_key]);
        i_key++;
        u_key++;
      }
    }
    else if (updated)
    {
      /* We successfully updated some keys at least */
      *updated = TRUE;
    }
  }

  g_strfreev(keys_array);
  g_strfreev(values_array);

  return unsupported_array;
}

static void
_set_playlist_duration_cb(GObject *object, GAsyncResult *res,
                          gpointer user_data)
{
  GError *error = NULL;

  tracker_sparql_connection_update_finish(
    TRACKER_SPARQL_CONNECTION(object), res, &error);

  if (error != NULL)
  {
    g_warning("Error while setting the playlist duration: "
              "%s", error->message);
    g_error_free(error);
  }
}

void
ti_set_playlist_duration(const gchar *uri, guint duration)
{
  GString *sql = g_string_new("");

  /* Store in Tracker the new value for the playlist duration */
  g_string_append_printf(sql,
                         "DELETE {?o nfo:listDuration ?d} "
                         "INSERT {?o nfo:listDuration %d} "
                         "WHERE {?o a nmm:Playlist.?o nie:url '%s'. "
                         "OPTIONAL{?o nfo:listDuration ?d}}",
                         duration, uri);

  tracker_sparql_connection_update_async(tc, sql->str, G_PRIORITY_DEFAULT, NULL,
                                         _set_playlist_duration_cb, NULL);

  g_string_free(sql, TRUE);
}
