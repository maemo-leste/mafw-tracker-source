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

#include <glib.h>
#include <string.h>
#include <tracker.h>
#include <libmafw/mafw.h>
#include <totem-pl-parser.h>
#include <stdlib.h>
#include <dbus/dbus-glib.h>

#include "tracker-iface.h"
#include "tracker-cache.h"
#include "mafw-tracker-source.h"
#include "util.h"
#include "album-art.h"
#include "key-mapping.h"

/* ------------------------ Internal types ----------------------- */

/* Stores information needed to invoke MAFW's callback after getting
   results from tracker */
struct _mafw_query_closure {
	/* Mafw callback */
	MafwTrackerSongsResultCB callback;
	/* Calback's user_data */
	gpointer user_data;
        /* Cache to store keys and values */
        TrackerCache *cache;
};

struct _mafw_metadata_closure {
        /* Mafw callback */
        union {
                MafwTrackerMetadataResultCB callback;
                MafwTrackerMetadatasResultCB mult_callback;
        };
        /* Callback's user data */
        gpointer user_data;
        /* If the childcount key must be counted instead of aggregated
         * when aggregating data in get_metadata */
        gboolean count_childcount;
        /* Cache to store keys and values */
        TrackerCache *cache;
};

/* ---------------------------- Globals -------------------------- */

static TrackerClient *tc = NULL;
static InfoKeyTable *info_keys = NULL;
static GHashTable *types_map = NULL;

/* ------------------------- Private API ------------------------- */

static void _stats_changed_handler(DBusGProxy *proxy,
				   GPtrArray *change_set,
				   gpointer user_data)
{	gint i;
	MafwSource *source;
	const gchar **p;
	const gchar *service_type = NULL;

	if (change_set == NULL) {
		return;
	}

	source = (MafwSource *) user_data;

	for (i = 0; i < change_set->len; i++) {
		p = g_ptr_array_index(change_set, i);
		service_type = p[0];

                if (!service_type) {
                        continue;
                }

		if (strcmp(service_type, "Music") == 0) {
			g_signal_emit_by_name(source,
					      "container-changed",
					      MUSIC_OBJECT_ID);
		} else if (strcmp(service_type, "Videos") == 0) {
			g_signal_emit_by_name(source,
					      "container-changed",
					      VIDEOS_OBJECT_ID);
		}
	}
}

static gchar *_ids_concat(gchar **parts)
{
        gchar **fixed_parts;
        gchar *joined;
        gint i;

        if (g_strv_length(parts) <= 1) {
                return g_strdup(parts[0]);
        }

        /* Replace each occurrence of "-" by "--" */
        fixed_parts = g_new0(gchar *, g_strv_length(parts) + 1);
        i=0;
        while (parts[i]) {
                fixed_parts[i] = util_str_replace(parts[i], "-", "--");
                i++;
        }

        /* Join result */
        joined = g_strjoinv(" - ", fixed_parts);

        g_strfreev(fixed_parts);

        return joined;
}

static GList *_build_objectids_from_pathname(TrackerCache *cache)
{
        GList *objectid_list = NULL;
        const GPtrArray *results;
        GValue *value;
        const gchar *uri;
        gchar *pathname;
        gint i;

        results = tracker_cache_values_get_results(cache);
        for (i = 0; i < results->len; i++) {
                value = tracker_cache_value_get(cache, MAFW_METADATA_KEY_URI, i);
                uri = g_value_get_string(value);
                pathname = g_filename_from_uri(uri, NULL, NULL);
                objectid_list = g_list_prepend(objectid_list, pathname);
                util_gvalue_free(value);
        }
        objectid_list = g_list_reverse(objectid_list);

        return objectid_list;
}

static GList *_build_objectids_from_unique_keys(TrackerCache *cache)
{
        GList *objectid_list = NULL;
        const GPtrArray *results;
        gchar **unique_keys;
        gchar **values;
        gint unique_keys_length;
        gint i;
        gint key_index;
        GValue *value;

        results = tracker_cache_values_get_results(cache);
        unique_keys = tracker_cache_keys_get_tracker(cache);
        unique_keys_length = g_strv_length(unique_keys);

        for (i = 0; i < results->len; i++) {
                values = g_new0(gchar *, unique_keys_length + 1);
                key_index = 0;
                while (unique_keys[key_index]) {
                        value = tracker_cache_value_get(cache,
                                                        unique_keys[key_index],
                                                        i);
                        if (G_VALUE_HOLDS_STRING(value)) {
                                values[key_index] =
                                        g_strdup(g_value_get_string(value));
                        } else if (G_VALUE_HOLDS_INT(value)) {
                                values[key_index] =
                                        g_strdup_printf("%d",
                                                        g_value_get_int(value));
                        } else {
                                values[key_index] = "";
                        }
                        util_gvalue_free(value);
                        key_index++;
                }
                objectid_list = g_list_prepend(objectid_list,
                                               _ids_concat(values));
                g_strfreev(values);
        }
        g_strfreev(unique_keys);
        objectid_list = g_list_reverse(objectid_list);

        return objectid_list;
}

