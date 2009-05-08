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

#include <string.h>
#include <totem-pl-parser.h>
#include <gio/gio.h>

#include "mafw-tracker-source.h"
#include "tracker-iface.h"
#include "util.h"
#include "definitions.h"

/* Used to store and emit browse results */
struct _browse_closure {
	/* Source instance */
	MafwSource *source;
	/* The browse request identifier */
	guint browse_id;
	/* The objectid to browse */
	gchar *object_id;
	/* Metadata keys requested */
	gchar **metadata_keys;
	/* Recursive query */
	gboolean recursive;
	/* Sort descending? */
	gboolean is_sort_descending;
        /* Sort fields */
        gchar **sort_fields;
	/* Filter criteria */
	gchar *filter_criteria;
	/* Offset & Count*/
	guint offset;
	guint count;
	/* A list of objectids of browsed items */
	GList *ids;
	/* A list of metadata values for each browsed item */
	GList *metadata_values;
	/* A flag stating if the operation has been cancelled */
	gboolean cancelled;
	/* Objectid prefix to apply to browsed items */
	gchar *object_id_prefix;
	/* When parsing playlists we have to handle offset,count ourselves,
	 this index field let's us know the index of the playlist entry being
	 parsed */
	guint index;
	/* When parsing playlists we have to query metadata for all entries
	   in bulk, so we need to save the URIs */
	GList *pls_uris;
	/* When parsing playlists we cannot expect tracker to provide
	   info about non-local files, so we have to store the ids of those
	   files to ask tracker for */
	GList *pls_local_ids;
	/* Stores the playlist duration calculated exhaustively by MAFW. */
	guint pls_duration;
	/* The user callback used to emit the browse results to the user */
	MafwSourceBrowseResultCb callback;
	/* User data for the user callback  */
	gpointer user_data;

	/* Some extra fields used to emit the results */
	GList *current_id;
	GList *current_metadata_value;
	guint current_index;
	guint remaining_count;
};

typedef gboolean (*_BrowseFunc)(struct _browse_closure *bc,
				GList *child,
				GError **error);
typedef gint (*_GetChildcountFunc)(gpointer data);
typedef gint (*_GetDurationFunc)(gpointer data);

static gboolean _add_playlist_duration_idle(gpointer data);

/*
 * Provides a new browse id
 */
static gint _get_next_browse_id(MafwTrackerSource * self)
{
	MafwTrackerSourceClass *klass = NULL;

	klass = MAFW_TRACKER_SOURCE_GET_CLASS(self);
	g_atomic_int_add(&klass->browse_id_counter, 1);
	return klass->browse_id_counter;
}

static gboolean _emit_browse_results_idle(gpointer data)
{
	struct _browse_closure *bc;
	gchar *current_id;
	GHashTable *current_metadata_value;

	bc = (struct _browse_closure *) data;

	/* Check if browse operation has been cancelled */
	if (bc->cancelled == TRUE) {
		return FALSE;
	}

	/* Otherwise, continue... */
	current_id = (bc->current_id) ?
		bc->current_id->data : NULL;
	current_metadata_value = (bc->current_metadata_value) ?
		bc->current_metadata_value->data : NULL;

	/* Emit one result */
	bc->callback(bc->source,
		     bc->browse_id,
		     bc->remaining_count,
		     bc->current_index,
		     current_id,
		     current_metadata_value,
		     bc->user_data,
		     NULL);

	/* Advance helper pointers to the next item to be emitted */
	bc->current_id = g_list_next(bc->current_id);
	bc->current_metadata_value = g_list_next(bc->current_metadata_value);
	bc->current_index++;
	bc->remaining_count--;

	/* Do we have to emit more results? */
#ifndef G_DEBUG_DISABLE
	if (bc->current_id == NULL) {
		perf_elapsed_time_checkpoint("Results dispatched to UI");
	}
#endif
	return (bc->current_id != NULL);
}

static inline void _register_pending_browse_operation(
	MafwTrackerSource *source,
	struct _browse_closure *bc)
{
        source->priv->pending_browse_ops =
		g_list_prepend (source->priv->pending_browse_ops, bc);
}

static inline void _remove_pending_browse_operation(MafwTrackerSource *source,
						    struct _browse_closure *bc)
{
        source->priv->pending_browse_ops =
		g_list_remove(source->priv->pending_browse_ops, bc);
}

static void
_free_sortfield_list(gchar **sort_fields)
{
        gint i = 0;

        if (!sort_fields)
                return;

        /* Previously +/- char was ignored; add it before relesing the
         * list */
        while(sort_fields[i]) {
                sort_fields[i]--;
                i++;
        }

        g_strfreev(sort_fields);
}

static gboolean
_is_sortfield_descending(gchar **sort_fields)
{
        if ((!sort_fields) || (!sort_fields[0]))
                return FALSE;

        /* As we specify a criteria per each, but we only can use a
         * global criteria to be used in tracker, let's the first
         * field to determine the whole criteria */
        return (*(sort_fields[0]-1)) == '-';
}

