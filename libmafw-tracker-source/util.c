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

#include "util.h"
#include <string.h>
#include <stdlib.h>
#include <totem-pl-parser.h>
#include <libmafw/mafw-source.h>
#include <libmafw/mafw.h>
#include "key-mapping.h"

/* ------------------------- Private API ------------------------- */

static void _insert_chars(gchar *str, gchar *chars, gint index)
{
	for (; *chars != '\0'; chars++) {
		str[index++] = *chars;
	}
}

const static gchar *_get_tracker_type(GHashTable *types_map,
				      gchar *tracker_key,
				      ServiceType service)
{
	gchar *value;
	value = g_hash_table_lookup(types_map, tracker_key);
	if (value == NULL) {
		return "String";
	} else {
		return value;
	}

}

/* ------------------------- Public API ------------------------- */

void util_gvalue_free(GValue *value)
{
        if (value) {
                g_value_unset(value);
                g_free(value);
        }
}

gchar *util_get_tracker_value_for_filter(const gchar *tracker_key,
					 const gchar *value)
{
        gchar *pathname;
        gchar *escaped_pathname;

        if (!value)
                return NULL;

	if (!tracker_key) {
		return g_strdup(value);
	}

	/* Tracker just stores pathnames, so convert URI to pathame */
	if (!strcmp(tracker_key, TRACKER_KEY_FULLNAME)) {
                pathname = g_filename_from_uri(value, NULL, NULL);
                escaped_pathname = util_escape_rdf_text(pathname);
                g_free(pathname);
                return escaped_pathname;
        } else if (!strcmp(tracker_key, TRACKER_KEY_LAST_PLAYED) ||
                   !strcmp(tracker_key, TRACKER_KEY_ADDED)) {
                /* convert from epoch to iso8601 */
                return util_epoch_to_iso8601(atol(value));
	}

	/* If the key does not need special handling, just use
	   the user provided value in the filter without any modifications */
	return util_escape_rdf_text(value);
}

gchar *util_str_replace(gchar *str, gchar *old, gchar *new)
{
	GString *g_str = g_string_new(str);
        const char *cur = g_str->str;
	gint oldlen = strlen(old);
	gint newlen = strlen(new);

        while ((cur = strstr(cur, old)) != NULL) {
                int position = cur - g_str->str;
                g_string_erase(g_str, position, oldlen);
                g_string_insert(g_str, position, new);
                cur = g_str->str + position + newlen;
        }

	return g_string_free(g_str, FALSE);
}

/*
 * Splits a path-like string coming from an objectid (/itemA/itemB/itemC/...)
 * into its components
 */
GList *util_itemid_to_path(const gchar *item_id)
{
	GList *path = NULL;
	gchar **tokens;
	gchar* str;
	int i = 0;

	if (!item_id || item_id[0] == '\0')
		return path;

	/* Split using '/' as separator */
	tokens = g_strsplit(item_id, "/", -1);

	/* We will unescape all the elements. This is because our object ids
	   include filename paths (which are escaped because we want to know
	   if a '/' comes from a library category (i.e. music/artists) or it
	   is part of the file URI */

	/* If there is only one element, put it alone in the GList */
	if (!tokens[0]) {
		str = util_unescape_string(item_id);
		path = g_list_append(path, str);
	} else {
		/* Add each token to the GList */
		for (i = 0; tokens[i]; i++) {
			str = util_unescape_string(tokens[i]);
			path = g_list_append(path, str);
		}
	}

	g_strfreev(tokens);

	return path;
}

gchar* util_unescape_string(const gchar* original)
{
	return g_uri_unescape_string(original, NULL);
}

/*
 * Returns the 'data' field of a GList node.
 */
inline gchar *get_data(const GList * list)
{
	return (gchar *) list->data;
}