static void _tracker_query_cb(GPtrArray *tracker_result,
                              GError *error,
                              gpointer user_data)
{
	MafwResult *mafw_result = NULL;
	struct _mafw_query_closure *mc;

	mc = (struct _mafw_query_closure *) user_data;

	if (error == NULL) {
                mafw_result = g_new0(MafwResult, 1);
                tracker_cache_values_add_results(mc->cache, tracker_result);
                mafw_result->metadata_values =
                        tracker_cache_build_metadata(mc->cache);
                mafw_result->ids =
                        _build_objectids_from_pathname(mc->cache);

                /* Invoke callback */
                mc->callback(mafw_result, NULL, mc->user_data);
	} else {
                g_warning("Error while querying: %s\n", error->message);
                mc->callback(NULL, error, mc->user_data);
        }

        tracker_cache_free(mc->cache);
        g_free(mc);
}

static void _tracker_unique_values_cb(GPtrArray *tracker_result,
				      GError *error,
				      gpointer user_data)
{
	MafwResult *mafw_result = NULL;
	struct _mafw_query_closure *mc;

	mc = (struct _mafw_query_closure *) user_data;

	if (error == NULL) {
                mafw_result = g_new0(MafwResult, 1);
                tracker_cache_values_add_results(mc->cache, tracker_result);
                mafw_result->metadata_values =
                        tracker_cache_build_metadata(mc->cache);
                mafw_result->ids = _build_objectids_from_unique_keys(mc->cache);

                /* Invoke callback */
                mc->callback(mafw_result, NULL, mc->user_data);
        } else {
                g_warning("Error while querying: %s\n", error->message);
                mc->callback(NULL, error, mc->user_data);
        }

        tracker_cache_free(mc->cache);
        g_free(mc);
}

static void _do_tracker_get_unique_values(gchar **keys,
                                          gchar *concat_key,
                                          char **filters,
                                          guint offset,
                                          guint count,
                                          struct _mafw_query_closure *mc)
{
        gchar *filter = NULL;
	gchar *count_key;
        gchar *sum_key;

        filter = util_build_complex_rdf_filter(filters, NULL);

#ifndef G_DEBUG_DISABLE
	perf_elapsed_time_checkpoint("Ready to query Tracker");
#endif

	/* Figure out what key we have to count. For example,
	   if we are browsing the artists category, then childcount
	   applies to its children (the #albums of each artist), so
	   the key we have to count are the individual albums of the
	   artist (TRACKER_AKEY_ALBUM) */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_CHILDCOUNT)) {
                if (g_strcmp0 (keys[0], TRACKER_AKEY_GENRE) == 0) {
                        count_key = TRACKER_AKEY_ARTIST;
                } else if (g_strcmp0 (keys[0], TRACKER_AKEY_ARTIST) == 0) {
                        count_key = TRACKER_AKEY_ALBUM;
                } else {
                        count_key = "*";
                }
        } else {
                count_key = NULL;
	}

        /* Check if we have to use sum API */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_DURATION)) {
                sum_key = TRACKER_AKEY_DURATION;
        } else {
                sum_key = NULL;
        }

        tracker_metadata_get_unique_values_with_concat_count_and_sum_async(
                tc,
                SERVICE_MUSIC,
                keys,
                filter,
                concat_key,
                count_key,        /* Count */
                sum_key, /* Sum */
                FALSE,
                offset,
                count,
                _tracker_unique_values_cb,
                mc);

	g_free(filter);
}

static void _tracker_metadata_cb(GPtrArray *results,
                                 GError *error,
                                 gpointer user_data)
{
        GList *metadata_list = NULL;
        struct _mafw_metadata_closure *mc =
                (struct _mafw_metadata_closure *) user_data;

        if (!error) {
                tracker_cache_values_add_results(mc->cache, results);
                metadata_list = tracker_cache_build_metadata(mc->cache);
                mc->mult_callback(metadata_list, NULL, mc->user_data);
                g_list_free(metadata_list);
        } else {
                g_warning("Error while getting metadata: %s\n",
                          error->message);
                mc->mult_callback(NULL, error, mc->user_data);
        }

        tracker_cache_free(mc->cache);
	g_free(mc);
}

static gchar **_uris_to_filenames(gchar **uris)
{
        gchar **filenames;
        gint i;

        filenames = g_new0(gchar *, g_strv_length(uris) + 1);
        for (i = 0; uris[i]; i++) {
                filenames[i] = g_filename_from_uri(uris[i], NULL, NULL);
        }

        return filenames;
}

static void _do_tracker_get_metadata(gchar **uris,
				     gchar **keys,
				     enum TrackerObjectType tracker_obj_type,
				     MafwTrackerMetadatasResultCB callback,
				     gpointer user_data)
{
	gchar **tracker_keys;
	gint service_type;
        struct _mafw_metadata_closure *mc = NULL;
        gchar **user_keys;
        gchar **pathnames;

	/* Figure out tracker service type */
	if (tracker_obj_type == TRACKER_TYPE_VIDEO) {
		service_type = SERVICE_VIDEOS;
	} else if (tracker_obj_type == TRACKER_TYPE_PLAYLIST){
		service_type = SERVICE_PLAYLISTS;
	} else {
		service_type = SERVICE_MUSIC;
	}

        /* Save required information */
        mc = g_new0(struct _mafw_metadata_closure, 1);
        mc->mult_callback = callback;
        mc->user_data = user_data;
        mc->cache = tracker_cache_new(service_type,
                                      TRACKER_CACHE_RESULT_TYPE_GET_METADATA);

        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        user_keys = tracker_cache_keys_get_tracker(mc->cache);

	tracker_keys = keymap_mafw_keys_to_tracker_keys(user_keys,
							service_type);
        g_strfreev(user_keys);

        if (g_strv_length(tracker_keys) > 0) {
                pathnames = _uris_to_filenames(uris);
                tracker_metadata_get_multiple_async(
                        tc,
                        service_type,
                        (const gchar **) pathnames,
                        (const gchar **)tracker_keys,
                        _tracker_metadata_cb,
                        mc);
                g_strfreev(pathnames);
        } else {
                _tracker_metadata_cb(NULL, NULL, mc);
        }
	g_strfreev(tracker_keys);
}

