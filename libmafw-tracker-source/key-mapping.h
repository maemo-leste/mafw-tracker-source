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

#ifndef _MAFW_TRACKER_SOURCE_KEY_MAPPING_H_
#define _MAFW_TRACKER_SOURCE_KEY_MAPPING_H_

#include <glib.h>
#include <tracker.h>

typedef struct MetadataKey {
        /* The type of the key. NOTE: G_TYPE_DATE will be handle as
         * G_TYPE_INT. But they are separated 'cause in we need to use
         * conversion functions when converting the keys to tracker keys */
        GType value_type;
        /* Is the key writable? Default is FALSE */
        gboolean writable;
} MetadataKey;

typedef struct InfoKeyTable {
        /* Mapping mafw->tracker keys within music service */
        GHashTable *music_keys;
        /* Mapping mafw->tracker keys within videos service */
        GHashTable *videos_keys;
        /* Mapping mafw->tracker keys within playlist service */
        GHashTable *playlist_keys;
        /* Mapping mafw->tracker keys within common service */
        GHashTable *common_keys;
        /* Metadata associated with each mafw key */
        GHashTable *metadata_keys;
} InfoKeyTable;


gchar *keymap_mafw_key_to_tracker_key(const gchar *mafw_key,
				      ServiceType service);
gchar **keymap_mafw_keys_to_tracker_keys(gchar **mafw_keys,
					 ServiceType service);
gboolean keymap_mafw_key_supported_in_tracker(const gchar *mafw_key);
gboolean keymap_mafw_key_is_writable(gchar *mafw_key);
InfoKeyTable *keymap_get_info_key_table(void);
GHashTable *keymap_build_tracker_types_map(void);

#endif