#ifndef G_DEBUG_DISABLE
void perf_elapsed_time_checkpoint(gchar *event)
{
        static GTimeVal elapsed_time = { 0 };
        static GTimeVal time_checkpoint = { 0 };
	GTimeVal t;
	GTimeVal checkpoint;

        if (!time_checkpoint.tv_sec) {
                g_get_current_time(&time_checkpoint);
        }

	/* Update elapsed time since last checkpoint */
	g_get_current_time(&t);
	checkpoint = t;
	t.tv_sec -= time_checkpoint.tv_sec;
	g_time_val_add(&t, -time_checkpoint.tv_usec);
	g_time_val_add(&elapsed_time,
		       t.tv_sec * 1000000L + t.tv_usec);

	/* Print current time information */
	g_debug("[PERFORMANCE] %s: %ld ms (%ld ms elapsed " \
		"since last checkpoint)\n",
		event,
		elapsed_time.tv_sec * 1000L +
		elapsed_time.tv_usec / 1000L,
		t.tv_sec * 1000L + t.tv_usec / 1000L);

	/* Save checkpoint */
	time_checkpoint.tv_sec = checkpoint.tv_sec;
        time_checkpoint.tv_usec = checkpoint.tv_usec;
}

#endif  /* G_DEBUG_DISABLE */

gchar *util_epoch_to_iso8601(glong epoch)
{
        GTimeVal timeval;

        timeval.tv_sec = epoch;
        timeval.tv_usec = 0;

        return g_time_val_to_iso8601(&timeval);
}

glong util_iso8601_to_epoch(const gchar *iso_date)
{
        GTimeVal timeval;

        g_time_val_from_iso8601(iso_date, &timeval);
        return timeval.tv_sec;
}

gchar *util_escape_rdf_text(const gchar *text)
{
	gchar *tmp;
	gint size;
	gint pos;
	gchar *result;

	if (text == NULL) {
		return NULL;
	}

	/* Calculate size of escaped string */
	for (tmp = (gchar *) text, size = 0; *tmp != '\0'; tmp++) {
		if (*tmp == '&') {
			size += 5;
		} else if (*tmp == '<' || *tmp == '>') {
			size += 4;
		} else if (*tmp == '"' || *tmp == '\'') {
			size += 6;
		} else {
			size += 1;
		}
	}

	/* Generate escaped string */
	result = g_new0(gchar, size + 1);
	for (tmp = (gchar *) text, pos = 0; *tmp != '\0'; tmp++) {
		if (*tmp == '&') {
			_insert_chars(result, "&amp;", pos);
			pos += 5;
		} else if (*tmp == '<') {
			_insert_chars(result, "&lt;", pos);
			pos += 4;
		} else if (*tmp == '>') {
			_insert_chars(result, "&gt;", pos);
			pos += 4;
		} else if (*tmp == '"') {
			_insert_chars(result, "&quot;", pos);
			pos += 6;
		} else if (*tmp == '\'') {
			_insert_chars(result, "&apos;", pos);
			pos += 6;
		} else {
			result[pos] = *tmp;
			pos++;
		}
	}

	return result;
}