/* ------------------------- Public API ------------------------- */

gchar *ti_create_filter(const MafwFilter *filter)
{
	GString *clause = NULL;
	gchar *ret_str = NULL;

	if (filter == NULL)
		return NULL;

	/* Convert the filter to RDF */
	clause = g_string_new("");
	if (util_mafw_filter_to_rdf(types_map, filter, clause)) {
		ret_str = g_string_free(clause, FALSE);
	} else {
		g_warning("Invalid or unsupported filter syntax");
		g_string_free(clause, TRUE);
	}

	return ret_str;
}

gboolean ti_init(void)
{
	if (info_keys == NULL) {
		info_keys = keymap_get_info_key_table();
	}

	if (types_map == NULL) {
		types_map = keymap_build_tracker_types_map();
	}

	tc = tracker_connect(TRUE);

	if (tc == NULL) {
		g_critical("Could not get a connection to Tracker. "
			   "Plugin disabled.");
	}

	return tc != NULL;
}

void  ti_init_watch (GObject *source)
{
	DBusGConnection *connection;
	DBusGProxy *proxy;
	GError *error = NULL;
	GType collection_type;

	connection = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
	if (connection == NULL) {
		g_warning ("Failed to open connection to bus: %s\n",
			   error->message);
		g_error_free (error);
		return;
	}

	proxy = dbus_g_proxy_new_for_name (connection,
					   "org.freedesktop.Tracker",
					   "/org/freedesktop/Tracker",
					   "org.freedesktop.Tracker");

	collection_type = dbus_g_type_get_collection("GPtrArray",
						      G_TYPE_STRV);
	dbus_g_proxy_add_signal (proxy,
				 "ServiceStatisticsUpdated",
				 collection_type,
				 G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (proxy, "ServiceStatisticsUpdated",
				     G_CALLBACK(_stats_changed_handler),
				     source, NULL);
}

void ti_deinit()
{
	tracker_disconnect(tc);
	tc = NULL;
}

void ti_get_videos(gchar **keys,
		   const gchar *rdf_filter,
		   gchar **sort_fields,
		   gboolean is_descending,
		   guint offset,
		   guint count,
		   MafwTrackerSongsResultCB callback,
		   gpointer user_data)
{
	gchar **tracker_keys;
        gchar **tracker_sort_keys;
	struct _mafw_query_closure *mc;
        gchar *filter;
        gchar **keys_to_query;

        if (rdf_filter) {
                filter = g_strdup_printf(RDF_QUERY_BEGIN
                                         " %s "
                                         RDF_QUERY_END,
                                         rdf_filter);
        } else {
                filter = NULL;
	}

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
	mc->user_data = user_data;
        mc->cache = tracker_cache_new(SERVICE_VIDEOS,
                                      TRACKER_CACHE_RESULT_TYPE_QUERY);

        /* Add requested keys; add also uri, as it will be needed to
         * build object_id list */
        tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, FALSE);
        tracker_cache_key_add_several(mc->cache, keys, TRUE);

	/* Map MAFW keys to Tracker keys */
        keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
	tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
							SERVICE_VIDEOS);
        g_strfreev(keys_to_query);

	if (sort_fields != NULL) {
		tracker_sort_keys =
			keymap_mafw_keys_to_tracker_keys(sort_fields,
							 SERVICE_VIDEOS);
	} else {
		tracker_sort_keys =
			util_create_sort_keys_array(2,
						    TRACKER_VKEY_TITLE,
						    TRACKER_FKEY_FILENAME);
	}

	/* Query tracker */
        tracker_search_query_async(tc, -1,
                                   SERVICE_VIDEOS,
                                   tracker_keys,
                                   NULL,   /* Search text */
                                   NULL,   /* Keywords */
                                   filter,
                                   offset, count,
                                   FALSE,   /* Sort by service */
                                   tracker_sort_keys, /* Sort fields */
                                   is_descending, /* sort descending? */
                                   _tracker_query_cb,
                                   mc);

        if (filter) {
                g_free(filter);
        }

        g_strfreev(tracker_keys);
        g_strfreev(tracker_sort_keys);
}