static void _browse_closure_free(gpointer data)
{
	struct _browse_closure *bc;
	GList *iter;

	bc = (struct _browse_closure *) data;

	/* Free objectid of browsed item */
	g_free(bc->object_id);

	/* Free list of objectids */
	g_list_foreach(bc->ids, (GFunc) g_free, NULL);
	g_list_free(bc->ids);

	/* For each object, destroy its metadata list */
	for (iter = bc->metadata_values; iter; iter = g_list_next(iter))
		mafw_metadata_release(iter->data);
	g_list_free(bc->metadata_values);

	/* Free pls_(local)_uris field */
	g_list_foreach(bc->pls_uris, (GFunc) g_free, NULL);
	g_list_free(bc->pls_uris);
        g_list_foreach(bc->pls_local_ids, (GFunc) g_free, NULL);
	g_list_free(bc->pls_local_ids);

	/* Free metadata keys */
	g_strfreev(bc->metadata_keys);

        /* Free sort fields */
        _free_sortfield_list(bc->sort_fields);

	/* Free objectid prefix */
	g_free(bc->object_id_prefix);

	/* Free filter_criteria */
	g_free(bc->filter_criteria);

	/* Remove browse closure from pending browse operations */
	_remove_pending_browse_operation(MAFW_TRACKER_SOURCE(bc->source), bc);

	/* Free browse closure structure */
	g_free(bc);
}

static void _emit_browse_results(struct _browse_closure *bc)
{
	gint remaining;

	/* Prepara extra info needed for emission */
	bc->current_id = bc->ids;
	bc->current_metadata_value = bc->metadata_values;
	bc->current_index = 0;
	remaining = g_list_length(bc->ids) - 1;
	if (remaining < 0) {
		bc->remaining_count = 0;
	} else {
		bc->remaining_count = remaining;
	}

#ifndef G_DEBUG_DISABLE
	perf_elapsed_time_checkpoint("Ready to emit");
#endif

	/* Emit results */
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			(GSourceFunc) _emit_browse_results_idle,
			bc,
			(GDestroyNotify) _browse_closure_free);
}

static void _emit_browse_error(struct _browse_closure *bc,
                               GError *error)
{
        GError *mafw_error;

	/* Check if browse operation has been cancelled */
	if (bc->cancelled == FALSE) {
                mafw_error = g_error_new(MAFW_SOURCE_ERROR,
					 MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
					 "%s not found", bc->object_id);
                bc->callback(bc->source,
                             bc->browse_id,
                             0,
                             0,
                             NULL,
                             NULL,
                             bc->user_data,
                             mafw_error);

                g_error_free(mafw_error);
        }
}

static void _add_object_id_prefix_to_list(gchar *object_id_prefix,
                                          GList *list,
                                          gboolean escape)
{
	GList *iter;
	gchar *data;

	iter = list;
	while (iter != NULL) {
		/* Do not add the prefix if this element has already
		   been handled as an URI element */
		if (iter->data && 
		    !g_str_has_prefix ((gchar *) iter->data,
				       MAFW_URI_SOURCE_UUID "::")) {
			if (escape) {
				data = mafw_tracker_source_escape_string(
					iter->data);
			} else {
				data = g_strdup(iter->data);
			}
			g_free(iter->data);
			iter->data =
				g_strconcat(object_id_prefix, "/", data, NULL);
			g_free(data);
		}
		iter = g_list_next(iter);
	}
}

static gchar **
_build_sortfield_list(const gchar *sort_clause)
{
        gchar **sort_fields;
        gint i = 0;

        if (!sort_clause)
                return NULL;

        /* Split the string in tokens */
        sort_fields = g_strsplit(sort_clause, ",", 0);

        /* Every token starts with + or - to indicate an ascending or
         * descending order. Ignore this char */
        while (sort_fields[i]) {
                sort_fields[i]++;
                i++;
        }

        return sort_fields;
}

static void _browse_tracker_cb(MafwResult *clips,
			       GError *error,
			       gpointer user_data)
{
	struct _browse_closure *bc = (struct _browse_closure *) user_data;

	if (error == NULL) {
		/* Convert results to object ids */
		_add_object_id_prefix_to_list(bc->object_id_prefix,
					      clips->ids,
					      TRUE);

		/* Add results to browse closure */
		bc->ids = g_list_concat(bc->ids, clips->ids);
		bc->metadata_values = g_list_concat(bc->metadata_values,
                                                    clips->metadata_values);

		/* Free MafwResult structure (not the info within though) */
		g_free(clips);

		/* Emit results */
		_emit_browse_results(bc);
	} else {
                _emit_browse_error(bc, error);
	}
}

static void _add_playlist_duration_cb(MafwSource *self,
				      guint browse_id,
				      gint remaining_count,
				      guint index,
				      const gchar *object_id,
				      GHashTable *metadata,
				      gpointer user_data,
				      const GError *error)
{
	GValue *gval;

	struct _browse_closure *playlists_bc =
		(struct _browse_closure *) user_data;

	/* Add the calculated playlist duration to the results. */
	if (!error && metadata) {
		gval = mafw_metadata_first(
			metadata,
			MAFW_METADATA_KEY_DURATION);

		if (gval) {
			mafw_metadata_add_int(
				playlists_bc->current_metadata_value->data,
				MAFW_METADATA_KEY_DURATION,
				g_value_get_int(gval));
		}

	}

	/* Advance helpers to the next playlist. */
	playlists_bc->current_id =
		g_list_next(playlists_bc->current_id);
	playlists_bc->current_metadata_value =
		g_list_next(playlists_bc->current_metadata_value);


	/* Continue adding durations to the next playlists. */
	g_idle_add(_add_playlist_duration_idle, playlists_bc);

	/* Frees. */
	mafw_metadata_release(metadata);
}