gboolean util_mafw_filter_to_rdf(GHashTable *keys_map,
				 GHashTable *types_map,
				 const MafwFilter *filter,
				 GString *p)
{
	gboolean ret = TRUE;

	if (MAFW_FILTER_IS_SIMPLE(filter)) {
		gchar *close_tag;
		gchar *start_tag;
		switch (filter->type) {
		case mafw_f_eq:
			start_tag = g_strdup("<rdfq:equals>");
			close_tag = g_strdup("</rdfq:equals>");
			break;
		case mafw_f_lt:
			start_tag = g_strdup("<rdfq:lessThan>");
			close_tag = g_strdup("</rdfq:lessThan>");
			break;
		case mafw_f_gt:
			start_tag = g_strdup("<rdfq:greaterThan>");
			close_tag = g_strdup("</rdfq:greaterThan>");
 			break;
		case mafw_f_approx:
			start_tag = g_strdup("<rdfq:contains>");
			close_tag = g_strdup("</rdfq:contains>");
			break;
		case mafw_f_exists:
			g_warning("mafw_f_exists not implemented");
			ret = FALSE;
			break;
		default:
			g_warning("Unknown filter type");
			ret = FALSE;
			break;
		}

		if (ret) {
			gchar *tracker_key;
			const gchar *tracker_type;
			gchar *tracker_value;

			tracker_key =
				keymap_mafw_key_to_tracker_key(
					filter->key,
					SERVICE_MUSIC);

                        g_string_append(p, start_tag);
                        g_string_append_printf(
                                p,
                                "<rdfq:Property name=\"%s\"/>",
                                tracker_key);

			tracker_type =
				_get_tracker_type(types_map,
						  tracker_key,
						  SERVICE_MUSIC);

			tracker_value =
				util_get_tracker_value_for_filter (
					tracker_key,
					filter->value);

                        g_string_append_printf(
                                p,
                                "<rdf:%s>%s</rdf:%s>",
				tracker_type,
                                tracker_value,
				tracker_type);
                        g_string_append(p, close_tag);

			g_free(tracker_value);
			g_free(tracker_key);
			g_free(start_tag);
			g_free(close_tag);
		}

	} else {
		/* Process each part of the filter recursively */
		MafwFilter **parts;
		for (parts = filter->parts; *parts; ) {
			if (filter->type == mafw_f_not) {
				GString *cts = g_string_new("");
				g_string_append(p, "<rdfq:not>");
				ret = ret && util_mafw_filter_to_rdf(
					keys_map, types_map, *parts, cts);
 				g_string_append(p, cts->str);
				g_string_free(cts, TRUE);
				g_string_append(p, "</rdfq:not>");
				parts++;
			} else if (filter->type == mafw_f_and) {
				g_string_append(p, "<rdfq:and>");
				while (*parts != NULL) {
					GString *cts = g_string_new("");
					ret = ret &&
						util_mafw_filter_to_rdf(
                                                        keys_map,
                                                        types_map,
                                                        *parts,
                                                        cts);
					g_string_append(p, cts->str);
					g_string_free(cts, TRUE);
					parts++;
				}
				g_string_append(p, "</rdfq:and>");
			} else if (filter->type == mafw_f_or) {
				g_string_append(p, "<rdfq:or>");
				while (*parts != NULL) {
					GString *cts = g_string_new("");
					ret = ret &&
						util_mafw_filter_to_rdf(
                                                        keys_map,
                                                        types_map,
                                                        *parts,
                                                        cts);
					g_string_append(p, cts->str);
					g_string_free(cts, TRUE);
					parts++;
				}
				g_string_append(p, "</rdfq:or>");
			} else {
				g_warning("Filter type not implemented");
				ret = FALSE;
				break;
			}
		}
	}

	return ret;
}

gboolean util_tracker_value_is_unknown(const gchar *value)
{
	return (value == NULL) || (*value == '\0');
}

gchar **util_create_sort_keys_array(gint n, gchar *key1, ...)
{
	gchar **sort_keys;
	gchar *key;
	va_list args;
	gint i = 0;

	g_return_val_if_fail(n > 0, NULL);

	va_start(args, key1);

	sort_keys = g_new0(gchar *, n+1);
	sort_keys[i++] = g_strdup(key1);

	while (i<n) {
		key = va_arg(args, gchar *);
		sort_keys[i] = g_strdup(key);
		i++;
	}

	va_end(args);

	return sort_keys;
}