void ti_get_songs(const gchar *genre,
                  const gchar *artist,
                  const gchar *album,
                  gchar **keys,
                  const gchar *user_filter,
                  gchar **sort_fields,
                  gboolean is_descending,
                  guint offset,
                  guint count,
                  MafwTrackerSongsResultCB callback,
                  gpointer user_data)
{
	gchar **tracker_keys;
        gchar **tracker_sort_keys;
	gchar *rdf_filter = NULL;
        gchar **use_sort_fields;
	struct _mafw_query_closure *mc;
        gchar **keys_to_query = NULL;

        /* Select default sort fields */
        if (!sort_fields) {
                use_sort_fields = g_new0(gchar *, 2);
                if (album) {
                        use_sort_fields[0] = MAFW_METADATA_KEY_TRACK;
                } else {
                        use_sort_fields[0] = MAFW_METADATA_KEY_TITLE;
                }
        } else {
                use_sort_fields = sort_fields;
        }

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
	mc->user_data = user_data;
        mc->cache = tracker_cache_new(SERVICE_MUSIC,
                                      TRACKER_CACHE_RESULT_TYPE_QUERY);

        /* Save known values */
        if (genre) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_GENRE,
                        FALSE,
                        genre);
        }

        if (artist) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_ARTIST,
                        FALSE,
                        artist);
        }

        if (album) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_ALBUM,
                        FALSE,
                        album);
        }

        /* Add URI, as it will likely be needed to build ids */
        tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, FALSE);

        /* Add remaining keys */
        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        /* Get the keys to ask tracker */
        keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
	tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
							SERVICE_MUSIC);
        g_strfreev(keys_to_query);
        tracker_sort_keys = keymap_mafw_keys_to_tracker_keys(use_sort_fields,
							     SERVICE_MUSIC);
        rdf_filter = util_create_filter_from_category(genre, artist,
						      album, user_filter);

	/* Map to tracker keys */
        tracker_search_query_async(tc, -1,
                                   SERVICE_MUSIC,
                                   tracker_keys,
                                   NULL,   /* Search text */
                                   NULL,   /* Keywords */
                                   rdf_filter,
                                   offset, count,
                                   FALSE,   /* Sort by service */
                                   tracker_sort_keys, /* Sort fields */
                                   is_descending, /* sort descending? */
                                   _tracker_query_cb,
                                   mc);

	if (rdf_filter) {
                g_free(rdf_filter);
        }

        if (use_sort_fields != sort_fields) {
                g_free(use_sort_fields);
        }

        g_strfreev(tracker_keys);
        g_strfreev(tracker_sort_keys);
}

void ti_get_artists(const gchar *genre,
                    gchar **keys,
		    const gchar *rdf_filter,
		    gchar **sort_fields,
		    gboolean is_descending,
		    guint offset,
		    guint count,
		    MafwTrackerSongsResultCB callback,
		    gpointer user_data)
{
	struct _mafw_query_closure *mc;
        gchar *escaped_genre = NULL;
        gchar **filters;
        gchar **unique_keys;
        gchar *concat_key;
        gchar *tracker_unique_keys[] = {TRACKER_AKEY_ARTIST, NULL};

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
	mc->user_data = user_data;
        mc->cache =
                tracker_cache_new(SERVICE_MUSIC,
                                  TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        filters = g_new0(gchar *, 3);

        /* If genre, retrieve all artists for that genre */
        if (genre) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_GENRE,
                        FALSE,
                        genre);
                escaped_genre =
                        util_get_tracker_value_for_filter(TRACKER_AKEY_GENRE,
                                                          genre);
                filters[0] = g_strdup_printf(RDF_QUERY_BY_GENRE,
                                             escaped_genre);
                g_free(escaped_genre);
                filters[1] = g_strdup(rdf_filter);
        } else {
                filters[0] = g_strdup(rdf_filter);
        }

        /* Artist will be used as title */
        tracker_cache_key_add_derived(mc->cache,
                                      MAFW_METADATA_KEY_TITLE,
                                      FALSE,
                                      MAFW_METADATA_KEY_ARTIST);

	unique_keys = g_new0(gchar *, 2);
	unique_keys[0] = g_strdup(MAFW_METADATA_KEY_ARTIST);
        tracker_cache_key_add_unique(mc->cache, unique_keys);
        g_strfreev(unique_keys);

        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        /* Concat albums */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ALBUM)) {
                tracker_cache_key_add_concat(mc->cache,
                                             MAFW_METADATA_KEY_ALBUM);
                concat_key = TRACKER_AKEY_ALBUM;
        } else {
                concat_key = NULL;
        }

	_do_tracker_get_unique_values(tracker_unique_keys,
                                      concat_key,
                                      filters,
                                      offset,
                                      count,
                                      mc);
        g_strfreev(filters);
}

void ti_get_genres(gchar **keys,
		   const gchar *rdf_filter,
		   gchar **sort_fields,
		   gboolean is_descending,
		   guint offset,
		   guint count,
		   MafwTrackerSongsResultCB callback,
		   gpointer user_data)
{
	struct _mafw_query_closure *mc;
        gchar **unique_keys;
        gchar **filters;
        gchar *concat_key;
        gchar *tracker_unique_keys[] = {TRACKER_AKEY_GENRE, NULL};

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
	mc->user_data = user_data;
        mc->cache =
                tracker_cache_new(SERVICE_MUSIC,
                                  TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        /* Genre will be used as title */
        tracker_cache_key_add_derived(mc->cache,
                                      MAFW_METADATA_KEY_TITLE,
                                      FALSE,
                                      MAFW_METADATA_KEY_GENRE);

	unique_keys = g_new0(gchar *, 2);
	unique_keys[0] = g_strdup(MAFW_METADATA_KEY_GENRE);
        tracker_cache_key_add_unique(mc->cache, unique_keys);
        g_strfreev(unique_keys);

        /* Add user's keys */
        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        /* Concat artists */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ARTIST)) {
                tracker_cache_key_add_concat(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST);
                concat_key = TRACKER_AKEY_ARTIST;
        } else {
                concat_key = NULL;
        }

        filters = g_new0(gchar *, 2);
        filters[0] = g_strdup (rdf_filter);

	/* Query tracker */
	_do_tracker_get_unique_values(tracker_unique_keys,
                                      concat_key,
                                      filters,
                                      offset,
                                      count,
                                      mc);

        g_strfreev(filters);
}

