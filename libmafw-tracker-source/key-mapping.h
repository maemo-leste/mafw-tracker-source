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

gchar *keymap_mafw_key_to_tracker_key(const gchar *mafw_key,
				      ServiceType service);
inline gboolean keymap_mafw_key_supported_in_tracker(const gchar *mafw_key);
gboolean keymap_mafw_key_is_writable(gchar *mafw_key);
GHashTable *keymap_build_mafw_to_tracker_keys_map(void);
GHashTable *keymap_build_tracker_types_map(void);
gchar **keymap_mafw_keys_to_tracker_keys(gchar **mafw_keys,
					 ServiceType service);

#endif
