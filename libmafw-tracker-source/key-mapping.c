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
        TrackerKey *tracker_key;

        tracker_key = keymap_get_tracker_info(mafw_key, service);

        if (tracker_key) {
                return g_strdup(tracker_key->tracker_key);
        } else {
                return NULL;
        }
}

gboolean keymap_is_key_supported_in_tracker(const gchar *mafw_key)
{
        static InfoKeyTable *t = NULL;

        if (!t) {
                t = keymap_get_info_key_table();
        }

        return g_hash_table_lookup(t->music_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->videos_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->playlist_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->common_keys, mafw_key) != NULL;
}

gboolean keymap_mafw_key_is_writable(gchar *mafw_key)
{
        MetadataKey *metadata_key;

        metadata_key = keymap_get_metadata(mafw_key);

        /* If key is not found, return FALSE */
        return metadata_key && metadata_key->writable;
}


InfoKeyTable *keymap_get_info_key_table(void)
{
	static InfoKeyTable *table = NULL;
        MetadataKey *metadata_key = NULL;
        TrackerKey *tracker_key = NULL;

        if (!table) {
                table = g_new0(InfoKeyTable, 1);
                table->music_keys = g_hash_table_new(g_str_hash, g_str_equal);
                table->videos_keys = g_hash_table_new(g_str_hash, g_str_equal);
                table->playlist_keys = g_hash_table_new(g_str_hash,
                                                        g_str_equal);
                table->common_keys = g_hash_table_new(g_str_hash, g_str_equal);
                table->metadata_keys = g_hash_table_new(g_str_hash,
                                                        g_str_equal);

                /* Insert mapping for music service */
                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_TITLE;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_DURATION;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_ARTIST;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_ARTIST,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_ALBUM;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_ALBUM,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_GENRE;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_GENRE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_TRACK;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_TRACK,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_YEAR;
                tracker_key->value_type = G_TYPE_DATE;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_YEAR,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_BITRATE;
                tracker_key->value_type = G_TYPE_DOUBLE;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_BITRATE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_LAST_PLAYED;
                tracker_key->value_type = G_TYPE_DATE;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_LAST_PLAYED,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_AKEY_PLAY_COUNT;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_PLAY_COUNT,
                                    tracker_key);

                /* Insert mapping for videos service */
                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_TITLE;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_DURATION;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_FRAMERATE;
                tracker_key->value_type = G_TYPE_DOUBLE;
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_VIDEO_FRAMERATE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_PAUSED_THUMBNAIL;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_PAUSED_POSITION;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_PAUSED_POSITION,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_SOURCE;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_VIDEO_SOURCE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_RES_X;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_RES_X,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_VKEY_RES_Y;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_RES_Y,
                                    tracker_key);

                /* Insert mapping for playlist service */
                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_PKEY_DURATION;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->playlist_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_PKEY_COUNT;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->playlist_keys,
                                    MAFW_METADATA_KEY_CHILDCOUNT,
                                    tracker_key);

                /* Special key (not available in MAFW) */
                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_PKEY_VALID_DURATION;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->playlist_keys,
                                    TRACKER_PKEY_VALID_DURATION,
                                    tracker_key);

                /* Insert mapping common for all services */
                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_COPYRIGHT;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_COPYRIGHT,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_FILESIZE;
                tracker_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_FILESIZE,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_FILENAME;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_FILENAME,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_MIME;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_MIME,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_ADDED;
                tracker_key->value_type = G_TYPE_DATE;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_ADDED,
                                    tracker_key);

                tracker_key = g_new0(TrackerKey, 1);
                tracker_key->tracker_key = TRACKER_FKEY_FULLNAME;
                tracker_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_URI,
                                    tracker_key);

                /* Insert metadata assocciated with each key */
                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                metadata_key->allowed_empty = TRUE;
                metadata_key->special = SPECIAL_KEY_CHILDCOUNT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_CHILDCOUNT,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_FLOAT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_VIDEO_FRAMERATE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_COPYRIGHT,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_FILESIZE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_FILENAME,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->allowed_empty = TRUE;
                metadata_key->special = SPECIAL_KEY_TITLE;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                metadata_key->special = SPECIAL_KEY_DURATION;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->special = SPECIAL_KEY_MIME;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_MIME,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ARTIST,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ALBUM,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_GENRE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_TRACK,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_YEAR,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_BITRATE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->special = SPECIAL_KEY_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_LONG;
                metadata_key->writable = TRUE;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_LAST_PLAYED,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                metadata_key->writable = TRUE;
                metadata_key->allowed_empty = TRUE;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_PLAY_COUNT,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_LONG;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ADDED,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->writable = TRUE;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                metadata_key->writable = TRUE;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_PAUSED_POSITION,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_VIDEO_SOURCE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_RES_X,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_RES_Y,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_BOOLEAN;
                g_hash_table_insert(table->metadata_keys,
                                    TRACKER_PKEY_VALID_DURATION,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = ALBUM_ART_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_ALBUM_ART_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ALBUM_ART_SMALL_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = ALBUM_ART_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_ALBUM_ART_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ALBUM_ART_MEDIUM_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = ALBUM_ART_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_ALBUM_ART_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ALBUM_ART_LARGE_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = ALBUM_ART_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_ALBUM;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_ALBUM_ART_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = THUMBNAIL_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_THUMBNAIL_SMALL_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = THUMBNAIL_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_THUMBNAIL_MEDIUM_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = THUMBNAIL_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_THUMBNAIL_LARGE_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
                metadata_key->key_type = THUMBNAIL_KEY;
                metadata_key->depends_on = MAFW_METADATA_KEY_URI;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_THUMBNAIL_URI,
                                    metadata_key);
        }

	return table;
}