void ti_get_playlists(gchar **keys,
		      guint offset,
		      guint count,
		      MafwTrackerSongsResultCB callback,
		      gpointer user_data)
{
	struct _mafw_query_closure *mc;
	gchar **tracker_keys;
        gchar **keys_to_query;

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
	mc->user_data = user_data;
        mc->cache = tracker_cache_new(SERVICE_PLAYLISTS,
                                      TRACKER_CACHE_RESULT_TYPE_QUERY);

        /* Add URI, as it will likely be needed to build ids */
        tracker_cache_key_add(mc->cache, MAFW_METADATA_KEY_URI, FALSE);

        tracker_cache_key_add_several(mc->cache, keys, TRUE);

	/* Map to tracker keys */
        keys_to_query = tracker_cache_keys_get_tracker(mc->cache);
	tracker_keys = keymap_mafw_keys_to_tracker_keys(keys_to_query,
							SERVICE_PLAYLISTS);
        g_strfreev(keys_to_query);
	/* Execute query */
        tracker_search_query_async(tc, -1,
                                   SERVICE_PLAYLISTS,
                                   tracker_keys,
                                   NULL,   /* Search text */
                                   NULL,   /* Keywords */
                                   NULL,
                                   offset, count,
                                   FALSE,   /* Sort by service */
                                   NULL, /* Sort fields */
                                   FALSE, /* sort descending? */
                                   _tracker_query_cb,
                                   mc);

        g_strfreev(tracker_keys);
}

void ti_get_albums(const gchar *genre,
                   const gchar *artist,
                   gchar **keys,
                   const gchar *rdf_filter,
                   gchar **sort_fields,
                   gboolean is_descending,
                   guint offset,
                   guint count,
                   MafwTrackerSongsResultCB callback,
                   gpointer user_data)
{
        gchar *escaped_genre;
        gchar *escaped_artist;
        gchar *concat_key;
        gchar **filters;
        gchar **unique_keys;
        gchar *tracker_unique_keys[] = {TRACKER_AKEY_ALBUM, NULL};
        gint i;
        struct _mafw_query_closure *mc;

	/* Prepare mafw closure struct */
	mc = g_new0(struct _mafw_query_closure, 1);
	mc->callback = callback;
 	mc->user_data = user_data;

        mc->cache = tracker_cache_new(SERVICE_MUSIC,
                                      TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        filters = g_new0(gchar *, 4);

	/* If genre and/or artist, retrieve all albums for that genre
         * and/or artist */
        i = 0;
        if (genre) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_GENRE,
                        FALSE,
                        genre);
                escaped_genre =
                        util_get_tracker_value_for_filter(TRACKER_AKEY_GENRE,
                                                          genre);
                filters[i] = g_strdup_printf(RDF_QUERY_BY_GENRE,
                                             escaped_genre);
                g_free(escaped_genre);
                i++;
        }

        if (artist) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_ARTIST,
                        FALSE,
                        artist);
                escaped_artist =
                        util_get_tracker_value_for_filter(TRACKER_AKEY_ARTIST,
                                                          artist);
                filters[i] = g_strdup_printf(RDF_QUERY_BY_ARTIST,
                                             escaped_artist);
                g_free(escaped_artist);
                i++;
        }

        filters[i] = g_strdup(rdf_filter);

        /* Album will be used as title */
        tracker_cache_key_add_derived(mc->cache,
                                      MAFW_METADATA_KEY_TITLE,
                                      FALSE,
                                      MAFW_METADATA_KEY_ALBUM);

        unique_keys = g_new0(gchar *, 2);
        unique_keys[0] = g_strdup(MAFW_METADATA_KEY_ALBUM);
        tracker_cache_key_add_unique(mc->cache, unique_keys);
        g_strfreev(unique_keys);

        /* Add user keys */
        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        /* Concat artists, if requested */
        if (!artist &&
            tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ARTIST)) {
                tracker_cache_key_add_concat(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST);
                concat_key = TRACKER_AKEY_ARTIST;
        } else {
                concat_key = NULL;
        }

        _do_tracker_get_unique_values(tracker_unique_keys,
                                      concat_key,
                                      filters,
                                      offset,
                                      count,
                                      mc);

        g_strfreev(filters);
}

static void _tracker_metadata_from_container_cb(GPtrArray *tracker_result,
                                                GError *error,
                                                gpointer user_data)
{
        GHashTable *metadata;
        struct _mafw_metadata_closure *mc =
                (struct _mafw_metadata_closure *) user_data;

        if (!error) {
                tracker_cache_values_add_results(mc->cache, tracker_result);
                metadata =
                        tracker_cache_build_metadata_aggregated(
                                mc->cache,
                                mc->count_childcount);
                mc->callback(metadata, NULL, mc->user_data);
        } else {
                mc->callback(NULL, error, mc->user_data);
        }

        tracker_cache_free(mc->cache);
        g_free(mc);
}