gchar *util_create_filter_from_category(const gchar *genre,
					const gchar *artist,
					const gchar *album,
					const gchar *user_filter)
{
        gint parts_of_filter = 1;
        gint i;
        gchar *rdf_filter = NULL;
        gchar **filters = NULL;
        gchar *escaped_genre = NULL;
        gchar *escaped_artist = NULL;
        gchar *escaped_album = NULL;

        if (genre)
                parts_of_filter++;
        if (artist)
                parts_of_filter++;
        if (album)
                parts_of_filter++;

        filters = g_new0(gchar *, parts_of_filter);
        i = 0;

        if (genre) {
                escaped_genre =
                        util_get_tracker_value_for_filter(TRACKER_KEY_GENRE,
							  genre);
                filters[i] = g_strdup_printf(RDF_QUERY_BY_GENRE,
                                             escaped_genre);
                g_free(escaped_genre);
                i++;
        }

        if (artist) {
                escaped_artist =
			util_get_tracker_value_for_filter(TRACKER_KEY_ARTIST,
							  artist);
                filters[i] = g_strdup_printf(RDF_QUERY_BY_ARTIST,
					     escaped_artist);
                g_free(escaped_artist);
                i++;
        }

        if (album) {
                escaped_album =
                        util_get_tracker_value_for_filter(TRACKER_KEY_ALBUM,
							  album);
                filters[i] = g_strdup_printf(RDF_QUERY_BY_ALBUM,
                                             escaped_album);
                g_free(escaped_album);
                i++;
        }

        rdf_filter = util_build_complex_rdf_filter(filters, user_filter);

        g_strfreev(filters);

        return rdf_filter;
}

gchar *util_build_complex_rdf_filter(gchar **filters,
				     const gchar *append_filter)
{
        gchar *cfilter;
        gchar *join_filters;
        gint len;

        if (filters)
                len = g_strv_length(filters);
        else
                len = 0;

        if (append_filter)
                len++;

        switch (len) {
        case 0:
                cfilter = NULL;
                break;
        case 1:
                if (append_filter)
                        cfilter = g_strconcat(RDF_QUERY_BEGIN, append_filter,
                                              RDF_QUERY_END, NULL);
                else
                        cfilter = g_strconcat(RDF_QUERY_BEGIN, filters[0],
                                              RDF_QUERY_END, NULL);
                break;
        default:
                join_filters = g_strjoinv(NULL, filters);
                if (append_filter)
                        cfilter = g_strconcat(RDF_QUERY_AND_BEGIN,
                                              join_filters,
                                              append_filter,
                                              RDF_QUERY_AND_END,
                                              NULL);
                else
                        cfilter = g_strconcat(RDF_QUERY_AND_BEGIN,
                                              join_filters,
                                              RDF_QUERY_AND_END,
                                              NULL);
                g_free(join_filters);
                break;
        }

        return cfilter;
}

void util_sum_duration(gpointer data, gpointer user_data)
{
        GValue *gval;
        gint *total_sum = (gint *) user_data;
        GHashTable *metadata = (GHashTable *) data;

        gval = mafw_metadata_first(metadata, MAFW_METADATA_KEY_DURATION);
        if (gval) {
                *total_sum += g_value_get_int(gval);
        }
}

void util_sum_count(gpointer data, gpointer user_data)
{
        GValue *gval;
        gint *total_sum = (gint *) user_data;
        GHashTable *metadata = (GHashTable *) data;

        gval = mafw_metadata_first(metadata, MAFW_METADATA_KEY_CHILDCOUNT);
        if (gval) {
                *total_sum += g_value_get_int(gval);
        }
}

