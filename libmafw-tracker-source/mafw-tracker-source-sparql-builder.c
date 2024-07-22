/*
 * mafw-tracker-source-sparql-builder.c
 *
 * Copyright (C) 2024 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "mafw-tracker-source-sparql-builder.h"

#include <stdio.h>

struct _MafwTrackerSourceSparqlBuilder
{
  GObject parent;
};

struct _MafwTrackerSourceSparqlBuilderClass
{
  GObjectClass parent_class;
};

typedef struct _MafwTrackerSourceSparqlBuilderPrivate \
    MafwTrackerSourceSparqlBuilderPrivate;

struct _MafwTrackerSourceSparqlBuilderPrivate
{
  unsigned int val_idx;
  char *val_buffer;
  unsigned int var_idx;
  char *var_buffer;
  GHashTable *values;
};

G_DEFINE_TYPE_WITH_PRIVATE(
  MafwTrackerSourceSparqlBuilder,
  mafw_tracker_source_sparql_builder,
  G_TYPE_OBJECT
);

#define PRIVATE(builder) \
  mafw_tracker_source_sparql_builder_get_instance_private( \
    (MafwTrackerSourceSparqlBuilder *)(builder))

static void
mafw_tracker_source_sparql_builder_dispose(GObject *object)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(object);

  g_hash_table_remove_all(priv->values);

  G_OBJECT_CLASS(mafw_tracker_source_sparql_builder_parent_class)->
      dispose(object);
}

static void
mafw_tracker_source_sparql_builder_finalize(GObject *object)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(object);

  g_hash_table_destroy(priv->values);
  g_free(priv->val_buffer);
  g_free(priv->var_buffer);

  G_OBJECT_CLASS(mafw_tracker_source_sparql_builder_parent_class)->
      finalize(object);
}

static void
mafw_tracker_source_sparql_builder_class_init(
    MafwTrackerSourceSparqlBuilderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);

  object_class->dispose = mafw_tracker_source_sparql_builder_dispose;
  object_class->finalize = mafw_tracker_source_sparql_builder_finalize;
}

static void
mafw_tracker_source_sparql_builder_init(MafwTrackerSourceSparqlBuilder *self)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(self);

  priv->val_buffer = g_new(char, 13);
  priv->var_buffer = g_new(char, 13);
  priv->val_idx = 0;
  priv->var_idx = 0;
  priv->values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

MafwTrackerSourceSparqlBuilder *
mafw_tracker_source_sparql_builder_new()
{
  return g_object_new(MAFW_TRACKER_SOURCE_TYPE_SPARQL_BUILDER, NULL);
}

static const gchar *
_next_var_id(MafwTrackerSourceSparqlBuilder *builder)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(builder);

  sprintf(priv->var_buffer, "?v%u", priv->var_idx++);

  return priv->var_buffer;
}

static const gchar *
_next_val_id(MafwTrackerSourceSparqlBuilder *builder)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(builder);

  sprintf(priv->val_buffer, "_%u", priv->val_idx++);

  return priv->val_buffer;
}

static void _add_value(MafwTrackerSourceSparqlBuilder *builder,
                       const gchar *id, const gchar *value)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(builder);

  g_hash_table_insert(priv->values, g_strdup(id), g_strdup(value));
}

gchar *
mafw_tracker_source_sparql_create_query_filter(
    MafwTrackerSourceSparqlBuilder *builder,
    const char *query,
    const char *value)
{
  gchar *filter;

  if (*value)
  {
    const gchar *id = _next_val_id(builder);

    _add_value(builder, id, value);
    filter = g_strdup_printf(" . %s ~%s", query, id);
  }
  else
  {
    const gchar *id = _next_var_id(builder);

    filter = g_strdup_printf(
          " . OPTIONAL {%s %s} . FILTER(%s='' || !bound(%s))",
          query, id, id, id);
  }

  return filter;
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

static void
_bind_values(MafwTrackerSourceSparqlBuilder *builder,
             TrackerSparqlStatement *stmt)
{
  MafwTrackerSourceSparqlBuilderPrivate *priv = PRIVATE(builder);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, priv->values);

  while (g_hash_table_iter_next(&iter, &key, &value))
    tracker_sparql_statement_bind_string(stmt, key, value);
}

TrackerSparqlStatement *
mafw_tracker_source_sparql_meta(MafwTrackerSourceSparqlBuilder *builder,
                                TrackerSparqlConnection *tc,
                                TrackerObjectType type,
                                gchar *const *uris,
                                gchar *const *fields,
                                int max_fields)
{
  TrackerSparqlStatement *stmt;
  GString *sparql_select;
  GString *sparql_where;
  guint i;
  gchar *sparql;
  gchar *uri_var = NULL;

  sparql_select = g_string_new("SELECT");
  sparql_where = g_string_new(" { ");

  g_string_append(sparql_where, _get_service(type));

  if (uris)
  {
    uri_var = g_strdup(_next_var_id(builder));
    g_string_append_printf(sparql_select, " %s", uri_var);
    g_string_append_printf(sparql_where, " . ?o nie:url %s", uri_var);
  }

  for (i = 0; i < g_strv_length((gchar **)fields) && i < max_fields; i++)
  {
    const gchar *var = _next_var_id(builder);

    g_string_append_printf(sparql_select, " %s", var);
    g_string_append_printf(sparql_where, " . OPTIONAL{%s %s}", fields[i], var);
  }

  if (uris)
  {
    GString *sparql_in = g_string_new(NULL);
    gchar *const *uri;

    g_string_append_printf(sparql_in, " . FILTER(%s IN(", uri_var);

    for (uri = uris; *uri; uri++)
    {
      const gchar *id = _next_val_id(builder);

      _add_value(builder, id, *uri);
      g_string_append(sparql_in, "~");
      g_string_append(sparql_in, id);

      if (*(uri + 1))
        g_string_append(sparql_in, ",");
    }

    g_string_append(sparql_where, sparql_in->str);
    g_string_append(sparql_where, "))");
    g_string_free(sparql_in, TRUE);
    g_free(uri_var);
  }

  sparql = g_strconcat(sparql_select->str,
                       " WHERE ", sparql_where->str, " }", NULL);
  g_string_free(sparql_select, TRUE);
  g_string_free(sparql_where, TRUE);

  g_debug("Created metadata sparql '%s'", sparql);

  stmt = tracker_sparql_connection_query_statement(tc, sparql, NULL, NULL);
  _bind_values(builder, stmt);

  g_free(sparql);

  return stmt;
}

TrackerSparqlStatement *
mafw_tracker_source_sparql_select(MafwTrackerSourceSparqlBuilder *builder,
                                  TrackerSparqlConnection *tc,
                                  TrackerObjectType type,
                                  const gchar *uri)
{
  const char *st = _get_service(type);
  const gchar *id = _next_val_id(builder);
  TrackerSparqlStatement *stmt;
  gchar *sparql;


  _add_value(builder, id, uri);
  sparql = g_strdup_printf("SELECT * WHERE {%s ; nie:url ~%s}", st, id);

  g_debug("Created select URI sparql '%s'", sparql);

  stmt = tracker_sparql_connection_query_statement(tc, sparql, NULL, NULL);
  _bind_values(builder, stmt);

  g_free(sparql);

  return stmt;
}

/*FIXME -  tracker 2 does not support prepared statements for update queries */
gchar *
mafw_tracker_source_sparql_update(MafwTrackerSourceSparqlBuilder *builder,
                                  TrackerObjectType type,
                                  const gchar *uri,
                                  gchar **keys,
                                  gchar **values)
{
  gchar *sql;
  gchar *escaped_uri = tracker_sparql_escape_string(uri);
  GString *sparql_delete = g_string_new("");
  GString *sparql_insert = g_string_new("");
  GString *sparql_where = g_string_new("");

  for (int i = 0; i < g_strv_length(keys); i++)
  {
    const gchar *var = _next_var_id(builder);

    g_string_append_printf(sparql_delete, " %s %s .", keys[i], var);
    g_string_append_printf(sparql_insert, " %s '%s' .", keys[i], values[i]);
    g_string_append_printf(sparql_where, " . OPTIONAL {%s %s}", keys[i], var);
  }

  sql = g_strdup_printf(
        "DELETE {%s} INSERT {%s} WHERE {%s . ?o nie:url '%s'%s}",
        sparql_delete->str, sparql_insert->str, _get_service(type), escaped_uri,
        sparql_where->str);

  g_string_free(sparql_delete, TRUE);
  g_string_free(sparql_insert, TRUE);
  g_string_free(sparql_where, TRUE);

  g_debug("Created update sparql '%s'", sql);

  g_free(escaped_uri);

  return sql;
}