static void _do_tracker_get_metadata_from_service(
        gchar **keys,
        const gchar *title,
        enum TrackerObjectType tracker_type,
        MafwTrackerMetadataResultCB callback,
        gpointer user_data)
{
        gchar *count_key;
        gchar *sum_key;
        ServiceType service;
        gchar **ukeys;
        gchar **unique_keys;
        struct _mafw_metadata_closure *mc = NULL;

        mc = g_new0(struct _mafw_metadata_closure, 1);
        mc->callback = callback;
        mc->user_data = user_data;
        mc->count_childcount = FALSE;
       	unique_keys = g_new0(gchar *, 2);
	unique_keys[0] = g_strdup("File:Mime");

        if (tracker_type == TRACKER_TYPE_MUSIC) {
                sum_key = TRACKER_AKEY_DURATION;
                service = SERVICE_MUSIC;
        } else if (tracker_type == TRACKER_TYPE_VIDEO) {
                sum_key = TRACKER_VKEY_DURATION;
                service = SERVICE_VIDEOS;
        } else {
                sum_key = TRACKER_PKEY_DURATION;
                service = SERVICE_PLAYLISTS;
        }

        mc->cache =
                tracker_cache_new(service, TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        ukeys = g_new0(gchar *, 2);
        ukeys[0] = g_strdup(MAFW_METADATA_KEY_MIME);
        tracker_cache_key_add_unique(mc->cache, ukeys);
        g_strfreev(ukeys);

        if (title) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_TITLE,
                        FALSE,
                        title);
        }

        tracker_cache_key_add_several(mc->cache, keys, TRUE);

        /* Check if we have to use count API */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_CHILDCOUNT)) {
                count_key = "*";
        } else {
                count_key = NULL;
        }

        /* Check if we have to use sum API */
        if (!tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_DURATION)) {
                sum_key = NULL;
        }

        tracker_metadata_get_unique_values_with_concat_count_and_sum_async(
                tc,
                service,
                unique_keys,
                NULL,
                NULL,
                count_key,
                sum_key,
                FALSE,
                0,
                -1,
                _tracker_metadata_from_container_cb,
                mc);

        g_strfreev(unique_keys);
}

void ti_get_metadata_from_videos(gchar **keys,
                                 const gchar *title,
                                 MafwTrackerMetadataResultCB callback,
                                 gpointer user_data)
{
        _do_tracker_get_metadata_from_service(keys, title, TRACKER_TYPE_VIDEO,
                                              callback, user_data);
}

void ti_get_metadata_from_music(gchar **keys,
                                const gchar *title,
                                MafwTrackerMetadataResultCB callback,
                                gpointer user_data)
{
        _do_tracker_get_metadata_from_service(keys, title, TRACKER_TYPE_MUSIC,
                                              callback, user_data);
}

void ti_get_metadata_from_playlists(gchar **keys,
                                    const gchar *title,
                                    MafwTrackerMetadataResultCB callback,
                                    gpointer user_data)
{
        _do_tracker_get_metadata_from_service(keys, title,
                                              TRACKER_TYPE_PLAYLIST,
                                              callback, user_data);
}

