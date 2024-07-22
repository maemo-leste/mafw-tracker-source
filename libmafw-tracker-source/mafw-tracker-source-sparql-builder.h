/*
 * mafw-tracker-source-sparql-builder.h
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

#ifndef __MAFW_TRACKER_SOURCE_SPARQL_BUILDER_H__
#define __MAFW_TRACKER_SOURCE_SPARQL_BUILDER_H__

#include <glib-object.h>
#include <glib.h>
#include <tracker-sparql.h>

#include "util.h"

G_BEGIN_DECLS

typedef struct _MafwTrackerSourceSparqlBuilder MafwTrackerSourceSparqlBuilder;
typedef struct _MafwTrackerSourceSparqlBuilderClass MafwTrackerSourceSparqlBuilderClass;

#define MAFW_TRACKER_SOURCE_TYPE_SPARQL_BUILDER (mafw_tracker_source_sparql_builder_get_type())

#define MAFW_TRACKER_SOURCE_SPARQL_BUILDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), MAFW_TRACKER_SOURCE_TYPE_HOME_SPARQL_BUILDER, MafwTrackerSourceSparqlBuilder))

#define MAFW_TRACKER_SOURCE_SPARQL_BUILDER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TRACKER_SOURCE_TYPE_HOME_SPARQL_BUILDER, MafwTrackerSourceSparqlBuilderClass))

#define MAFW_TRACKER_SOURCE_IS_HOME_SPARQL_BUILDER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAFW_TRACKER_SOURCE_TYPE_HOME_SPARQL_BUILDER))

#define MAFW_TRACKER_SOURCE_IS_HOME_SPARQL_BUILDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), MAFW_TRACKER_SOURCE_TYPE_HOME_SPARQL_BUILDER))

#define MAFW_TRACKER_SOURCE_SPARQL_BUILDER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), MAFW_TRACKER_SOURCE_TYPE_HOME_SPARQL_BUILDER, MafwTrackerSourceSparqlBuilderClass))

GType
mafw_tracker_source_sparql_builder_get_type() G_GNUC_CONST;

MafwTrackerSourceSparqlBuilder *
mafw_tracker_source_sparql_builder_new();

TrackerSparqlStatement *
mafw_tracker_source_sparql_meta(MafwTrackerSourceSparqlBuilder *builder,
                                TrackerSparqlConnection *tc,
                                TrackerObjectType type,
                                gchar *const *uris,
                                gchar *const *fields,
                                int max_fields);

TrackerSparqlStatement *
mafw_tracker_source_sparql_select(MafwTrackerSourceSparqlBuilder *builder,
                                  TrackerSparqlConnection *tc,
                                  TrackerObjectType type,
                                  const gchar *uri);

gchar *
mafw_tracker_source_sparql_update(MafwTrackerSourceSparqlBuilder *builder,
                                  TrackerObjectType type,
                                  const gchar *uri,
                                  gchar **keys,
                                  gchar **values);

gchar *
mafw_tracker_source_sparql_create_filter_from_category(
    MafwTrackerSourceSparqlBuilder *builder,
    const gchar *genre,
    const gchar *artist,
    const gchar *album,
    const gchar *user_filter);

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
                                  gboolean desc);

gchar *
mafw_tracker_source_sparql_create_query_filter(
    MafwTrackerSourceSparqlBuilder *builder,
    const char *query,
    const char *value);

G_END_DECLS

#endif /* __MAFW_TRACKER_SOURCE_SPARQL_BUILDER_H__ */
