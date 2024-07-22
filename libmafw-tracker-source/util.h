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

#ifndef _MAFW_TRACKER_SOURCE_UTIL_H_
#define _MAFW_TRACKER_SOURCE_UTIL_H_

#include <glib.h>
#include <libmafw/mafw.h>
#include <tracker-sparql.h>

#include "definitions.h"

#define IS_STRING_EMPTY(str) ((str) == NULL || (str)[0] == '\0')

typedef enum
{
  TRACKER_TYPE_MUSIC,
  TRACKER_TYPE_VIDEO,
  TRACKER_TYPE_PLAYLIST
} TrackerObjectType;

typedef enum
{
  CATEGORY_ROOT,
  CATEGORY_VIDEO,
  CATEGORY_MUSIC,
  CATEGORY_MUSIC_PLAYLISTS,
  CATEGORY_MUSIC_SONGS,
  CATEGORY_MUSIC_ALBUMS,
  CATEGORY_MUSIC_GENRES,
  CATEGORY_MUSIC_ARTISTS,
  CATEGORY_ERROR
} CategoryType;

#define AGGREGATED_TYPE_CONCAT "GROUP_CONCAT"
#define AGGREGATED_TYPE_COUNT  "COUNT"
#define AGGREGATED_TYPE_SUM    "SUM"

void
util_gvalue_free(GValue *value);

gchar *util_str_replace(gchar *str, gchar *old, gchar *new);
GList *
util_itemid_to_path(const gchar *item_id);
gchar *
util_unescape_string(const gchar *original);

/*
 * Returns the 'data' field of a GList node.
 */
inline static gchar *
get_data(const GList *list)
{
  return (gchar *)list->data;
}

#ifndef G_DEBUG_DISABLE
void
perf_elapsed_time_checkpoint(gchar *event);

#endif  /* G_DEBUG_DISABLE */

gchar *
util_epoch_to_iso8601(glong epoch);
glong
util_iso8601_to_epoch(const gchar *iso_date);
gboolean
util_mafw_filter_to_sparql(const MafwFilter *filter,
                           GString *p);
gchar *
util_get_tracker_value_for_filter(const gchar *mafw_key,
                                  TrackerObjectType type,
                                  const gchar *value);
gboolean
util_tracker_value_is_unknown(const gchar *value);
gchar **
util_create_sort_keys_array(gint n, gchar *key1, ...);

gchar *
util_build_complex_rdf_filter(gchar **filters,
                              const gchar *append_filter);
void
util_sum_duration(gpointer data, gpointer user_data);
void
util_sum_count(gpointer data, gpointer user_data);
CategoryType
util_extract_category_info(const gchar *object_id,
                           gchar **genre,
                           gchar **artist,
                           gchar **album,
                           gchar **clip);
gboolean
util_is_duration_requested(const gchar **key_list);
gboolean
util_calculate_playlist_duration_is_needed(GHashTable *pls_metadata);
void
util_remove_tracker_data_to_check_pls_duration(GHashTable *metadata,
                                               gchar **metadata_keys);

gchar **
util_list_to_strv(GList *list);
gchar **
util_add_element_to_strv(gchar **array, const gchar *element);

#endif