CategoryType util_extract_category_info(const gchar *object_id,
                                        gchar **genre,
                                        gchar **artist,
                                        gchar **album,
                                        gchar **clip)
{
	gchar *item_id;
	GList *path;
	GList *next;
	gchar *path_name;
	guint path_length;
	CategoryType category;

        /* Initialize values */
        if (genre) {
                *genre = NULL;
        }

        if (artist) {
                *artist = NULL;
        }

        if (album) {
                *album = NULL;
        }

        if (clip) {
                *clip = NULL;
        }

	item_id = NULL;

	/* Skip the protocol part of the objectid to get the path-like part */
        mafw_source_split_objectid(object_id, NULL, &item_id);

	/* Wrong protocol */
	if (!item_id) {
		return CATEGORY_ERROR;
        }

	/* Split the path of the objectid into its components */
	path = util_itemid_to_path(item_id);
	g_free(item_id);

	/* Get category type */
	if (path != NULL) {
		path_name = get_data(path);
		path_length = g_list_length(path);
		
		if (g_strcasecmp(path_name, TRACKER_SOURCE_VIDEOS) == 0) {
			category = CATEGORY_VIDEO;
		} else if (g_strcasecmp(path_name,
                                        TRACKER_SOURCE_MUSIC) == 0) {
			next = g_list_next(path);
			if (!next) {
				category = CATEGORY_MUSIC;
			} else {
				path_name = get_data(next);
				if (!g_strcasecmp(path_name,
						  TRACKER_SOURCE_PLAYLISTS)) {
					category = CATEGORY_MUSIC_PLAYLISTS;
				} else if (!g_strcasecmp(
                                                   path_name,
                                                   TRACKER_SOURCE_SONGS)) {
					category = CATEGORY_MUSIC_SONGS;
				} else if (!g_strcasecmp(
                                                   path_name,
                                                   TRACKER_SOURCE_GENRES)) {
					category = CATEGORY_MUSIC_GENRES;
				} else if (!g_strcasecmp(
                                                   path_name,
                                                   TRACKER_SOURCE_ARTISTS)) {
					category = CATEGORY_MUSIC_ARTISTS;
				} else if (!g_strcasecmp(
                                                   path_name,
                                                   TRACKER_SOURCE_ALBUMS)) {
					category = CATEGORY_MUSIC_ALBUMS;
				} else {
					category = CATEGORY_ERROR;
				}
			}
		} else {
			category = CATEGORY_ERROR;
		}
	} else {
		category = CATEGORY_ROOT;
	}

	/* Get info */
	switch (category) {
	case CATEGORY_ROOT:
	case CATEGORY_MUSIC:
	case CATEGORY_ERROR:
		/* No info to extract */
		break;
	case CATEGORY_VIDEO:
		if (path_length >2) {
			category = CATEGORY_ERROR;
		} else if (clip && path_length == 2) {
                        *clip = g_filename_to_uri(get_data(g_list_nth(path, 1)),
                                                  NULL, NULL);
		}
		break;
	case CATEGORY_MUSIC_SONGS:
		if (path_length > 3) {
			category = CATEGORY_ERROR;
		} else if (clip && path_length == 3) {
			*clip = g_filename_to_uri(get_data(g_list_nth(path, 2)),
                                                  NULL, NULL);
		}
		break;
	case CATEGORY_MUSIC_PLAYLISTS:
		switch (path_length) {
		case 3:
                        if (clip) {
                                *clip =
                                        g_filename_to_uri(
						get_data(g_list_nth(path, 2)),
						NULL, NULL);
                        }
			break;
		case 2:
			/* No info to extract */
			break;
		default:
			category = CATEGORY_ERROR;
			break;
		}
		break;
	case CATEGORY_MUSIC_ALBUMS:
		switch (path_length) {
		case 4:
                        if (clip) {
                                *clip = g_filename_to_uri(
                                        get_data(g_list_nth(path, 3)),
                                        NULL, NULL);
                        }
			/* No break */
		case 3:
			if (album) {
				*album = g_strdup(get_data(g_list_nth(path, 2)));
			}
			break;
		case 2:
			/* No info to extract */
			break;
		default:
			category = CATEGORY_ERROR;
			break;
		}
		break;
	case CATEGORY_MUSIC_ARTISTS:
		switch (path_length) {
		case 5:
                        if (clip) {
                                *clip = g_filename_to_uri(
                                        get_data(g_list_nth(path, 4)),
                                        NULL, NULL);
                        }
			/* No break */
		case 4:
                        if (album) {
                                *album =
                                        g_strdup(get_data(g_list_nth(path, 3)));
                        }
			/* No break */
		case 3:
                        if (artist) {
                                *artist =
                                        g_strdup(get_data(g_list_nth(path, 2)));
                        }
			/* No break */
		case 2:
			/* No info to extract */
			break;
		default:
			category = CATEGORY_ERROR;
			break;
		}
		break;
	case CATEGORY_MUSIC_GENRES:
		switch (path_length) {
		case 6:
                        if (clip) {
                                *clip = g_filename_to_uri(
                                        get_data(g_list_nth(path, 5)),
                                        NULL, NULL);
                        }
			/* No break */
		case 5:
                        if (album) {
                                *album =
                                        g_strdup(get_data(g_list_nth(path, 4)));
                        }
			/* No break */
		case 4:
                        if (artist) {
                                *artist =
                                        g_strdup(get_data(g_list_nth(path, 3)));
                        }
			/* No break */
		case 3:
                        if (genre) {
                                *genre =
                                        g_strdup(get_data(g_list_nth(path, 2)));
                        }
			break;
		case 2:
			/* No info to extract */
			break;
		default:
			category = CATEGORY_ERROR;
			break;
		}
		break;
	default:
		category = CATEGORY_ERROR;
		break;
	}

	/* Frees */
	g_list_foreach(path, (GFunc) g_free, NULL);
	g_list_free(path);

	return category;
}

