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

#ifndef __MAFW_ALBUMART_H__
#define __MAFW_ALBUMART_H__

#include <glib.h>


/* Supportted album art thumbnail sizes */
enum thumbnail_size {
	THUMBNAIL_CROPPED = 1,
	THUMBNAIL_NORMAL
};


gchar *albumart_get_album_art_uri(const gchar *album);

gchar *albumart_get_thumbnail_uri(const gchar *orig_file_uri,
                                   enum thumbnail_size size);

gboolean albumart_key_is_album_art(const gchar *key);
gboolean albumart_key_is_thumbnail(const gchar *key);

#endif