void ti_get_metadata_from_category(const gchar *genre,
                                   const gchar *artist,
                                   const gchar *album,
                                   const gchar *default_count_key,
                                   const gchar *title,
                                   gchar **keys,
                                   MafwTrackerMetadataResultCB callback,
                                   gpointer user_data)
{
        gchar *filter;
        gchar *count_key;
        gchar *concat_key;
        gchar *sum_key;
        struct _mafw_metadata_closure *mc;
        gchar **ukeys;
        gchar **tracker_ukeys;

        mc = g_new0(struct _mafw_metadata_closure, 1);
        mc->callback = callback;
        mc->user_data = user_data;

	/* Create cache */
        mc->cache =
                tracker_cache_new(SERVICE_MUSIC,
                                  TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        /* Preset metadata that we know already */
        if (genre) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_GENRE,
                        FALSE,
                        genre);
        }

        if (artist) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_ARTIST,
                        FALSE,
                        artist);
        }

        if (album) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_ALBUM,
                        FALSE,
                        album);
        }

        /* Select the key that will be used as title */
        if (album) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_TITLE,
                        FALSE,
                        album);
        } else if (artist) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_TITLE,
                        FALSE,
                        artist);
        } else if (genre) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_TITLE,
                        FALSE,
                        genre);
        } else if (title) {
                tracker_cache_key_add_precomputed_string(
                        mc->cache,
                        MAFW_METADATA_KEY_TITLE,
                        FALSE,
                        title);
        }

	/* Select unique keys to use */
        ukeys = g_new0(gchar *, 2);
        if (album) {
                ukeys[0] = g_strdup(MAFW_METADATA_KEY_ALBUM);
        } else if (artist) {
                ukeys[0] = g_strdup(MAFW_METADATA_KEY_ARTIST);
        } else if (genre) {
                ukeys[0] = g_strdup(MAFW_METADATA_KEY_GENRE);
        } else {
                ukeys[0] = g_strdup(default_count_key);
        }

	/* Add required keys to the cache (beware: order is important) */
        tracker_cache_key_add_unique(mc->cache, ukeys);

        tracker_cache_key_add_several(mc->cache, keys, TRUE);

	/* Check if we have to use the concat API */
        if (artist && !album &&
            tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_ALBUM)) {
		/* Concatenate albums if requesting metadata from an artist */
                tracker_cache_key_add_concat(mc->cache,
                                             MAFW_METADATA_KEY_ALBUM);
		concat_key = TRACKER_AKEY_ALBUM;
        } else if (!artist && album &&
                   tracker_cache_key_exists(mc->cache,
                                            MAFW_METADATA_KEY_ARTIST)) {
		/* Concatenate artist if requesting metadata from an album */
                tracker_cache_key_add_concat(mc->cache,
                                             MAFW_METADATA_KEY_ARTIST);
		concat_key = TRACKER_AKEY_ARTIST;
	} else {
                concat_key = NULL;
        }

        /* Check if we have to use count API */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_CHILDCOUNT)) {
                if (album) {
                        count_key = g_strdup("*");
                        mc->count_childcount = FALSE;
                } else if (artist) {
                        count_key = g_strdup(TRACKER_AKEY_ALBUM);
                        mc->count_childcount = FALSE;
                } else if (genre) {
                        count_key = g_strdup(TRACKER_AKEY_ARTIST);
                        mc->count_childcount = FALSE;
                } else {
                        count_key =
                                keymap_mafw_key_to_tracker_key(
                                        default_count_key,
                                        SERVICE_MUSIC);
                        mc->count_childcount = TRUE;
                }
        } else {
                count_key = NULL;
        }

        /* Check if we have to use sum API */
        if (tracker_cache_key_exists(mc->cache, MAFW_METADATA_KEY_DURATION)) {
                sum_key = TRACKER_AKEY_DURATION;
        } else {
                sum_key = NULL;
        }

	/* Compute tracker filter and tracker keys */
        filter = util_create_filter_from_category(genre, artist, album, NULL);

        tracker_ukeys = keymap_mafw_keys_to_tracker_keys(ukeys,
                                                         SERVICE_MUSIC);

        tracker_metadata_get_unique_values_with_concat_count_and_sum_async(
                tc,
                SERVICE_MUSIC,
                tracker_ukeys,
                filter,
                concat_key,
                count_key,
                sum_key,
                FALSE,
                0,
                -1,
                _tracker_metadata_from_container_cb,
                mc);

        g_free(count_key);
        g_free(filter);
        g_strfreev(tracker_ukeys);
        g_strfreev(ukeys);
}


void ti_get_metadata_from_videoclip(gchar **uris,
                                    gchar **keys,
                                    MafwTrackerMetadatasResultCB callback,
                                    gpointer user_data)
{
        _do_tracker_get_metadata(uris, keys, TRACKER_TYPE_VIDEO,
				 callback, user_data);
}

void ti_get_metadata_from_audioclip(gchar **uris,
                                    gchar **keys,
                                    MafwTrackerMetadatasResultCB callback,
                                    gpointer user_data)
{
        _do_tracker_get_metadata(uris, keys, TRACKER_TYPE_MUSIC,
				 callback, user_data);
}

void ti_get_metadata_from_playlist(gchar **uris,
				   gchar **keys,
				   MafwTrackerMetadatasResultCB callback,
				   gpointer user_data)
{
        _do_tracker_get_metadata(uris, keys, TRACKER_TYPE_PLAYLIST,
				 callback, user_data);
}

void ti_get_playlist_entries(GList *pathnames,
			     gchar **keys,
			     MafwTrackerSongsResultCB callback,
			     gpointer user_data,
			     GError **error)
{
	gchar *rdf_filter;
        gint required_size = 0;
        gint *string_sizes;
        gint i;
        GList *current_path;
        gchar *path_list;
        gchar *insert_place;

        /* Build a filter */

        /* Compute the required size for the filter */
        current_path = pathnames;
        string_sizes = g_new0(gint, g_list_length(pathnames));
        i = 0;
        while (current_path != NULL) {
                string_sizes[i] = strlen(current_path->data);
                /* Requires size for the string plus the ',' */
                required_size += string_sizes[i] + 1;
                current_path = g_list_next(current_path);
                i++;
        }

        path_list = g_new0(gchar, required_size);

        /* Copy strings */
        current_path = pathnames;
        insert_place = path_list;
        i = 0;
        while (current_path != NULL) {
                memmove(insert_place, current_path->data, string_sizes[i]);
                insert_place += string_sizes[i];
                /* Put a ',' at the end */
                *insert_place = ',';
                insert_place++;
                /* Move ahead */
                current_path = g_list_next(current_path);
                i++;
        }

        /* Due to the last 'insert_place++', we're placed one position
         * after the end of the big string. Move one position back to
         * insert the '\0' */
        *(insert_place - 1) = '\0';

	rdf_filter = g_strdup_printf (RDF_QUERY_BY_FILE_SET, path_list);
        ti_get_songs(NULL, NULL, NULL,
			      keys,
			      rdf_filter,
			      NULL,
			      FALSE,
			      0, -1,
			      callback, user_data);
	g_free(rdf_filter);
	g_free(path_list);
        g_free(string_sizes);
}

