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

#include "key-mapping.h"
#include "album-art.h"
#include "definitions.h"
#include <string.h>
#include <libmafw/mafw.h>

/* ------------------------- Public API ------------------------- */

gchar *keymap_mafw_key_to_tracker_key(const gchar *mafw_key,
				      ServiceType service)
{
	gchar *value = NULL;
        GHashTable *keys_map;

	if (mafw_key == NULL) {
		return NULL;
	}

	/* Special cases */
	if (service == SERVICE_VIDEOS) {
		if (!strcmp(mafw_key, MAFW_METADATA_KEY_TITLE)) {
			value = g_strdup(TRACKER_KEY_V_TITLE);
                        return value;
		}

                if (!strcmp(mafw_key, MAFW_METADATA_KEY_DURATION)) {
                        value = g_strdup(TRACKER_KEY_VIDEO_DURATION);
                        return value;
                }
	}

	if (service == SERVICE_PLAYLISTS) {
		if (!strcmp(mafw_key, MAFW_METADATA_KEY_DURATION)) {
			value = g_strdup(TRACKER_KEY_PLAYLIST_DURATION);
                        return value;
		} else if (!strcmp(mafw_key, MAFW_METADATA_KEY_CHILDCOUNT)) {
			value = g_strdup(TRACKER_KEY_PLAYLIST_COUNT);
                        return value;
		}
	}

	/* Normal cases */
        keys_map = keymap_build_mafw_to_tracker_keys_map();
        value = g_hash_table_lookup(keys_map, mafw_key);

        if (value == NULL) {
                /* Fallback case...*/
                value = g_strdup(mafw_key);
        } else {
                value = g_strdup (value);
        }

	return value;
}

inline gboolean keymap_mafw_key_supported_in_tracker(const gchar *mafw_key)
{
	return g_hash_table_lookup(keymap_build_mafw_to_tracker_keys_map(),
                                   mafw_key) != NULL;
}


gboolean keymap_mafw_key_is_writable(gchar *mafw_key)
{
        if (strcmp(mafw_key, MAFW_METADATA_KEY_LAST_PLAYED) == 0 ||
            strcmp(mafw_key, MAFW_METADATA_KEY_PLAY_COUNT) == 0 ||
            strcmp(mafw_key, MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI) == 0 ||
            strcmp(mafw_key, MAFW_METADATA_KEY_PAUSED_POSITION) == 0) {
                return TRUE;
        } else {
                return FALSE;
        }
}

GHashTable *keymap_build_mafw_to_tracker_keys_map(void)
{
	static GHashTable *table = NULL;

        if (!table) {
                table = g_hash_table_new(g_str_hash, g_str_equal);

                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_TITLE, TRACKER_KEY_TITLE);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_DURATION,
                                    TRACKER_KEY_DURATION);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_MIME, TRACKER_KEY_MIME);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_ARTIST,
                                    TRACKER_KEY_ARTIST);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_ALBUM, TRACKER_KEY_ALBUM);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_GENRE, TRACKER_KEY_GENRE);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_TRACK, TRACKER_KEY_TRACK);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_YEAR, TRACKER_KEY_YEAR);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_BITRATE,
                                    TRACKER_KEY_BITRATE);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_URI,
                                    TRACKER_KEY_FULLNAME);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_LAST_PLAYED,
                                    TRACKER_KEY_LAST_PLAYED);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_PLAY_COUNT,
                                    TRACKER_KEY_AUDIO_PLAY_COUNT);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_ADDED, TRACKER_KEY_ADDED);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
                                    TRACKER_KEY_PAUSED_THUMBNAIL);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_PAUSED_POSITION,
                                    TRACKER_KEY_PAUSED_POSITION);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_VIDEO_SOURCE,
                                    TRACKER_KEY_VIDEO_SOURCE);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_RES_X, TRACKER_KEY_RES_X);
                g_hash_table_insert(table,
                                    MAFW_METADATA_KEY_RES_Y, TRACKER_KEY_RES_Y);
		g_hash_table_insert(table,
				    TRACKER_KEY_PLAYLIST_VALID_DURATION,
				    TRACKER_KEY_PLAYLIST_VALID_DURATION);
        }

	return table;
}

GHashTable *keymap_build_tracker_types_map(void)
{
	static GHashTable *table = NULL;

        if (!table) {
                table = g_hash_table_new(g_str_hash, g_str_equal);

                g_hash_table_insert(table,
                                    TRACKER_KEY_DURATION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_VIDEO_DURATION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_TRACK, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_YEAR, "Date");
                g_hash_table_insert(table,
                                    TRACKER_KEY_BITRATE, "Double");
                g_hash_table_insert(table,
                                    TRACKER_KEY_LAST_PLAYED, "Date");
                g_hash_table_insert(table,
                                    TRACKER_KEY_AUDIO_PLAY_COUNT, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_ADDED, "Date");
                g_hash_table_insert(table,
                                    TRACKER_KEY_PAUSED_POSITION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_RES_X, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_KEY_RES_Y, "Integer");
        }

        return table;
}

gchar **keymap_mafw_keys_to_tracker_keys(gchar **mafw_keys,
					 ServiceType service)
{
	gchar **tracker_keys;
	gint i, count;

	if (mafw_keys == NULL) {
		return NULL;
	}

	/* Count the number of keys */
	for (i=0, count=0; mafw_keys[i] != NULL; i++) {
                if (keymap_mafw_key_supported_in_tracker(mafw_keys[i]) ||
                    (service == SERVICE_PLAYLISTS &&
                     g_strcmp0(mafw_keys[i],
                               MAFW_METADATA_KEY_CHILDCOUNT) == 0)) {
                        count++;
                }
	}

	/* Allocate memory for the converted array (include trailing NULL) */
	tracker_keys = g_new0(gchar *, count + 1);

	/* Translate keys */
	for (i=0, count=0; mafw_keys[i] != NULL; i++)
	{
                if ((service == SERVICE_PLAYLISTS &&
                     g_strcmp0(mafw_keys[i],
                               MAFW_METADATA_KEY_CHILDCOUNT) == 0) ||
                    (keymap_mafw_key_supported_in_tracker(mafw_keys[i]))) {
                        tracker_keys[count++] =
                                keymap_mafw_key_to_tracker_key(mafw_keys[i],
                                                               service);
                }
	}

	return tracker_keys;
}