static gboolean _add_playlist_duration_idle(gpointer data)
{
	gchar *pls_id;
	GHashTable *pls_metadata;

	struct _browse_closure *playlists_bc = (struct _browse_closure *) data;

	/* Get the id and the metadata of the current playlist from the
	   playlists browse results. */
	pls_id = (playlists_bc->current_id) ?
		playlists_bc->current_id->data : NULL;
	pls_metadata = (playlists_bc->current_metadata_value) ?
		playlists_bc->current_metadata_value->data: NULL;

	/* Calculate the duration of the current playlist and add it to the
	   results if needed. */
	if (util_calculate_playlist_duration_is_needed(pls_metadata)) {
		mafw_tracker_source_get_playlist_duration(
			playlists_bc->source,
			pls_id,
			_add_playlist_duration_cb,
			data);

		return FALSE;
	}

	/* Remove the non-Mafw data used to check if MAFW has to calculate
	 the playlist duration. */
	util_remove_tracker_data_to_check_pls_duration(pls_metadata,
		playlists_bc->metadata_keys);

	/* If not, the duration contains the correct value. Continue with the
	   next playlist. */
	playlists_bc->current_id = g_list_next(playlists_bc->current_id);
	playlists_bc->current_metadata_value =
		g_list_next(playlists_bc->current_metadata_value);

	/* If there are no more playlists, emit the results. */
	if (playlists_bc->current_id == NULL) {
		_emit_browse_results(playlists_bc);

		return FALSE;
	}

	return TRUE;
}

static void _browse_playlists_tracker_cb(MafwResult *clips,
					 GError *error,
					 gpointer user_data)
{
	struct _browse_closure *playlists_bc =
		(struct _browse_closure *) user_data;

	if (!error) {
		/* Convert the results to object ids */
		_add_object_id_prefix_to_list(playlists_bc->object_id_prefix,
					      clips->ids,
					      TRUE);

		/* Add the results to the browse closure */
		playlists_bc->ids = g_list_concat(playlists_bc->ids,
						  clips->ids);
		playlists_bc->metadata_values =
			g_list_concat(playlists_bc->metadata_values,
				      clips->metadata_values);

		/* Check if it's possible that we have to recalculate the
		   durations of some playlists. */
		if (util_is_duration_requested(
			    (const gchar **) playlists_bc->metadata_keys)) {
			/* Maybe we have to recalculate some of them.
			   Set the initial playlist to process in an idle. */
			playlists_bc->current_id =
				playlists_bc->ids;
			playlists_bc->current_metadata_value =
				playlists_bc->metadata_values;

			/* Start recalculating the durations not returned by
			   tracker and storing them in the results. */
			g_idle_add(_add_playlist_duration_idle,
				   playlists_bc);
		} else {
			/* We don't have to recalculate nothing.
			   So, emit the results as usual.*/
			_emit_browse_results(playlists_bc);
		}

		g_free(clips);
	} else {
                _emit_browse_error(playlists_bc, error);
	}
}

static void _consolidate_metadata_cb(MafwSource *source,
                                     const gchar *object_id,
                                     GHashTable *metadata,
                                     gpointer user_data,
                                     const GError *error)
{
        struct _browse_closure *bc = (struct _browse_closure *) user_data;

        bc->remaining_count--;

        /* Add result */
        if (!error) {
                bc->ids = g_list_prepend(bc->ids,
                                         g_strdup(object_id));
                g_hash_table_ref(metadata);
                bc->metadata_values = g_list_prepend(bc->metadata_values,
                                                     metadata);
        }

        /* If there aren't more callbacks pending, emit the results */
        if (bc->remaining_count == 0) {
                _emit_browse_results(bc);
        }
}

static gchar *_build_object_id(const gchar *item1, ...)
{
	va_list args;
	gchar *next_item;
        gchar *escaped;
	GString *result;

	/* Add uuid::item1 to items */
	result = g_string_new(MAFW_TRACKER_SOURCE_UUID "::");
	escaped = mafw_tracker_source_escape_string(item1);
	result = g_string_append(result, escaped);
        g_free(escaped);

	/* Add remaining items */
	va_start(args, item1);
	while ((next_item = va_arg(args, gchar *)) != NULL) {
		result = g_string_append_c(result, '/');
		escaped = mafw_tracker_source_escape_string(next_item);
		result = g_string_append(result, escaped);
		g_free(escaped);
	}
	va_end(args);

	return g_string_free(result, FALSE);
}