gchar **keymap_mafw_keys_to_tracker_keys(gchar **mafw_keys,
					 ServiceType service)
{
	gchar **tracker_keys;
	gint i, count;
        TrackerKey *tracker_key;

	if (mafw_keys == NULL) {
		return NULL;
	}

	/* Count the number of keys */
	for (i=0, count=0; mafw_keys[i] != NULL; i++) {
                /* Check if the key is translatable to tracker */
                if (keymap_get_tracker_info(mafw_keys[i], service) != NULL) {
                        count++;
                }
	}

	/* Allocate memory for the converted array (include trailing NULL) */
	tracker_keys = g_new0(gchar *, count + 1);

	/* Now, translate the keys supported in tracker */
	for (i=0, count=0; mafw_keys[i] != NULL; i++) {
                tracker_key = keymap_get_tracker_info(mafw_keys[i], service);
                if (tracker_key) {
                        tracker_keys[count++] =
                                keymap_mafw_key_to_tracker_key(mafw_keys[i],
                                                               service);
                }
        }

	return tracker_keys;
}

MetadataKey *keymap_get_metadata(const gchar *mafw_key)
{
        static InfoKeyTable *table = NULL;

        if (!table) {
                table = keymap_get_info_key_table();
        }

        return g_hash_table_lookup(table->metadata_keys, mafw_key);
}

TrackerKey *keymap_get_tracker_info(const gchar *mafw_key,
                                    ServiceType service)
{
        static InfoKeyTable *table = NULL;
        TrackerKey *tracker_key;

        if (!table) {
                table = keymap_get_info_key_table();
        }

        switch (service) {
        case SERVICE_VIDEOS:
                tracker_key = g_hash_table_lookup(table->videos_keys,
                                                  mafw_key);
                break;
        case SERVICE_PLAYLISTS:
                tracker_key = g_hash_table_lookup(table->playlist_keys,
                                                  mafw_key);
                break;
        default:
                tracker_key = g_hash_table_lookup(table->music_keys,
                                                  mafw_key);
        }

        if (tracker_key) {
                return tracker_key;
        } else {
                return g_hash_table_lookup(table->common_keys,
                                           mafw_key);
        }
}

GType keymap_get_tracker_type(const gchar *mafw_key,
                              ServiceType service)
{
        TrackerKey *tracker_key;

        tracker_key = keymap_get_tracker_info(mafw_key, service);

        if (tracker_key) {
                return tracker_key->value_type;
        } else {
                return G_TYPE_NONE;
        }
}