gchar *
mafw_tracker_source_sparql_create_filter_from_category(
    MafwTrackerSourceSparqlBuilder *builder,
    const gchar *genre,
    const gchar *artist,
    const gchar *album,
    const gchar *user_filter)
{
  gint parts_of_filter = 1;
  gint i;
  gchar *rdf_filter = NULL;
  gchar **filters = NULL;
  gchar *value = NULL;

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
    value = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_GENRE,
                                              TRACKER_TYPE_MUSIC, genre);
    filters[i] = mafw_tracker_source_sparql_create_query_filter(
          builder, SPARQL_QUERY_BY_GENRE, value);
    g_free(value);
    i++;
  }

  if (artist)
  {
    value = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_ARTIST,
                                              TRACKER_TYPE_MUSIC, artist);
    filters[i] = mafw_tracker_source_sparql_create_query_filter(
          builder, SPARQL_QUERY_BY_ARTIST, value);
    g_free(value);
    i++;
  }

  if (album)
  {
    value = util_get_tracker_value_for_filter(MAFW_METADATA_KEY_ALBUM,
                                              TRACKER_TYPE_MUSIC, album);
    filters[i] = mafw_tracker_source_sparql_create_query_filter(
          builder, SPARQL_QUERY_BY_ALBUM, value);
    g_free(value);
    i++;
  }

  rdf_filter = util_build_complex_rdf_filter(filters, user_filter);

  g_strfreev(filters);

  return rdf_filter;
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

