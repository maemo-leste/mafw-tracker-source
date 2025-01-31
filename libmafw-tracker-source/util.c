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
#include "util.h"
#include <libmafw/mafw-source.h>
#include <libmafw/mafw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <totem-pl-parser.h>

/* ------------------------- Private API ------------------------- */

/* ------------------------- Public API ------------------------- */

void
util_gvalue_free(GValue *value)
{
  if (value)
  {
    g_value_unset(value);
    g_free(value);
  }
}

gchar *
util_get_tracker_value_for_filter(const gchar *mafw_key,
                                  TrackerObjectType type,
                                  const gchar *value)
{
  MetadataKey *metadata_key;
  TrackerKey *tracker_key;

  if (!value)
    return NULL;

  if (!mafw_key)
    return g_strdup(value);

  metadata_key = keymap_get_metadata(mafw_key);

  if (!metadata_key)
    return g_strdup(value);

  tracker_key = keymap_get_tracker_info(mafw_key, type);

  if (!tracker_key)
    return g_strdup(value);

  /* If the tracker's type is a date, convert the value from epoch to iso
   * 8601 */
  if (tracker_key->value_type == G_TYPE_DATE)
    return util_epoch_to_iso8601(atol(value));

  /* If the key does not need special handling, just use
     the user provided value in the filter without any modifications */
  return g_strdup(value);
}