static gchar** _add_key(gchar **key_list, const gchar *key)
{
	gchar **new_key_list;
	guint n = g_strv_length(key_list);

	new_key_list = g_try_renew(gchar *, key_list, n + 2);
	new_key_list[n] = g_strdup(key);
	new_key_list[n + 1] = NULL;

	return new_key_list;
}

static gboolean _contains_key(const gchar **key_list, const gchar *key)
{
	int i = 0;

	if (key_list == NULL)
		return FALSE;

	while (key_list[i] != NULL) {
		if (g_strcmp0(key_list[i], key) == 0) {
			return TRUE;
		}
		i++;
	}

	return FALSE;
}

gboolean util_is_duration_requested(const gchar **key_list)
{
	return _contains_key(key_list, MAFW_METADATA_KEY_DURATION);
}

/*
 * To calculate the playlist duration is needed when Tracker has returned
 * duration 0 and MAFW has never calculate it before.
 */
gboolean util_calculate_playlist_duration_is_needed(GHashTable *pls_metadata)
{
	GValue *gval;
	gboolean calculate = FALSE;

	if (!pls_metadata)
		return FALSE;

	/* Check if Tracker returned duration 0. */
	gval = mafw_metadata_first(
		pls_metadata,
		MAFW_METADATA_KEY_DURATION);

	if (!gval) {
		/* Duration is 0. */
		/* Check if MAFW has calculated this duration before. */
		gval = mafw_metadata_first(
			pls_metadata,
			TRACKER_KEY_PLAYLIST_VALID_DURATION);

		if ((gval) && (g_value_get_int(gval) == 0)) {
			/* The duration hasn't been calculated before,
			   so do it now. */
			calculate = TRUE;
		}
	}

	return calculate;
}

gchar** util_add_tracker_data_to_check_pls_duration(gchar **keys)
{
	/* Add the valid-duration key to the requested keys.
	   It's a Tracker key, without correspondence in MAFW, is needed to
	   check if MAFW has calculated the playlist duration before.
	   Don't forget remove it from the MAFW results before sending them to
	   the user!. */
	return _add_key(keys, TRACKER_KEY_PLAYLIST_VALID_DURATION);
}

void util_remove_tracker_data_to_check_pls_duration(GHashTable *metadata,
						    gchar **metadata_keys)
{
	/* Remove the valid-duration value from the results.
	   It's a Tracker metadata key without correspondence in MAFW.
	   Don't return it to the user!. */
	g_hash_table_remove(metadata, TRACKER_KEY_PLAYLIST_VALID_DURATION);

	if (_contains_key((const gchar **) metadata_keys,
			  TRACKER_KEY_PLAYLIST_VALID_DURATION)) {
		gint n = g_strv_length(metadata_keys);
		g_free(metadata_keys[n-1]);
		metadata_keys[n-1] = NULL;
	}
}