TrackerSparqlStatement *
mafw_tracker_source_sparql_create(MafwTrackerSourceSparqlBuilder *builder,
                                  TrackerSparqlConnection *tc,
                                  TrackerObjectType type,
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
  TrackerSparqlStatement *stmt;
  GString *sparql_select = g_string_new("SELECT");
  GString *sparql_where = g_string_new("WHERE { ");
  GString *sparql_group = g_string_new(NULL);
  guint i;
  gchar *sparql;

  g_string_append(sparql_where, _get_service(type));

  for (i = 0; i < g_strv_length(fields); i++)
  {
    const gchar *var = _next_var_id(builder);

    g_string_append_printf(sparql_select, " %s", var);
    g_string_append_printf(sparql_where, " . OPTIONAL {%s %s}", fields[i], var);

    if (unique)
      g_string_append_printf(sparql_group, " GROUP BY %s", var);
  }

  if (aggregates)
  {
    for (i = 0; i < g_strv_length(aggregates); i++)
    {
      const gchar *var = _next_var_id(builder);

      if (!strcmp(aggregates[i], AGGREGATED_TYPE_CONCAT))
      {
        gchar *concat = _group_concat(var);

        g_string_append_printf(sparql_select, " %s",concat);
        g_free(concat);
      }
      else if (!strcmp(aggregates[i], AGGREGATED_TYPE_COUNT))
      {
        if (!strcmp(aggregate_fields[i], "*"))
          g_string_append_printf(sparql_select, " %s(*)", aggregates[i]);
        else
        {
          g_string_append_printf(
                sparql_select, " %s(DISTINCT %s)", aggregates[i], var);
        }
      }
      else
        g_string_append_printf(sparql_select, " %s(%s)", aggregates[i], var);

      if (strcmp(aggregates[i], AGGREGATED_TYPE_COUNT) ||
          strcmp(aggregate_fields[i], "*"))
      {
        g_string_append_printf(sparql_where, " . %s %s",
                               aggregate_fields[i], var);
      }
    }
  }

  if (limit)
    g_string_append_printf(sparql_group, " LIMIT %u OFFSET %u", limit, offset);

  if (condition)
    g_string_append_printf(sparql_where, "%s", condition);

  sparql = g_strconcat(sparql_select->str, " ",
                       sparql_where->str, " }",
                       sparql_group->str, NULL);

  g_string_free(sparql_select, TRUE);
  g_string_free(sparql_where, TRUE);
  g_string_free(sparql_group, TRUE);

  g_debug("Created sparql '%s'", sparql);

  stmt = tracker_sparql_connection_query_statement(tc, sparql, NULL, NULL);
  _bind_values(builder, stmt);

  g_free(sparql);

  return stmt;
}
