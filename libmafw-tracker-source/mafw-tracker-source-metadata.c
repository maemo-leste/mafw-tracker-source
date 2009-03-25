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

#include "mafw-tracker-source.h"
#include "tracker-iface.h"
#include "util.h"
#include "definitions.h"

struct _metadata_closure {
	/* Source instance */
	MafwSource *source;
	/* The objectid to get metadata from */
	gchar *object_id;
	/* Metadata keys requested */
	gchar **metadata_keys;
	/* Metadata values obtained */
	GHashTable *metadata_values;
	/* The user callback used to emit the browse results to the user */
	MafwSourceMetadataResultCb callback;
	/* User data for the user callback  */
	gpointer user_data;
};

struct _update_metadata_closure {
        /* Source instance */
        MafwSource *source;
        /* The objectid to be updated */
        gchar *object_id;
        /* Metadata keys&values to be set */
        GHashTable *metadata;
        /* The user callback to notify what has happened */
        MafwSourceMetadataSetCb cb;
        /* User data for callback */
        gpointer user_data;
        /* The clip to be updated */
        gchar *clip;
};

typedef gboolean (*_GetMetadataFunc)(struct _metadata_closure *mc,
				     GList *child,
				     GError **error);

static void _metadata_closure_free(gpointer data)
{
	struct _metadata_closure *mc;

	mc = (struct _metadata_closure *) data;

	/* Free objectid of explored item */
	g_free(mc->object_id);

	/* Destroy metadata information */
	mafw_metadata_release(mc->metadata_values);

	/* Free metadata keys */
	g_strfreev(mc->metadata_keys);

	/* Free metadata closure structure */
	g_free(mc);
}

static gboolean _emit_metadata_results_idle(gpointer data)
{
	struct _metadata_closure *mc;

	mc = (struct _metadata_closure *) data;

	mc->callback(mc->source,
		     mc->object_id,
		     mc->metadata_values,
		     mc->user_data,
		     NULL);

	return FALSE;
}

static void _emit_metadata_results(struct _metadata_closure *mc)
{
	/* Emit results */
	g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
			(GSourceFunc) _emit_metadata_results_idle,
			mc,
			(GDestroyNotify) _metadata_closure_free);
}

static void _emit_metadata_error(struct _metadata_closure *mc,
                                 GError *error)
{
        GError *mafw_error;

        mafw_error = g_error_new(MAFW_SOURCE_ERROR,
                                  MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                                  "%s not found", mc->object_id);
        mc->callback(mc->source,
                     mc->object_id,
                     NULL,
                     mc->user_data,
                     mafw_error);
        g_error_free(mafw_error);
	_metadata_closure_free(mc);
}

static void _get_metadata_tracker_cb(GHashTable *result,
                                     GError *error,
                                     gpointer user_data)
{
        struct _metadata_closure *mc = (struct _metadata_closure *) user_data;

        if (!error) {
                mc->metadata_values = result;
                _emit_metadata_results(mc);
        } else {
                _emit_metadata_error(mc, error);
        }
}

static gboolean _calculate_duration_is_needed(GHashTable *metadata,
					      gchar **metadata_keys,
					      const gchar *object_id)
{
	CategoryType category;
	gchar *pls_uri = NULL;
	gboolean calculate = FALSE;

	if (util_is_duration_requested((const gchar **) metadata_keys)) {
		category = util_extract_category_info(object_id,
						      NULL,
						      NULL,
						      NULL,
						      &pls_uri);

		if (category == CATEGORY_MUSIC_PLAYLISTS) {
			if (pls_uri) {
				/* Single playlist. */
			        calculate =
					util_calculate_playlist_duration_is_needed(
						metadata);
				/* Remove the non-Mafw data used to check if
				   MAFW has to calculate the playlist
				   duration. */
				util_remove_tracker_data_to_check_pls_duration(
					metadata, metadata_keys);

				g_free(pls_uri);
			} else {
				/* Playlists category. When duration is requested,
				   calculate it always. */
				calculate = TRUE;
			}
		}
	}
	return calculate;
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
	GValue *gval = NULL;
        gint pls_duration;

	struct _metadata_closure *pls_mc =
		(struct _metadata_closure *) user_data;

	/* Get the calculated duration and add it to the results. */
	if (!error && metadata) {
		gval = mafw_metadata_first(
			metadata,
			MAFW_METADATA_KEY_DURATION);

		if (gval) {
			pls_duration = 	g_value_get_int(gval);

			gval = mafw_metadata_first(
				pls_mc->metadata_values,
				MAFW_METADATA_KEY_DURATION);

			if (gval) {
				g_value_set_int(gval, pls_duration);
			} else {
				mafw_metadata_add_int(pls_mc->metadata_values,
						      MAFW_METADATA_KEY_DURATION,
						      pls_duration);
			}
		}
	}

	_get_metadata_tracker_cb(pls_mc->metadata_values, (GError *) error,
				 user_data);

	mafw_metadata_release(metadata);
}

