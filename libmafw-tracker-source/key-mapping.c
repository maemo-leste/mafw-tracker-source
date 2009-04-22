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
        InfoKeyTable *t = keymap_build_mafw_to_tracker_keys_map();
        gchar *tracker_key;

        switch (service) {
        case SERVICE_MUSIC:
                tracker_key = g_hash_table_lookup(t->music_keys, mafw_key);
                break;
        case SERVICE_VIDEOS:
                tracker_key = g_hash_table_lookup(t->videos_keys, mafw_key);
                break;
        default:
                tracker_key = g_hash_table_lookup(t->playlist_keys, mafw_key);
                break;
        }

        /* If not found, look in common */
        if (!tracker_key) {
                tracker_key = g_hash_table_lookup(t->common_keys, mafw_key);
        }

	return g_strdup(tracker_key);
}

gboolean keymap_mafw_key_supported_in_tracker(const gchar *mafw_key)
{
        InfoKeyTable *t = keymap_build_mafw_to_tracker_keys_map();

        return g_hash_table_lookup(t->music_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->videos_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->playlist_keys, mafw_key) != NULL ||
                g_hash_table_lookup(t->common_keys, mafw_key) != NULL;
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

InfoKeyTable *keymap_build_mafw_to_tracker_keys_map(void)
{
	static InfoKeyTable *table = NULL;
        MetadataKey *metadata_key = NULL;

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
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    TRACKER_AKEY_TITLE);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    TRACKER_AKEY_DURATION);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_ARTIST,
                                    TRACKER_AKEY_ARTIST);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_ALBUM,
                                    TRACKER_AKEY_ALBUM);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_GENRE,
                                    TRACKER_AKEY_GENRE);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_TRACK,
                                    TRACKER_AKEY_TRACK);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_YEAR,
                                    TRACKER_AKEY_YEAR);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_BITRATE,
                                    TRACKER_AKEY_BITRATE);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_LAST_PLAYED,
                                    TRACKER_AKEY_LAST_PLAYED);
                g_hash_table_insert(table->music_keys,
                                    MAFW_METADATA_KEY_PLAY_COUNT,
                                    TRACKER_AKEY_PLAY_COUNT);

                /* Insert mapping for videos service */
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    TRACKER_VKEY_TITLE);
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    TRACKER_VKEY_DURATION);
                g_hash_table_insert(table->videos_keys,
                                    MAFW_METADATA_KEY_VIDEO_FRAMERATE,
                                    TRACKER_VKEY_FRAMERATE);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
                                    TRACKER_VKEY_PAUSED_THUMBNAIL);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_PAUSED_POSITION,
                                    TRACKER_VKEY_PAUSED_POSITION);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_VIDEO_SOURCE,
                                    TRACKER_VKEY_SOURCE);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_RES_X,
                                    TRACKER_VKEY_RES_X);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_RES_Y,
                                    TRACKER_VKEY_RES_Y);

                /* Insert mapping for playlist service */
                g_hash_table_insert(table->playlist_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    TRACKER_PKEY_DURATION);
                g_hash_table_insert(table->playlist_keys,
                                    MAFW_METADATA_KEY_CHILDCOUNT,
                                    TRACKER_PKEY_COUNT);

                /* Special key (not available in MAFW) */
                g_hash_table_insert(table->playlist_keys,
                                    TRACKER_PKEY_VALID_DURATION,
                                    TRACKER_PKEY_VALID_DURATION);

                /* Insert mapping common for all services */
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_COPYRIGHT,
                                    TRACKER_FKEY_COPYRIGHT);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_FILESIZE,
                                    TRACKER_FKEY_FILESIZE);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_FILENAME,
                                    TRACKER_FKEY_FILENAME);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_MIME,
                                    TRACKER_FKEY_MIME);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_ADDED,
                                    TRACKER_FKEY_ADDED);
                g_hash_table_insert(table->common_keys,
                                    MAFW_METADATA_KEY_URI,
                                    TRACKER_FKEY_FULLNAME);

                /* Insert metadata assocciated with each key */
                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
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
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_TITLE,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_DURATION,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_STRING;
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
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_LONG;
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_LAST_PLAYED,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
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
                g_hash_table_insert(table->metadata_keys,
                                    MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
                                    metadata_key);

                metadata_key = g_new0(MetadataKey, 1);
                metadata_key->value_type = G_TYPE_INT;
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
        }

	return table;
}

GHashTable *keymap_build_tracker_types_map(void)
{
	static GHashTable *table = NULL;

        if (!table) {
                table = g_hash_table_new(g_str_hash, g_str_equal);

                g_hash_table_insert(table,
                                    TRACKER_AKEY_DURATION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_VKEY_DURATION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_AKEY_TRACK, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_AKEY_YEAR, "Date");
                g_hash_table_insert(table,
                                    TRACKER_AKEY_BITRATE, "Double");
                g_hash_table_insert(table,
                                    TRACKER_AKEY_LAST_PLAYED, "Date");
                g_hash_table_insert(table,
                                    TRACKER_AKEY_PLAY_COUNT, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_FKEY_ADDED, "Date");
                g_hash_table_insert(table,
                                    TRACKER_VKEY_PAUSED_POSITION, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_VKEY_RES_X, "Integer");
                g_hash_table_insert(table,
                                    TRACKER_VKEY_RES_Y, "Integer");
        }

        return table;
}

gchar **keymap_mafw_keys_to_tracker_keys(gchar **mafw_keys,
					 ServiceType service)
{
        InfoKeyTable *t = keymap_build_mafw_to_tracker_keys_map();
        GHashTable *lookin;
	gchar **tracker_keys;
	gint i, count;

	if (mafw_keys == NULL) {
		return NULL;
	}

	/* Count the number of keys */
        switch (service) {
        case SERVICE_MUSIC:
                lookin = t->music_keys;
                break;
        case SERVICE_VIDEOS:
                lookin = t->videos_keys;
                break;
        default:
                lookin = t->playlist_keys;
                break;
        }

	for (i=0, count=0; mafw_keys[i] != NULL; i++) {
                /* Check if the key is translatable to tracker */
                if (g_hash_table_lookup(lookin, mafw_keys[i]) ||
                    g_hash_table_lookup(t->common_keys, mafw_keys[i])) {
                        count++;
                }
	}

	/* Allocate memory for the converted array (include trailing NULL) */
	tracker_keys = g_new0(gchar *, count + 1);

	/* Now, translate the keys */
	for (i=0, count=0; mafw_keys[i] != NULL; i++) {
                tracker_keys[count++] =
                        keymap_mafw_key_to_tracker_key(mafw_keys[i], service);
        }

	return tracker_keys;
}