static void _browse_enqueue_videos_cb(MafwResult *clips,
                                      GError *error,
                                      gpointer user_data)
{
	struct _browse_closure *bc = (struct _browse_closure *) user_data;

	if (error == NULL) {
		/* Convert results to object ids */
		_add_object_id_prefix_to_list(bc->object_id_prefix,
					      clips->ids,
					      TRUE);

		/* Add results to browse closure */
		bc->ids = g_list_concat(bc->ids, clips->ids);
		bc->metadata_values = g_list_concat(bc->metadata_values,
                                                    clips->metadata_values);

                /* Count how many videos should be searched */
                bc->count -= g_list_length(clips->ids);

		/* Free MafwResult structure (not the info within though) */
		g_free(clips);


		if (bc->count == 0) {
                        _emit_browse_results(bc);
                        return;
                }
        }

        /* Browsing /videos category */
        bc->object_id_prefix = _build_object_id(TRACKER_SOURCE_VIDEOS,
                                                NULL);

        ti_get_videos(bc->metadata_keys,
		      bc->filter_criteria,
		      bc->sort_fields,
		      bc->is_sort_descending,
		      bc->offset,
		      bc->count,
		      _browse_tracker_cb,
		      bc);
}

static GHashTable *_new_metadata_from_untracked_resource(gchar *uri,
                                                         gchar **keys)
{
	GHashTable *metadata;
	gint i=0;

	metadata = mafw_metadata_new();

	for (i=0; keys[i] != NULL; i++) {
		if (!strcmp(keys[i], MAFW_METADATA_KEY_TITLE)) {
			gchar *unescaped_title, *temp;
			temp = g_path_get_basename(uri);
			unescaped_title = g_uri_unescape_string(temp, NULL);
			mafw_metadata_add_str(metadata,
						keys[i],
						unescaped_title);
			g_free(unescaped_title);
			g_free(temp);
		} else if (!strcmp(keys[i], MAFW_METADATA_KEY_CHILDCOUNT)) {
			mafw_metadata_add_int(metadata, keys[i], 0);
		} else if (!strcmp(keys[i], MAFW_METADATA_KEY_URI)) {
			gchar *fixed_uri;
			if (uri[0] == '/') {
				fixed_uri = g_filename_from_uri(uri,
                                                                NULL,
                                                                NULL);
			} else {
				fixed_uri = g_strdup(uri);
			}
			mafw_metadata_add_str(metadata, keys[i], fixed_uri);
			g_free(fixed_uri);
		}
	}

	return metadata;
}

static void _construct_playlist_entries_result(struct _browse_closure * bc,
					       GHashTable *tracker_metadatas,
					       MafwResult *clips)
{
	GList *iter;
	GHashTable *metadata;
	gchar *clip;
	gchar *pathname;
	gchar *escape_pathname;
	gchar *objectid;
        gchar *uri;

	iter = bc->pls_uris;
	while (iter != NULL) {
                uri= (gchar *) iter->data;
		if (uri == NULL) {
			iter = g_list_next(iter);
			continue;
		}

                if (g_str_has_prefix(uri, "file://")) {
			/* Construct the objectid for the local file */
                        pathname = g_filename_from_uri(uri, NULL, NULL);
			escape_pathname =
				mafw_tracker_source_escape_string(pathname);
			objectid = g_strconcat(bc->object_id_prefix, "/",
					      escape_pathname,
					      NULL);
			g_free(pathname);
			g_free(escape_pathname);
		} else {
                        objectid = NULL;
                }

		if (objectid &&
                    (tracker_metadatas != NULL) &&
                    (metadata = g_hash_table_lookup(tracker_metadatas,
						    objectid))) {
			/* The clip is in tracker results. Add tracker
                           metadata. */
                        clip = g_strdup(objectid);
			g_hash_table_ref(metadata);

                        clips->ids = g_list_prepend(clips->ids, clip);
                        clips->metadata_values =
                                g_list_prepend(clips->metadata_values,
                                               metadata);
                } else {
                        /* The clip non-local or missing in tracker
                           results. Add untracked metadata. */
                        clip = 	mafw_source_create_objectid(uri);
                        metadata =
                                _new_metadata_from_untracked_resource(
                                        uri, bc->metadata_keys);

                        clips->ids = g_list_prepend(clips->ids, clip);
                        clips->metadata_values =
                                g_list_prepend(clips->metadata_values,
                                               metadata);
                }
		iter = g_list_next(iter);
		g_free(objectid);
	}
}

static void
_pls_entry_parsed (TotemPlParser *parser,
		   const gchar *uri,
		   GHashTable *metadata,
		   gpointer user_data)
{
	struct _browse_closure *bc = (struct _browse_closure *) user_data;
	gchar *unescaped_uri;
        gchar *escaped_uri;
        gchar *filename;

	if (uri == NULL) {
		return;
	}

	if (bc->index >= bc->offset && bc->index < bc->offset + bc->count) {
                /* Make sure it is an uri and also escaped */
		unescaped_uri = util_unescape_string(uri);
                if (unescaped_uri[0] == '/') {
                        escaped_uri = g_filename_to_uri(unescaped_uri,
                                                        NULL,
                                                        NULL);
                } else {
                        escaped_uri =
                                g_uri_escape_string(
                                        unescaped_uri,
                                        G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
                                        TRUE);
                }
                g_free(unescaped_uri);

		/* Check if the URI is local (tracker can resolve the
		   metadata) or not  */
                if (g_str_has_prefix(escaped_uri, "file://")) {
                        filename = g_filename_from_uri(escaped_uri, NULL, NULL);
                        if (filename) {
                                bc->pls_local_ids =
                                        g_list_prepend(bc->pls_local_ids,
                                                       filename);
                        }
                }
		bc->pls_uris = g_list_prepend(bc->pls_uris, escaped_uri);
	}
	bc->index++;
}