static void _get_metadata_tracker_from_playlist_cb(GHashTable *result,
						   GError *error,
						   gpointer user_data)
{
	if (!error && result) {
		struct _metadata_closure *mc =
			(struct _metadata_closure *) user_data;

		/* Check if we need to calculate the duration exhaustively. */
		if (_calculate_duration_is_needed(
			    result,
			    mc->metadata_keys,
			    mc->object_id)) {

			/* Store the result in the closure. */
			mc->metadata_values = result;

			/* Calculate the duration and add it to the MAFW
			   results. */
			mafw_tracker_source_get_playlist_duration(
				mc->source,
				mc->object_id,
				_add_playlist_duration_cb,
				(gpointer) mc);

			return;
		}
	}

	_get_metadata_tracker_cb(result, error, user_data);
}

static void _get_metadata_tracker_from_music_cb(GHashTable *result,
						GError *error,
						gpointer user_data)
{
        GValue *gval;

        if (result) {
                /* The count on 'music' category is 5: albums,
                 * artists, genres, songs and playlists */
                gval = mafw_metadata_first(result,
                                            MAFW_METADATA_KEY_CHILDCOUNT);
                if (gval) {
                        g_value_set_int(gval, 5);
                }
        }

        _get_metadata_tracker_cb(result, error, user_data);
}

static void _get_metadata_tracker_from_root_videos_cb(GHashTable *result,
                                                      GError *error,
                                                      gpointer user_data)
{
        GValue *gvalcur;
        GValue *gvalnew;
        struct _metadata_closure *mc = (struct _metadata_closure *) user_data;

        /* If there are previous results, aggregate durations */
        if (result) {
                if (mc->metadata_values) {
                        gvalcur = mafw_metadata_first(
                                result,
                                MAFW_METADATA_KEY_DURATION);
                        gvalnew = mafw_metadata_first(
                                mc->metadata_values,
                                MAFW_METADATA_KEY_DURATION);
                        if (gvalcur && gvalnew) {
                                g_value_set_int(gvalcur,
                                                g_value_get_int(gvalcur) +
                                                g_value_get_int(gvalnew));
                        }
			mafw_metadata_release(mc->metadata_values);
                }
        } else {
                result = mc->metadata_values;
        }

        if (result) {
                /* Change childcount: it is 2 (music and videos) */
                gvalcur = mafw_metadata_first(result,
                                               MAFW_METADATA_KEY_CHILDCOUNT);
                if (gvalcur) {
                        g_value_set_int(gvalcur, 2);
                }
        }
        _get_metadata_tracker_cb(result, error, user_data);
}

static void _get_metadata_tracker_from_root_music_cb(GHashTable *result,
                                                     GError *error,
                                                     gpointer user_data)
{
        struct _metadata_closure *mc = (struct _metadata_closure *) user_data;

        /* Store the current value */
        mc->metadata_values = result;

        /* Ask for videos and enqueue results */
        ti_get_metadata_from_videos(mc->metadata_keys,
                                    ROOT_TITLE,
                                    _get_metadata_tracker_from_root_videos_cb,
                                    mc);
}

static gchar ** _get_keys(GHashTable *metadata)
{
	GList *keys;
	gchar **key_array;
	gint i, n;

	if (!metadata)
		return NULL;

	keys = g_hash_table_get_keys(metadata);
	if (!keys)
		return NULL;

	n = g_list_length(keys);
        key_array = g_new0(gchar *, n + 1);

	for (i = 0; i < n; i++) {
		key_array[i] = g_strdup((gchar *) g_list_nth_data(keys, i));
	}

	g_list_free(keys);
	return key_array;
}