gchar **
ti_set_metadata(const gchar *uri, GHashTable *metadata, gboolean *updated)
{
        GList *keys = NULL;
        GList *running_key;
        gint count_keys;
        gint i_key, u_key;
        gchar **keys_array = NULL;
        gchar **values_array = NULL;;
        gchar **unsupported_array = NULL;
        gchar *mafw_key;
        gchar *pathname;
        GError *error = NULL;
        GValue *value;
        gboolean updatable;
        ServiceType service;

	/* We have not updated anything yet */
	if (updated) {
		*updated = FALSE;
	}

        /* Get list of keys */
        keys = g_hash_table_get_keys(metadata);

        count_keys = g_list_length(keys);
        keys_array = g_new0(gchar *, count_keys+1);
        values_array = g_new0(gchar *, count_keys+1);

        /* Currently, either audio or video
         * keys are changed, but not both at the same time. So assume
         * audio keys unless none of them is found */
        running_key = g_list_first(keys);
        service = SERVICE_VIDEOS;
        while (running_key) {
                mafw_key = (gchar *) running_key->data;
                if (strcmp(mafw_key, MAFW_METADATA_KEY_LAST_PLAYED) == 0 ||
                    strcmp(mafw_key, MAFW_METADATA_KEY_PLAY_COUNT) == 0) {
                        service = SERVICE_MUSIC;
                        break;
                }
		running_key = g_list_next(running_key);
        }

        /* Convert lists to arrays */
        running_key = g_list_first(keys);
        i_key = 0;
        u_key = 0;
        while (running_key) {
                mafw_key = (gchar *) running_key->data;
                updatable = TRUE;
                /* Get only supported keys, and convert values to
                 * strings */
                if (keymap_mafw_key_is_writable(mafw_key)) {
                        /* Special case: last-play should follow
                         * ISO-8601 spec */
                        if (strcmp(mafw_key,
				   MAFW_METADATA_KEY_LAST_PLAYED) == 0) {
                                value = mafw_metadata_first(metadata,
                                                             mafw_key);
                                if (G_VALUE_HOLDS_LONG(value)) {
                                        values_array[i_key] =
					  util_epoch_to_iso8601(
					    g_value_get_long(value));
                                } else {
                                        updatable = FALSE;
                                }
                        } else {
                                value = mafw_metadata_first(metadata,
                                                             mafw_key);
                                switch (G_VALUE_TYPE(value)) {
                                case G_TYPE_STRING:
                                        values_array[i_key] =
                                                g_value_dup_string(value);
                                        break;
                                default:
                                        values_array[i_key] =
                                                g_strdup_value_contents(value);
                                        break;
                                }

                        }
                } else {
                        updatable = FALSE;
                }

                if (updatable) {
                        keys_array[i_key] =
				keymap_mafw_key_to_tracker_key(mafw_key,
							       SERVICE_MUSIC);
                        i_key++;
                } else {
                        if (!unsupported_array) {
                                unsupported_array = g_new0(gchar *,
                                                           count_keys+1);
                        }
                        unsupported_array[u_key] = g_strdup(mafw_key);
                        u_key++;
                }

                running_key = g_list_next(running_key);
        }

        g_list_free(keys);

	/* If there are some updatable keys, call tracker to update them */
	if (keys_array[0] != NULL) {
                pathname = g_filename_from_uri(uri, NULL, NULL);
		tracker_metadata_set(tc, service, pathname,
                                     (const gchar **)keys_array,
                                     values_array, &error);
                g_free(pathname);

		if (error) {
			/* Tracker_metadata_set is an atomic operation; so
			 * none of the keys were updated */
			i_key = 0;
			while (keys_array[i_key]) {
				if (!unsupported_array) {
					unsupported_array =
						g_new0(gchar *, count_keys+1);
				}
				unsupported_array[u_key] =
					g_strdup(keys_array[i_key]);
				i_key++;
				u_key++;
			}

			g_error_free(error);
		} else if (updated) {
			/* We successfully updated some keys at least */
			*updated = TRUE;
		}
	}

        g_strfreev(keys_array);
        g_strfreev(values_array);

        return unsupported_array;
}


static void _set_playlist_duration_cb(GError *error, gpointer user_data)
{
	if (error != NULL) {
		g_warning("Error while setting the playlist duration: "
			  "%s", error->message);
	}
}


void ti_set_playlist_duration(const gchar *uri, guint duration)
{
	gchar **keys_array;
	gchar **values_array;
	gchar *pathname;

	/* Store in Tracker the new value for the playlist duration and set the
	   valid_duration value to TRUE. */
	pathname = g_filename_from_uri(uri, NULL, NULL);

        keys_array = g_new0(gchar *, 3);
	keys_array[0] = g_strdup(TRACKER_PKEY_DURATION);
	keys_array[1] = g_strdup(TRACKER_PKEY_VALID_DURATION);

        values_array = g_new0(gchar *, 3);
	values_array[0] = g_strdup_printf("%d", duration);
	values_array[1] = g_strdup_printf("%d", 1);

	tracker_metadata_set_async(tc,
				   SERVICE_PLAYLISTS,
				   pathname,
				   (const gchar **) keys_array,
				   values_array,
				   _set_playlist_duration_cb,
				   NULL);

	g_free(pathname);
	g_strfreev(keys_array);
        g_strfreev(values_array);
}