static void _browse_playlist_tracker_cb(MafwSource *self,
					GHashTable *tracker_metadatas,
					gpointer user_data,
					const GError *error)
{
	struct _browse_closure *bc = (struct _browse_closure *) user_data;

	MafwResult *clips;

	/* We may get an error if none of the elements in the playlist exists
	   or they are stored in untracked directories, so let's just
	   print a warning here */
	if (error != NULL) {
		g_warning ("Got error from tracker when getting metadata for " \
			   "local references in a playlist file: %s",
                           error->message);
	}

	clips = g_new0(MafwResult, 1);

	_construct_playlist_entries_result(bc, tracker_metadatas, clips);

	/* Add results to browse closure */
	bc->ids = clips->ids;
	bc->metadata_values = clips->metadata_values;

	/* Free MafwResult structure (not the info within though) */
	g_free(clips);

	/* Emit results */
	_emit_browse_results(bc);
}

static gboolean _get_playlist_entries(const gchar *pls_uri,
				      struct _browse_closure *bc,
				      GError **error)
{
	TotemPlParser *pl;
	gboolean result;

	/* Get entries */
	pl = totem_pl_parser_new ();
	g_object_set (pl, "recurse", FALSE, "disable-unsafe", TRUE, NULL);
	g_signal_connect (G_OBJECT (pl), "entry-parsed",
			  G_CALLBACK (_pls_entry_parsed), bc);

	if (totem_pl_parser_parse (pl, pls_uri, FALSE) !=
	    TOTEM_PL_PARSER_RESULT_SUCCESS) {
		g_warning (" Failed to parse playlist: %s", pls_uri);
		result = FALSE;
                if (error) {
                        *error = g_error_new(
				MAFW_SOURCE_ERROR,
				MAFW_SOURCE_ERROR_PLAYLIST_PARSING_FAILED,
				"%s does not exist or is not a valid playlist",
				bc->object_id);
		}
	} else {
		if (bc->pls_local_ids != NULL) {
			gchar **local_objectids;

                        /* Reverse the list */
                        bc->pls_local_ids =
                                g_list_reverse(bc->pls_local_ids);

			/* Construct the objectids */
			_add_object_id_prefix_to_list(bc->object_id_prefix,
						      bc->pls_local_ids,
						      TRUE);
			local_objectids = util_list_to_strv(bc->pls_local_ids);

			/* Do we have local references in the playlist? If so,
			   try to resolve metadata for them using Tracker */
			mafw_tracker_source_get_metadatas(
				bc->source,
				(const gchar **) local_objectids,
				(const gchar *const *) bc->metadata_keys,
				_browse_playlist_tracker_cb,
				bc);

			g_free(local_objectids);
		} else if (bc->pls_uris != NULL) {
                        /* Reverse the list */
                        bc->pls_uris = g_list_reverse(bc->pls_uris);
			/* We do not have local references, but we have
			   external references at least: simulate an empty
			   tracker result */
			_browse_playlist_tracker_cb(bc->source,
						    NULL,
						    bc,
						    NULL);
		} else {
			/* The playlist is empty! */
			_browse_playlist_tracker_cb(bc->source,
						    NULL,
						    bc,
						    NULL);
		}
		result = TRUE;
	}
	g_object_unref(pl);

	return result;
}

static void _send_error(MafwSource *source,
                        MafwSourceBrowseResultCb browse_cb,
                        gint code,
                        gchar *description,
                        gpointer user_data)
{
        GError *error = NULL;

        g_set_error(&error,
                    MAFW_SOURCE_ERROR,
                    code,
                    "%s",description);
        browse_cb(source, MAFW_SOURCE_INVALID_BROWSE_ID, 0, 0, NULL, NULL,
                  user_data, error);
        g_error_free(error);
}

static void _browse_songs_branch(const gchar *genre,
                                 const gchar *artist,
                                 const gchar *album,
                                 struct _browse_closure *bc)
{
        /* Browsing /music/songs */
        bc->object_id_prefix = _build_object_id(TRACKER_SOURCE_MUSIC,
                                                TRACKER_SOURCE_SONGS,
                                                NULL);
        ti_get_songs(genre, artist, album,
                     bc->metadata_keys,
                     bc->filter_criteria,
                     bc->sort_fields,
                     bc->is_sort_descending,
                     bc->offset,
                     bc->count,
                     _browse_tracker_cb,
                     bc);
}

static void _browse_albums_branch(const gchar *album,
                                  struct _browse_closure *bc)
{
        gchar *escaped_album;

        if (bc->recursive) {
                _browse_songs_branch(NULL, NULL, album, bc);
        } else {
                if (album) {
                        /* Browsing /music/albums/<album from artist> */
                        escaped_album =
                                mafw_tracker_source_escape_string(album);
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_ALBUMS,
                                                 escaped_album, NULL);
                        g_free(escaped_album);
                        ti_get_songs(NULL, NULL, album,
                                     bc->metadata_keys,
                                     bc->filter_criteria,
                                     bc->sort_fields,
                                     bc->is_sort_descending,
                                     bc->offset,
                                     bc->count,
                                     _browse_tracker_cb,
                                     bc);
                } else {
                        /* Browsing /music/albums */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_ALBUMS,
                                                 NULL);
                        ti_get_albums(NULL, NULL,
                                      bc->metadata_keys,
                                      bc->filter_criteria,
                                      bc->sort_fields,
                                      bc->is_sort_descending,
                                      bc->offset,
                                      bc->count,
                                      _browse_tracker_cb,
                                      bc);
                }
        }
}