static gboolean _update_metadata_idle(gpointer data)
{
        struct _update_metadata_closure *umc = NULL;
        GError *error = NULL;
        gchar **non_updated_keys = NULL;
	gboolean updated;

        umc = (struct _update_metadata_closure *) data;

        non_updated_keys = ti_set_metadata(umc->clip, umc->metadata, 
					   &updated);

        if (!non_updated_keys) {
                error = NULL;
        } else {
                error = g_error_new(
                        MAFW_SOURCE_ERROR,
                        MAFW_SOURCE_ERROR_UNSUPPORTED_METADATA_KEY,
                        "Some keys could not be set.");
        }

	if (umc->cb != NULL) {
		umc->cb(umc->source,
			umc->object_id,
			(const gchar **) non_updated_keys,
			umc->user_data,
			error);
	}

	if (updated) {
		g_signal_emit_by_name(MAFW_SOURCE(umc->source),
				      "metadata-changed",
				      umc->object_id);
	}

        if (error) {
                g_error_free(error);
        } 		
        g_hash_table_unref(umc->metadata);
        g_free(umc->object_id);
        g_free(umc->clip);
        g_free(umc);
        g_strfreev(non_updated_keys);

        return FALSE;
}

void
mafw_tracker_source_get_metadata(MafwSource *self,
                                 const gchar *object_id,
                                 const gchar *const *metadata_keys,
                                 MafwSourceMetadataResultCb metadata_cb,
                                 gpointer user_data)
{
	GError *error = NULL;
        CategoryType category;
        const gchar* const* meta_keys;
        gchar *album = NULL;
        gchar *artist = NULL;
        gchar *clip = NULL;
        gchar *genre = NULL;
        struct _metadata_closure *mc = NULL;

	g_return_if_fail(MAFW_IS_TRACKER_SOURCE(self));

	if (!object_id) {
		g_set_error(&error,
			    MAFW_SOURCE_ERROR,
			    MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
			    "No object id was specified");
		metadata_cb(self, object_id, NULL, user_data, error);
		g_error_free(error);
		return;
        }

        category = util_extract_category_info(object_id, &genre, &artist,
                                              &album, &clip);

        if (category == CATEGORY_ERROR) {
                g_set_error(&error,
                            MAFW_SOURCE_ERROR,
                            MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
                            "Invalid object id");
		metadata_cb(self, object_id, NULL, user_data, error);
                g_error_free(error);
                return;
        }

	if (mafw_source_all_keys(metadata_keys)) {
		meta_keys = MAFW_SOURCE_LIST(KNOWN_METADATA_KEYS);
	} else {
		meta_keys = metadata_keys;
	}

	/* Prepare get_metadata operation */
	mc = g_new0(struct _metadata_closure, 1);
	mc->source = self;
	mc->object_id = g_strdup(object_id);
	mc->metadata_keys = g_strdupv((gchar **) meta_keys);
	mc->callback = metadata_cb;
	mc->user_data = user_data;

        if (clip) {
                switch (category) {
                case CATEGORY_VIDEO:
                        ti_get_metadata_from_videoclip(clip, mc->metadata_keys,
                                                       _get_metadata_tracker_cb,
                                                       mc);
                        break;
		case CATEGORY_MUSIC_PLAYLISTS:
			if (util_is_duration_requested(
				    (const gchar **) mc->metadata_keys)) {
				mc->metadata_keys =
					util_add_tracker_data_to_check_pls_duration(
						mc->metadata_keys);
			}

			ti_get_metadata_from_playlist(
				clip,
				mc->metadata_keys,
				_get_metadata_tracker_from_playlist_cb,
				mc);
			break;
                default:
                        ti_get_metadata_from_audioclip(clip, mc->metadata_keys,
                                                       _get_metadata_tracker_cb,
                                                       mc);
                        break;
                }

		g_free(clip);

		if (genre) {
			g_free(genre);
		};

		if (artist) {
			g_free(artist);
		};

		if (album) {
			g_free(album);
		};

                return;
        }

        switch (category) {
        case CATEGORY_ROOT:
                ti_get_metadata_from_music(
                        mc->metadata_keys,
                        ROOT_TITLE,
                        _get_metadata_tracker_from_root_music_cb,
                        mc);
                break;

        case CATEGORY_VIDEO:
                ti_get_metadata_from_videos(mc->metadata_keys,
                                            ROOT_VIDEOS_TITLE,
                                            _get_metadata_tracker_cb,
                                            mc);
                break;

        case CATEGORY_MUSIC:
                ti_get_metadata_from_music(
                        mc->metadata_keys,
                        ROOT_MUSIC_TITLE,
                        _get_metadata_tracker_from_music_cb,
                        mc);
                break;

        case CATEGORY_MUSIC_ARTISTS:
                ti_get_metadata_from_category(genre, artist, album,
                                              MAFW_METADATA_KEY_ARTIST,
                                              ROOT_MUSIC_ARTISTS_TITLE,
                                              mc->metadata_keys,
                                              _get_metadata_tracker_cb,
                                              mc);
                break;
        case CATEGORY_MUSIC_ALBUMS:
                ti_get_metadata_from_category(genre, artist, album,
                                              MAFW_METADATA_KEY_ALBUM,
                                              ROOT_MUSIC_ALBUMS_TITLE,
                                              mc->metadata_keys,
                                              _get_metadata_tracker_cb,
                                              mc);
                break;

        case CATEGORY_MUSIC_GENRES:
                ti_get_metadata_from_category(genre, artist, album,
                                              MAFW_METADATA_KEY_GENRE,
                                              ROOT_MUSIC_GENRES_TITLE,
                                              mc->metadata_keys,
                                              _get_metadata_tracker_cb,
                                              mc);
                break;

        case CATEGORY_MUSIC_SONGS:
                ti_get_metadata_from_music(mc->metadata_keys,
                                           ROOT_MUSIC_SONGS_TITLE,
                                           _get_metadata_tracker_cb,
                                           mc);
                break;

        case CATEGORY_MUSIC_PLAYLISTS:
		ti_get_metadata_from_playlists(
			mc->metadata_keys,
			ROOT_MUSIC_PLAYLISTS_TITLE,
			_get_metadata_tracker_from_playlist_cb,
			mc);
                break;

        default:
		/* CATEGORY_ERROR was already considered above */
                break;
        }
        if (genre) {
                g_free(genre);
        };

        if (artist) {
                g_free(artist);
        };

        if (album) {
                g_free(album);
        };
}

