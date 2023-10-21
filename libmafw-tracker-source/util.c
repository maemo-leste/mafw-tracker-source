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
  return tracker_sparql_escape_string(value);
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

static const gchar *
var_id()
{
  static unsigned int id = 0;
  static gchar var[7];

  if (id > 10000)
    id = 0;

  sprintf(var, "?v%04u", id++);

  return var;
}

static gchar *
_get_expression(const MafwFilter *filter, TrackerObjectType type,
                const gchar *tracker_key, const gchar *var)
{
  gchar *expr;
  const char *c;

  gchar *tracker_value;
  gboolean is_cont = FALSE;

  g_assert(MAFW_FILTER_IS_SIMPLE(filter));

  switch (filter->type)
  {
    case mafw_f_eq:
    {
      c = "=";
      break;
    }
    case mafw_f_lt:
    {
      c = "<";
      break;
    }
    case mafw_f_gt:
    {
      c = ">";
      break;
    }
    case mafw_f_approx:
    {
      is_cont = TRUE;
      break;
    }
    case mafw_f_exists:
    {
      g_warning("mafw_f_exists not implemented");
      return NULL;
    }
    default:
      g_warning("Unknown filter type");
      return NULL;
  }

  tracker_value = util_get_tracker_value_for_filter(filter->key, type,
                                                    filter->value);

  if (is_cont)
    expr = g_strdup_printf("CONTAINS(%s,'%s')", var, tracker_value);
  else
    expr = g_strdup_printf("(%s%s'%s')", var, c, tracker_value);

  g_free(tracker_value);

  return expr;
}

static gboolean
_filter_to_sparql(const MafwFilter *filter,
                  GString *k, GString *e)
{
  gboolean ret = TRUE;
  gchar *key;

  if (MAFW_FILTER_IS_SIMPLE(filter))
  {
    gchar *expr;
    const gchar *var = var_id();

    key = keymap_mafw_key_to_tracker_key(
        filter->key, TRACKER_TYPE_MUSIC);
    expr = _get_expression(filter, TRACKER_TYPE_MUSIC, key, var);

    if (expr)
    {
      g_string_append_printf(k, " . %s %s", key, var);
      g_string_append_printf(e, "(%s)", expr);

      g_free(expr);
    }
    else
      ret = FALSE;

    g_free(key);
  }
  else
  {
    MafwFilter **parts;

    for (parts = filter->parts; *parts;)
    {
      if ((filter->type == mafw_f_and) || (filter->type == mafw_f_or))
      {
        while (*parts != NULL)
        {
          GString *_k = g_string_new("");
          GString *_e = g_string_new("");

          ret = _filter_to_sparql(*parts, _k, _e);

          if (ret)
          {
            const char *op;

            if (filter->type == mafw_f_and)
              op = " && ";
            else
              op = " || ";

            g_string_append(k, _k->str);

            if (e->len)
              g_string_append(e, op);

            g_string_append(e, _e->str);
          }

          g_string_free(_k, TRUE);
          g_string_free(_e, TRUE);

          if (!ret)
            break;

          parts++;
        }
      }
      else if (filter->type == mafw_f_not)
      {
        GString *_k = g_string_new("");
        GString *_e = g_string_new("");

        ret = _filter_to_sparql(*parts, _k, _e);

        if (ret)
        {
          g_string_append(k, _k->str);
          g_string_append(e, "!");
          g_string_append(e, _e->str);
        }

        g_string_free(_k, TRUE);
        g_string_free(_e, TRUE);

        if (!ret)
          break;

        parts++;
      }
      else
      {
        g_warning("Filter type not implemented");
        ret = FALSE;
        break;
      }
    }
  }

  return ret;
}