static void _browse_artists_branch(const gchar *artist,
                                   const gchar *album,
                                   struct _browse_closure *bc)
{
        if (bc->recursive) {
                _browse_songs_branch(NULL, artist, album, bc);
        } else {
                if (album) {
                        /* Browsing /music/artists/<artists>/<album> */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_ARTISTS,
                                                 artist, album, NULL);
                        ti_get_songs(NULL, artist, album,
                                     bc->metadata_keys,
                                     bc->filter_criteria,
                                     bc->sort_fields,
                                     bc->is_sort_descending,
                                     bc->offset,
                                     bc->count,
                                     _browse_tracker_cb,
                                     bc);
                } else if (artist) {
                        /* Browsing /music/artists/<artist> */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_ARTISTS,
                                                 artist, NULL);
                        ti_get_albums(NULL, artist,
                                      bc->metadata_keys,
                                      bc->filter_criteria,
                                      bc->sort_fields,
                                      bc->is_sort_descending,
                                      bc->offset,
                                      bc->count,
                                      _browse_tracker_cb,
                                      bc);
                } else {
                        /* Browsing /music/artists */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_ARTISTS,
                                                 NULL);
                        ti_get_artists(NULL,
                                       bc->metadata_keys,
                                       bc->filter_criteria,
                                       bc->sort_fields,
                                       bc->is_sort_descending,
                                       bc->offset,
                                       bc->count,
                                       _browse_tracker_cb,
                                       bc);
                }
        }
}

static void _browse_genres_branch(const gchar *genre,
                                  const gchar *artist,
                                  const gchar *album,
                                  struct _browse_closure *bc)
{
        if (bc->recursive) {
                _browse_songs_branch(genre, artist, album, bc);
        } else {
                if (album) {
                        /* Browsing /music/genres/<genre>/<artists>/<album> */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_GENRES,
                                                 genre, artist, album, NULL);
                        ti_get_songs(genre, artist, album,
                                     bc->metadata_keys,
                                     bc->filter_criteria,
                                     bc->sort_fields,
                                     bc->is_sort_descending,
                                     bc->offset,
                                     bc->count,
                                     _browse_tracker_cb,
                                     bc);
                } else if (artist) {
                        /* Browsing /music/genres/<genre>/<artist> */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_GENRES,
                                                 genre, artist, NULL);
                        ti_get_albums(genre, artist,
                                      bc->metadata_keys,
                                      bc->filter_criteria,
                                      bc->sort_fields,
                                      bc->is_sort_descending,
                                      bc->offset,
                                      bc->count,
                                      _browse_tracker_cb,
                                      bc);
                } else if (genre) {
                        /* Browsing /music/genres/<genre> */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_GENRES,
                                                 genre, NULL);
                        ti_get_artists(genre,
                                       bc->metadata_keys,
                                       bc->filter_criteria,
                                       bc->sort_fields,
                                       bc->is_sort_descending,
                                       bc->offset,
                                       bc->count,
                                       _browse_tracker_cb,
                                       bc);
                } else {
                        /* Browsing /music/genres */
                        bc->object_id_prefix =
                                _build_object_id(TRACKER_SOURCE_MUSIC,
                                                 TRACKER_SOURCE_GENRES,
                                                 NULL);
                        ti_get_genres(bc->metadata_keys,
                                      bc->filter_criteria,
                                      bc->sort_fields,
                                      bc->is_sort_descending,
                                      bc->offset,
                                      bc->count,
                                      _browse_tracker_cb,
                                      bc);
                }
        }
}

static void _browse_root(struct _browse_closure *bc)
{
        gchar *object_id;

        if (bc->recursive) {
                bc->object_id_prefix = _build_object_id(TRACKER_SOURCE_MUSIC,
                                                        TRACKER_SOURCE_SONGS,
                                                        NULL);
                ti_get_songs(NULL, NULL, NULL,
                             bc->metadata_keys,
                             bc->filter_criteria,
                             bc->sort_fields,
                             bc->is_sort_descending,
                             bc->offset,
                             bc->count,
                             _browse_enqueue_videos_cb,
                             bc);
        } else {
                /* Browsing / */

                /* Obtain metadata from root category */

                /* Save how many callbacks we need to received before
                 * sending results */
                bc->remaining_count = 2;

                /* Get metadata for videos */
                object_id = _build_object_id(TRACKER_SOURCE_VIDEOS, NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);

                /* Get metadata for music */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC, NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);
        }
}

static void _browse_videos_branch(struct _browse_closure *bc)
{
        /* Browsing /videos */
        bc->object_id_prefix = _build_object_id(TRACKER_SOURCE_VIDEOS,
                                                NULL);
        ti_get_videos(bc->metadata_keys,
                      bc->filter_criteria,
                      bc->sort_fields,
                      bc->is_sort_descending,
                      bc->offset,
                      bc->count,
                      _browse_tracker_cb,
                      bc);
}