void
mafw_tracker_source_set_metadata(MafwSource *self,
                                 const gchar *object_id,
                                 GHashTable *metadata,
                                 MafwSourceMetadataSetCb cb,
                                 gpointer user_data)
{
	GError *error = NULL;
        gchar *clip = NULL;
	gchar **failed_keys;
	CategoryType category;

        struct _update_metadata_closure *_update_metadata_data = NULL;

        g_return_if_fail(MAFW_IS_TRACKER_SOURCE(self));

        if (!object_id) {
		g_set_error(&error,
			MAFW_SOURCE_ERROR,
			MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
			"No browse id was specified");
		failed_keys = _get_keys(metadata);

		cb(self, object_id, (const gchar **) failed_keys,
		   user_data, error);

		g_strfreev(failed_keys);
		g_error_free(error);
		return;
        }

        if (!metadata) {
		g_set_error(&error,
			MAFW_SOURCE_ERROR,
			MAFW_SOURCE_ERROR_UNSUPPORTED_METADATA_KEY,
			"No metadata was specified");
		failed_keys = _get_keys(metadata);

		cb(self, object_id, (const gchar **) failed_keys,
		   user_data, error);

		g_strfreev(failed_keys);
		g_error_free(error);
                return;
        }

        /* Only audio and video clips can be changed */
	category = util_extract_category_info(object_id, NULL,
                                              NULL, NULL, &clip);
        if ((clip) && (category != CATEGORY_MUSIC_PLAYLISTS)) {
                _update_metadata_data =
                        g_new0(struct _update_metadata_closure, 1);
                _update_metadata_data->source = self;
                _update_metadata_data->object_id = g_strdup(object_id);
                _update_metadata_data->metadata = metadata;
                _update_metadata_data->cb = cb;
                _update_metadata_data->user_data = user_data;
                _update_metadata_data->clip = clip;

                /* Block hashtable while not finishing */
                g_hash_table_ref(metadata);

                g_idle_add(_update_metadata_idle, _update_metadata_data);

                return;
        } else {
		g_set_error(&error,
			MAFW_SOURCE_ERROR,
			MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
			"Only objectids referencing clips are allowed");
		failed_keys = _get_keys(metadata);

              	cb(self, object_id, (const gchar **) failed_keys,
		   user_data, error);

		g_strfreev(failed_keys);
		g_error_free(error);
                return;
        }
}