gchar *
util_str_replace(gchar *str, gchar *old, gchar *new)
{
  GString *g_str = g_string_new(str);
  const char *cur = g_str->str;
  gint oldlen = strlen(old);
  gint newlen = strlen(new);

  while ((cur = strstr(cur, old)) != NULL)
  {
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
GList *
util_itemid_to_path(const gchar *item_id)
{
  GList *path = NULL;
  gchar **tokens;
  gchar *str;
  int i = 0;

  if (!item_id || (item_id[0] == '\0'))
    return path;

  /* Split using '/' as separator */
  tokens = g_strsplit(item_id, "/", -1);

  /* We will unescape all the elements. This is because our object ids
     include filename paths (which are escaped because we want to know
     if a '/' comes from a library category (i.e. music/artists) or it
     is part of the file URI */

  /* If there is only one element, put it alone in the GList */
  if (!tokens[0])
  {
    str = util_unescape_string(item_id);
    path = g_list_append(path, str);
  }
  else
  {
    /* Add each token to the GList */
    for (i = 0; tokens[i]; i++)
    {
      str = util_unescape_string(tokens[i]);
      path = g_list_append(path, str);
    }
  }

  g_strfreev(tokens);

  return path;
}

gchar *
util_unescape_string(const gchar *original)
{
  return g_uri_unescape_string(original, NULL);
}

#ifndef G_DEBUG_DISABLE
void
perf_elapsed_time_checkpoint(gchar *event)
{
  static GTimeVal elapsed_time = { 0 };
  static GTimeVal time_checkpoint = { 0 };
  GTimeVal t;
  GTimeVal checkpoint;

  if (!time_checkpoint.tv_sec)
    g_get_current_time(&time_checkpoint);

  /* Update elapsed time since last checkpoint */
  g_get_current_time(&t);
  checkpoint = t;
  t.tv_sec -= time_checkpoint.tv_sec;
  g_time_val_add(&t, -time_checkpoint.tv_usec);
  g_time_val_add(&elapsed_time, t.tv_sec * 1000000L + t.tv_usec);

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

gchar *
util_epoch_to_iso8601(glong epoch)
{
  GTimeVal timeval;

  timeval.tv_sec = epoch;
  timeval.tv_usec = 0;

  return g_time_val_to_iso8601(&timeval);
}

glong
util_iso8601_to_epoch(const gchar *iso_date)
{
  GTimeVal timeval;

  g_time_val_from_iso8601(iso_date, &timeval);
  return timeval.tv_sec;
}

gint
util_iso8601_to_year(const gchar *iso_date)
{
  g_autoptr(GDateTime) dt = g_date_time_new_from_iso8601(iso_date, NULL);

  if (dt)
    return g_date_time_get_year(dt);

  return 0;
}

gboolean
util_tracker_value_is_unknown(const gchar *value)
{
  return (value == NULL) || (*value == '\0');
}

gchar **
util_create_sort_keys_array(gint n, gchar *key1, ...)
{
  gchar **sort_keys;
  gchar *key;
  va_list args;
  gint i = 0;

  g_return_val_if_fail(n > 0, NULL);

  va_start(args, key1);

  sort_keys = g_new0(gchar *, n+1);
  sort_keys[i++] = g_strdup(key1);

  while (i < n)
  {
    key = va_arg(args, gchar *);
    sort_keys[i] = g_strdup(key);
    i++;
  }

  va_end(args);

  return sort_keys;
}

gchar *
util_build_complex_rdf_filter(gchar **filters,
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

  switch (len)
  {
    case 0:
    {
      cfilter = NULL;
      break;
    }
    case 1:
    {
      if (append_filter)
        cfilter = g_strdup(append_filter);
      else
        cfilter = g_strdup(filters[0]);

      break;
    }
    default:
    {
      join_filters = g_strjoinv(NULL, filters);

      if (append_filter)
        cfilter = g_strconcat(join_filters, append_filter, NULL);
      else
        cfilter = g_strdup(join_filters);

      g_free(join_filters);
      break;
    }
  }

  return cfilter;
}

void
util_sum_duration(gpointer data, gpointer user_data)
{
  GValue *gval;
  gint *total_sum = (gint *)user_data;
  GHashTable *metadata = (GHashTable *)data;

  gval = mafw_metadata_first(metadata, MAFW_METADATA_KEY_DURATION);

  if (gval)
    *total_sum += g_value_get_int(gval);
}

void
util_sum_count(gpointer data, gpointer user_data)
{
  GValue *gval;
  gint *total_sum = (gint *)user_data;
  GHashTable *metadata = (GHashTable *)data;

  gval = mafw_metadata_first(metadata, MAFW_METADATA_KEY_CHILDCOUNT_1);

  if (gval)
    *total_sum += g_value_get_int(gval);
}

static gchar *
filename_to_uri(const gchar *filename)
{
  gchar *uri = g_filename_to_uri(filename, NULL, NULL);

  if (!uri)
    uri = g_strdup(filename);

  return uri;
}

CategoryType
util_extract_category_info(const gchar *object_id,
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
  if (genre)
    *genre = NULL;

  if (artist)
    *artist = NULL;

  if (album)
    *album = NULL;

  if (clip)
    *clip = NULL;

  item_id = NULL;

  /* Skip the protocol part of the objectid to get the path-like part */
  mafw_source_split_objectid(object_id, NULL, &item_id);

  /* Wrong protocol */
  if (!item_id)
    return CATEGORY_ERROR;

  /* Split the path of the objectid into its components */
  path = util_itemid_to_path(item_id);
  g_free(item_id);

  /* Get category type */
  if (path != NULL)
  {
    path_name = get_data(path);
    path_length = g_list_length(path);

    if (g_strcasecmp(path_name, TRACKER_SOURCE_VIDEOS) == 0)
      category = CATEGORY_VIDEO;
    else if (g_strcasecmp(path_name, TRACKER_SOURCE_MUSIC) == 0)
    {
      next = g_list_next(path);

      if (!next)
        category = CATEGORY_MUSIC;
      else
      {
        path_name = get_data(next);

        if (!g_strcasecmp(path_name, TRACKER_SOURCE_PLAYLISTS))
          category = CATEGORY_MUSIC_PLAYLISTS;
        else if (!g_strcasecmp( path_name, TRACKER_SOURCE_SONGS))
          category = CATEGORY_MUSIC_SONGS;
        else if (!g_strcasecmp(path_name, TRACKER_SOURCE_GENRES))
        {
          category = CATEGORY_MUSIC_GENRES;
        }
        else if (!g_strcasecmp( path_name, TRACKER_SOURCE_ARTISTS))
          category = CATEGORY_MUSIC_ARTISTS;
        else if (!g_strcasecmp(path_name, TRACKER_SOURCE_ALBUMS))
          category = CATEGORY_MUSIC_ALBUMS;
        else
          category = CATEGORY_ERROR;
      }
    }
    else
      category = CATEGORY_ERROR;
  }
  else
    category = CATEGORY_ROOT;

  /* Get info */
  switch (category)
  {
    case CATEGORY_ROOT:
    case CATEGORY_MUSIC:
    case CATEGORY_ERROR:
    {
      /* No info to extract */
      break;
    }
    case CATEGORY_VIDEO:
    {
      if (path_length > 2)
        category = CATEGORY_ERROR;
      else if (clip && (path_length == 2))
        *clip = filename_to_uri(get_data(g_list_nth(path, 1)));

      break;
    }
    case CATEGORY_MUSIC_SONGS:
    {
      if (path_length > 3)
        category = CATEGORY_ERROR;
      else if (clip && (path_length == 3))
        *clip = filename_to_uri(get_data(g_list_nth(path, 2)));

      break;
    }
    case CATEGORY_MUSIC_PLAYLISTS:
    {
      switch (path_length)
      {
        case 3:
        {
          if (clip)
            *clip = filename_to_uri(get_data(g_list_nth(path, 2)));

          break;
        }
        case 2:
        {
          /* No info to extract */
          break;
        }
        default:
        {
          category = CATEGORY_ERROR;
          break;
        }
      }

      break;
    }
    case CATEGORY_MUSIC_ALBUMS:
    {
      switch (path_length)
      {
        case 4:
        {
          if (clip)
            *clip = filename_to_uri(get_data(g_list_nth(path, 3)));

          /* No break */
        }
        case 3:
        {
          if (album)
            *album = g_strdup(get_data(g_list_nth(path, 2)));

          break;
        }
        case 2:
        {
          /* No info to extract */
          break;
        }
        default:
        {
          category = CATEGORY_ERROR;
          break;
        }
      }

      break;
    }
    case CATEGORY_MUSIC_ARTISTS:
    {
      switch (path_length)
      {
        case 5:
        {
          if (clip)
            *clip = filename_to_uri(get_data(g_list_nth(path, 4)));

          /* No break */
        }
        case 4:
        {
          if (album)
            *album = g_strdup(get_data(g_list_nth(path, 3)));

          /* No break */
        }
        case 3:
        {
          if (artist)
            *artist = g_strdup(get_data(g_list_nth(path, 2)));

          /* No break */
        }
        case 2:
        {
          /* No info to extract */
          break;
        }
        default:
        {
          category = CATEGORY_ERROR;
          break;
        }
      }

      break;
    }
    case CATEGORY_MUSIC_GENRES:
    {
      switch (path_length)
      {
        case 6:
        {
          if (clip)
            *clip = filename_to_uri(get_data(g_list_nth(path, 5)));

          /* No break */
        }
        case 5:
        {
          if (album)
            *album = g_strdup(get_data(g_list_nth(path, 4)));

          /* No break */
        }
        case 4:
        {
          if (artist)
            *artist = g_strdup(get_data(g_list_nth(path, 3)));

          /* No break */
        }
        case 3:
        {
          if (genre)
            *genre = g_strdup(get_data(g_list_nth(path, 2)));

          break;
        }
        case 2:
        {
          /* No info to extract */
          break;
        }
        default:
        {
          category = CATEGORY_ERROR;
          break;
        }
      }

      break;
    }
  }

  /* Frees */
  g_list_foreach(path, (GFunc)g_free, NULL);
  g_list_free(path);

  return category;
}

static gboolean
_contains_key(const gchar **key_list, const gchar *key)
{
  int i = 0;

  if (key_list == NULL)
    return FALSE;

  while (key_list[i] != NULL)
  {
    if (g_strcmp0(key_list[i], key) == 0)
      return TRUE;

    i++;
  }

  return FALSE;
}

gboolean
util_is_duration_requested(const gchar **key_list)
{
  return _contains_key(key_list, MAFW_METADATA_KEY_DURATION);
}

/*
 * To calculate the playlist duration is needed when Tracker has returned
 * duration 0 and MAFW has never calculate it before.
 */
gboolean
util_calculate_playlist_duration_is_needed(GHashTable *pls_metadata)
{
  GValue *gval;
  gboolean calculate = FALSE;

  if (!pls_metadata)
    return FALSE;

  /* Check if Tracker returned duration 0. */
  gval = mafw_metadata_first(pls_metadata, MAFW_METADATA_KEY_DURATION);

  if (!gval)
    calculate = TRUE;

  return calculate;
}

gchar **
util_list_to_strv(GList *list)
{
  gchar **strv = NULL;
  gint i;

  strv = g_new0(gchar *, g_list_length(list) + 1);
  i = 0;

  while (list)
  {
    strv[i] = list->data;
    i++;
    list = list->next;
  }

  return strv;
}

gchar **
util_add_element_to_strv(gchar **array, const gchar *element)
{
  gchar **new_array;
  guint n = g_strv_length(array);

  new_array = g_try_renew(gchar *, array, n + 2);
  new_array[n] = g_strdup(element);
  new_array[n + 1] = NULL;

  return new_array;
}