static void _browse_music_branch(struct _browse_closure *bc)
{
        gchar *object_id = NULL;

        /* Browsing /music */
        if (bc->recursive) {
                _browse_songs_branch(NULL, NULL, NULL, bc);
        } else {
                /* Save how many callbacks we need to received before
                 * sending results */
                bc->remaining_count = 5;

                /* Get metadata for albums */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC,
                                             TRACKER_SOURCE_ALBUMS,
                                             NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);

                /* Get metadata for artists */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC,
                                             TRACKER_SOURCE_ARTISTS,
                                             NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);

                /* Get metadata for genres */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC,
                                             TRACKER_SOURCE_GENRES,
                                             NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);

                /* Get metadata for songs */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC,
                                             TRACKER_SOURCE_SONGS,
                                             NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);

                /* Get metadata for playlists */
                object_id = _build_object_id(TRACKER_SOURCE_MUSIC,
                                             TRACKER_SOURCE_PLAYLISTS,
                                             NULL);
                mafw_tracker_source_get_metadata(
                        bc->source,
                        object_id,
                        (const gchar *const *) bc->metadata_keys,
                        _consolidate_metadata_cb,
                        bc);
                g_free(object_id);
        }
}

static gboolean _browse_playlists_branch(const gchar *playlist,
                                         struct _browse_closure *bc)
{
        GError *error = NULL;

        if (playlist) {
                /* Browsing /music/playlists/<playlist> */
                bc->object_id_prefix = _build_object_id(TRACKER_SOURCE_MUSIC,
                                                        TRACKER_SOURCE_SONGS,
                                                        NULL);
                if (_get_playlist_entries(playlist, bc, &error)) {
                        return TRUE;
                } else {
                        _send_error(bc->source, bc->callback,
                                    error->code,
                                    error->message,
                                    bc->user_data);
			g_error_free(error);

                        return FALSE;
                }
        } else {
                /* Browsing /music/playlists */
                bc->object_id_prefix =
                        _build_object_id(TRACKER_SOURCE_MUSIC,
                                         TRACKER_SOURCE_PLAYLISTS,
                                         NULL);

		if (util_is_duration_requested(
			    (const gchar **) bc->metadata_keys)) {
			bc->metadata_keys =
				util_add_tracker_data_to_check_pls_duration(
					bc->metadata_keys);
		}

		ti_get_playlists(bc->metadata_keys,
				 bc->offset,
				 bc->count,
				 _browse_playlists_tracker_cb,
				 bc);

                return TRUE;
        }
}

guint
mafw_tracker_source_browse(MafwSource *self,
                           const gchar *object_id,
                           gboolean recursive,
                           const MafwFilter *filter,
                           const gchar *sort_criteria,
                           const gchar *const *metadata_keys,
                           guint skip_count, guint item_count,
                           MafwSourceBrowseResultCb browse_cb,
                           gpointer user_data)
{
	gint browse_id = 0;
	struct _browse_closure *bc = NULL;
        CategoryType category;
        const gchar* const* meta_keys;
        gchar *album = NULL;
        gchar *artist = NULL;
        gchar *clip = NULL;
        gchar *genre = NULL;

	/* Handle preconditions */
	g_return_val_if_fail(MAFW_IS_TRACKER_SOURCE(self), 0);

	if (!object_id) {
                _send_error(self, browse_cb,
                            MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                            "No object id was specified",
                            user_data);
		return MAFW_SOURCE_INVALID_BROWSE_ID;
        }

        category = util_extract_category_info(object_id, &genre, &artist,
                                              &album, &clip);

        if (category == CATEGORY_ERROR) {
                _send_error(self, browse_cb,
                            MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                            "Invalid object id",
                            user_data);
                return MAFW_SOURCE_INVALID_BROWSE_ID;
        }

        if ((clip != NULL) && (category != CATEGORY_MUSIC_PLAYLISTS)){
                _send_error(self, browse_cb,
                            MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                            "object id is not browseable",
                            user_data);
                return MAFW_SOURCE_INVALID_BROWSE_ID;
        }

	if (metadata_keys == NULL) {
		metadata_keys = MAFW_SOURCE_NO_KEYS;
	}

	if (mafw_source_all_keys(metadata_keys)) {
		meta_keys = MAFW_SOURCE_LIST(KNOWN_METADATA_KEYS);
	} else {
		meta_keys = metadata_keys;
	}

	if (item_count == MAFW_SOURCE_BROWSE_ALL) {
		item_count = G_MAXINT;
        }

	/* Obtain browse id for this request */
	browse_id = _get_next_browse_id(MAFW_TRACKER_SOURCE(self));

	/* Prepare browse operation */
	bc = g_new0(struct _browse_closure, 1);
	bc->source = self;
	bc->object_id = g_strdup(object_id);
	bc->metadata_keys = g_strdupv((gchar **) meta_keys);
	bc->sort_fields = _build_sortfield_list(sort_criteria);
	bc->is_sort_descending = _is_sortfield_descending(bc->sort_fields);
	bc->filter_criteria = ti_create_filter(filter);
	bc->offset = skip_count;
	bc->count = item_count;
	bc->recursive = recursive;
	bc->callback = browse_cb;
	bc->user_data = user_data;
	bc->browse_id = browse_id;

        #ifndef G_DEBUG_DISABLE
        perf_elapsed_time_checkpoint("Start time");
        #endif

	/* Keep a reference to this pending operation,
	   so we can cancel it */
	_register_pending_browse_operation(MAFW_TRACKER_SOURCE(self),
					   bc);

        switch (category) {
        case CATEGORY_ROOT:
                _browse_root(bc);
                break;

        case CATEGORY_VIDEO:
                _browse_videos_branch(bc);
                break;

        case CATEGORY_MUSIC:
                _browse_music_branch(bc);
                break;

        case CATEGORY_MUSIC_SONGS:
                _browse_songs_branch(NULL, NULL, NULL, bc);
                break;

        case CATEGORY_MUSIC_ALBUMS:
                _browse_albums_branch(album, bc);
                break;

        case CATEGORY_MUSIC_ARTISTS:
                _browse_artists_branch(artist, album, bc);
                break;

        case CATEGORY_MUSIC_GENRES:
                _browse_genres_branch(genre, artist, album, bc);
                break;

        case CATEGORY_MUSIC_PLAYLISTS:
                if (!_browse_playlists_branch(clip, bc)) {
                        return MAFW_SOURCE_INVALID_BROWSE_ID;
                }
                break;

        default:
		/* CATEGORY_ERROR was already considered above */
                break;
        }

        return browse_id;
}

