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

#include "album-art.h"
#include "util.h"
#include "key-mapping.h"
#include <string.h>
#include <stdlib.h>
#include <hildon-thumbnail-factory.h>
#include <hildon-albumart-factory.h>
#include <gio/gio.h>


/* ------------------------- Public API ------------------------- */

gchar *albumart_get_thumbnail_uri(const gchar *orig_file_uri,
				  enum thumbnail_size size)
{
        gchar *file_uri;
        GFile *file;

        if (size == THUMBNAIL_CROPPED) {
                file_uri = hildon_thumbnail_get_uri(orig_file_uri,
                                                    128, 128, TRUE);
                /* Check if file doesn't exist */
                file = g_file_new_for_uri(file_uri);
                if (!g_file_query_exists(file, NULL)) {
                        g_free(file_uri);
                        file_uri = NULL;
                }
                g_object_unref(file);
        } else {
                /* Get the original album art */
                file_uri = albumart_get_album_art_uri(orig_file_uri);
        }

        return file_uri;
}

gchar *albumart_get_album_art_uri(const gchar *album)
{
	gchar *file_uri;
        gchar *file_path;
	gchar *album_key;
        GFile *file;

	if (util_tracker_value_is_unknown(album)) {
                return NULL;
	} else {
		album_key = g_strdup(album);
	}

	/* Get the path to the album-art */
	file_path = hildon_albumart_get_path(NULL, album_key, "album");
        file_uri = g_filename_to_uri(file_path, NULL, NULL);
        g_free(file_path);

        /* Check if file exists */
        file = g_file_new_for_uri(file_uri);
        if (!g_file_query_exists(file, NULL)) {
                g_free(file_uri);
                file_uri = NULL;
        }
	g_object_unref(file);
	g_free(album_key);

	return file_uri;
}

gboolean albumart_key_is_album_art(const gchar *key)
{
        MetadataKey *metadata_key;

        metadata_key = keymap_get_metadata(key);

        if (metadata_key) {
                return metadata_key->key_type == ALBUM_ART_KEY;
        } else {
                return FALSE;
        }
}

gboolean albumart_key_is_thumbnail(const gchar *key)
{
        MetadataKey *metadata_key;

        metadata_key = keymap_get_metadata(key);

        if (metadata_key) {
                return metadata_key->key_type == THUMBNAIL_KEY;
        } else {
                return FALSE;
        }
}