gboolean
util_mafw_filter_to_sparql(const MafwFilter *filter, GString *p)
{
  gboolean ret;
  GString *k = g_string_new("");
  GString *e = g_string_new("");

  ret = _filter_to_sparql(filter, k, e);

  if (ret)
    g_string_append_printf(p, "%s . FILTER(%s)", k->str, e->str);

  g_string_free(k, TRUE);
  g_string_free(e, TRUE);

  return ret;
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
util_create_filter_from_category(const gchar *genre,
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

  if (genre)
  {
    escaped_genre = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_GENRE,
                                                      TRACKER_TYPE_MUSIC,
                                                      genre);
    filters[i] = g_strdup_printf(SPARQL_QUERY_BY_GENRE, escaped_genre);
    g_free(escaped_genre);
    i++;
  }

  if (artist)
  {
    escaped_artist = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_ARTIST,
                                                       TRACKER_TYPE_MUSIC,
                                                       artist);
    filters[i] = g_strdup_printf(SPARQL_QUERY_BY_ARTIST, escaped_artist);
    g_free(escaped_artist);
    i++;
  }

  if (album)
  {
    escaped_album = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_ALBUM,
                                                      TRACKER_TYPE_MUSIC,
                                                      album);
    filters[i] = g_strdup_printf(SPARQL_QUERY_BY_ALBUM, escaped_album);
    g_free(escaped_album);
    i++;
  }

  rdf_filter = util_build_complex_rdf_filter(filters, user_filter);

  g_strfreev(filters);

  return rdf_filter;
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

static gchar *
_group_concat(const gchar *v)
{
  /* We do all this voodoo magic because SQLITE GROUP_CONCAT() with
   * custom separator is broken, so we cannot replace the default ','
   * separator with '|', when calling GROUP_CONCAT. Lets hope there is
   * no artist in this world that will name her album with something
   * containing the string below. Though, given the state the music is
   * nowadays...
   */

#define SEP "!@_SQLITE_GROUP_CONCAT_IS_BROKEN_@!"

  return g_strdup_printf("REPLACE(REPLACE("
                         AGGREGATED_TYPE_CONCAT "(DISTINCT CONCAT(%s, '"
                         SEP "')),'" SEP ",','|'), '" SEP "', '')", v);

#undef SEP
}

static const char *
_get_service(TrackerObjectType type)
{
  switch (type)
  {
    case TRACKER_TYPE_PLAYLIST:
    {
      return "?o a nmm:Playlist";
    }
    case TRACKER_TYPE_VIDEO:
    {
      return "?o a nmm:Video";
    }
    default:
      return "?o a nmm:MusicPiece";
  }
}

gchar *
util_build_sparql(TrackerObjectType type,
                  gboolean unique,
                  gchar **fields,
                  const gchar *condition,
                  gchar **aggregates,
                  gchar **aggregate_fields,
                  guint offset,
                  guint limit,
                  gchar **tracker_sort_keys,
                  gboolean desc)
{
  GString *sql_select;
  GString *sql_where;
  GString *sql_group;
  guint i;
  gchar *sql;

  sql_select = g_string_new("SELECT");
  sql_where = g_string_new("WHERE { ");
  sql_group = g_string_new(NULL);

  g_string_append(sql_where, _get_service(type));

  for (i = 0; i < g_strv_length(fields); i++)
  {
    const gchar *var = var_id();

    g_string_append_printf(sql_select, " %s", var);
    g_string_append_printf(sql_where, " . OPTIONAL {%s %s}", fields[i], var);

    if (unique)
      g_string_append_printf(sql_group, " GROUP BY %s", var);
  }

  if (aggregates)
  {
    for (i = 0; i < g_strv_length(aggregates); i++)
    {
      const gchar *var = var_id();

      if (!strcmp(aggregates[i], AGGREGATED_TYPE_CONCAT))
      {
        gchar *concat = _group_concat(var);

        g_string_append_printf(sql_select, " %s",concat);
        g_free(concat);
      }
      else if (!strcmp(aggregates[i], AGGREGATED_TYPE_COUNT))
      {
        if (!strcmp(aggregate_fields[i], "*"))
          g_string_append_printf(sql_select, " %s(*)", aggregates[i]);
        else
        {
          g_string_append_printf(
                sql_select, " %s(DISTINCT %s)", aggregates[i], var);
        }
      }
      else
        g_string_append_printf(sql_select, " %s(%s)", aggregates[i], var);

      if (strcmp(aggregates[i], AGGREGATED_TYPE_COUNT) ||
          strcmp(aggregate_fields[i], "*"))
      {
        g_string_append_printf(sql_where, " . %s %s", aggregate_fields[i], var);
      }
    }
  }

  if (limit)
    g_string_append_printf(sql_group, " LIMIT %u OFFSET %u", limit, offset);

  if (condition)
    g_string_append_printf(sql_where, "%s", condition);

  sql = g_strconcat(
        sql_select->str, " ", sql_where->str, " }", sql_group->str, NULL);

  g_string_free(sql_select, TRUE);
  g_string_free(sql_where, TRUE);
  g_string_free(sql_group, TRUE);

  g_debug("Created sparql '%s'", sql);

  return sql;
}