gboolean
mafw_tracker_source_cancel_browse(MafwSource *self,
                                   guint browse_id,
                                   GError **error)
{
        GList *iter;
        GList *pending_browse_ops;
        struct _browse_closure *bc;

	g_return_val_if_fail(MAFW_IS_TRACKER_SOURCE(self), FALSE);

	/* Find browse operation and cancel it */
	pending_browse_ops =
		MAFW_TRACKER_SOURCE(self)->priv->pending_browse_ops;
        for (iter = pending_browse_ops; iter != NULL;
	     iter = g_list_next(iter)) {
		bc = (struct _browse_closure *) iter->data;
		if (bc->browse_id == browse_id) {
			bc->cancelled = TRUE;
			return TRUE;
		}
	}

	/* If we reach this point it means we did not find a
	   matching operation */
        if (error) {
                *error = g_error_new(MAFW_SOURCE_ERROR,
                                     MAFW_SOURCE_ERROR_INVALID_BROWSE_ID,
                                     "Browse id %u does not exist", browse_id);
	}

        return FALSE;
}

static void _get_playlist_duration_cb(MafwSource *self,
				      guint browse_id,
				      gint remaining_count,
				      guint index,
				      const gchar *object_id,
				      GHashTable *metadata,
				      gpointer user_data,
				      const GError *error)
{
	GValue *gval = NULL;
	struct _browse_closure *duration_bc =
		(struct _browse_closure *) user_data;

	/* Add the duration of the playlist item to the playlist duration. */
	if (!error && metadata) {
		gval =  mafw_metadata_first(metadata,
					    MAFW_METADATA_KEY_DURATION);
		if (gval) {
			duration_bc->pls_duration =
				duration_bc->pls_duration +
				g_value_get_int(gval);
		}
	}

	if (remaining_count == 0) {
		/* The calculation of the playlist duration has finished.
		   Now "pls_duration" contains the final value. */
		GHashTable *duration_metadata = NULL;
		gchar *pls_uri = NULL;

		util_extract_category_info(duration_bc->object_id,
					   NULL,
					   NULL,
					   NULL,
					   &pls_uri);

		/* Store the new duration in Tracker. */
		if (pls_uri) {
			/* It's a playlist, not the playlists category. */
			ti_set_playlist_duration(pls_uri,
						 duration_bc->pls_duration);
			g_free(pls_uri);
		}

		/* Create the result adding the duration. */
		if (duration_bc->pls_duration > 0) {
			duration_metadata = mafw_metadata_new();
			mafw_metadata_add_int(
				duration_metadata,
				MAFW_METADATA_KEY_DURATION,
				duration_bc->pls_duration);
		}

		/* Call to the callback to return the calculated duration. */
		duration_bc->callback(self,
				      browse_id,
				      0,
				      0,
				      g_strdup(duration_bc->object_id),
				      duration_metadata,
				      duration_bc->user_data,
				      error);

		/* Frees. */
		g_free(duration_bc->object_id);
		g_free(duration_bc);
	}
}

void mafw_tracker_source_get_playlist_duration(MafwSource *self,
					       const gchar *object_id,
					       MafwSourceBrowseResultCb callback,
					       gpointer user_data)
{
	/* Calculate exhaustively the playlist or playlists category
	   durations. */

	/* Prepare browse operation. */
	gchar **keys =  g_strdupv((gchar **) MAFW_SOURCE_LIST(
					  MAFW_METADATA_KEY_DURATION));

	struct _browse_closure *pls_duration_bc =
		g_new0(struct _browse_closure, 1);

	pls_duration_bc->object_id = g_strdup(object_id);
	pls_duration_bc->pls_duration = 0;
	pls_duration_bc->callback = callback;
	pls_duration_bc->user_data = user_data;

	/* Browse */
	mafw_tracker_source_browse(self,
				   object_id,
				   FALSE,
				   NULL,
				   "",
				   (const gchar * const *) keys,
				   0,
				   MAFW_SOURCE_BROWSE_ALL,
				   _get_playlist_duration_cb,
				   (gpointer) pls_duration_bc);

	g_strfreev(keys);
}