gchar *
util_build_meta_sparql(TrackerObjectType type,
                       gchar **uris,
                       gchar **fields,
                       int max_fields)
{
  GString *sql_select;
  GString *sql_where;
  guint i;
  gchar *sql;

  sql_select = g_string_new("SELECT");
  sql_where = g_string_new(" { ");

  g_string_append(sql_where, _get_service(type));

  for (i = 0; i < g_strv_length(fields) && i < max_fields; i++)
  {
    const gchar *var = var_id();

    g_string_append_printf(sql_select, " %s", var);
    g_string_append_printf(sql_where, " . OPTIONAL{%s %s}", fields[i], var);
  }

  if (uris)
  {
    gchar *tmp;
    gchar **uri;

    g_string_append(sql_where, " . ?o nie:url '%s' }");

    tmp = g_string_free(sql_where, FALSE);
    sql_where = g_string_new("{");

    for (uri = uris; *uri; uri++)
    {
      gchar *escaped_uri = tracker_sparql_escape_string(*uri);

      g_string_append_printf(sql_where, tmp, escaped_uri);
      g_free(escaped_uri);

      if (*(uri + 1))
        g_string_append(sql_where, " UNION");
    }

    g_free(tmp);
  }

  sql = g_strconcat(sql_select->str, " WHERE ", sql_where->str, " }", NULL);
  g_string_free(sql_select, TRUE);
  g_string_free(sql_where, TRUE);

  g_debug("Created metadata sparql '%s'", sql);

  return sql;
}

gchar *
util_build_update_sparql(TrackerObjectType type,
                         const gchar *uri,
                         gchar **keys,
                         gchar **values,
                         gboolean select)
{
  gchar *sql;
  gchar *escaped_uri;

  escaped_uri = tracker_sparql_escape_string(uri);

  if (select)
  {
    sql = g_strdup_printf("SELECT * WHERE {%s ; nie:url '%s'}",
                          _get_service(type), escaped_uri);

    g_debug("Created update select sparql '%s'", sql);
  }
  else
  {
    GString *sql_delete;
    GString *sql_insert;
    GString *sql_where;

    sql_where = g_string_new("");
    sql_delete = g_string_new("");
    sql_insert = g_string_new("");

    for (int i = 0; i < g_strv_length(keys); i++)
    {
      const gchar *var = var_id();

      g_string_append_printf(sql_delete, " %s %s .", keys[i], var);
      g_string_append_printf(sql_insert, " %s '%s' .", keys[i], values[i]);
      g_string_append_printf(sql_where, " . OPTIONAL {%s %s}", keys[i], var);
    }

    sql = g_strdup_printf(
        "DELETE {%s} INSERT {%s} WHERE {%s . ?o nie:url '%s'%s}",
        sql_delete->str, sql_insert->str, _get_service(type), escaped_uri,
        sql_where->str);

    g_string_free(sql_delete, TRUE);
    g_string_free(sql_insert, TRUE);
    g_string_free(sql_where, TRUE);

    g_debug("Created update sparql '%s'", sql);
  }

  g_free(escaped_uri);

  return sql;
}
