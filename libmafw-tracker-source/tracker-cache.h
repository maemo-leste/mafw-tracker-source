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

#ifndef __MAFW_TRACKER_CACHE_H__
#define __MAFW_TRACKER_CACHE_H__

#include <glib.h>
#include <glib-object.h>
#include <tracker.h>

/* How the key should be managed in the cache */
enum TrackerCacheKeyType {
        /* The value is precomputed/fixed */
        TRACKER_CACHE_KEY_TYPE_COMPUTED,
        /* The value must be obtained from tracker */
        TRACKER_CACHE_KEY_TYPE_TRACKER,
        /* The value must be obtained from hildon thumbnailer */
        TRACKER_CACHE_KEY_TYPE_THUMBNAILER,
        /* The value is obtained from other key */
        TRACKER_CACHE_KEY_TYPE_DERIVED,
};

/* How results from tracker where obtained */
enum TrackerCacheResultType {
        /* Results were obtained with tracker_search_query_async */
        TRACKER_CACHE_RESULT_TYPE_QUERY,
        /* Results were obtained with
         * tracker_metadata_get_unique_values_with_count_and_sum_async */
        TRACKER_CACHE_RESULT_TYPE_UNIQUE_COUNT_SUM,
        /* Results were obtained with
         * tracker_metadata_get_unique_values_with_concat_count_and_sum_async */
        TRACKER_CACHE_RESULT_TYPE_UNIQUE_CONCAT_COUNT_SUM,
        /* Results were obtained with tracker_get_metadata */
        TRACKER_CACHE_RESULT_TYPE_GET_METADATA,
};

/* The value of the cached key */
typedef struct TrackerCacheValue {
        /* How to manage it */
        enum TrackerCacheKeyType key_type;
        /* Has the user asked for this key? */
        gboolean user_key;
        union {
                /* Pre-computed/fixed keys */
                GValue value;
                /* Obtained from tracker */
                gint tracker_index;
                /* Derived from other key */
                gchar *key_derived_from;
        };
} TrackerCacheValue;

/* The cache where to store the values */
typedef struct TrackerCache {
        /* How many keys are to query tracker */
        gint last_tracker_index;
        /* How results from tracker have been obtained */
        enum TrackerCacheResultType result_type;
        /* The service used with tracker */
        ServiceType service;
        /* Values returned by tracker */
        GPtrArray *tracker_results;
        /* The list of keys */
        GHashTable *cache;
} TrackerCache;


TrackerCache *tracker_cache_new(ServiceType service,
                                enum TrackerCacheResultType result_type);

void tracker_cache_free(TrackerCache *cache);

void tracker_cache_key_add_precomputed(TrackerCache *cache,
                                       const gchar *key,
                                       gboolean user_key,
                                       const GValue *value);

void tracker_cache_key_add_precomputed_string(TrackerCache *cache,
                                              const gchar *key,
                                              gboolean user_key,
                                              const gchar *value);

void tracker_cache_key_add_precomputed_int(TrackerCache *cache,
                                           const gchar *key,
                                           gboolean user_key,
                                           gint value);

void tracker_cache_key_add_derived(TrackerCache *cache,
                                   const gchar *key,
                                   gboolean user_key,
                                   gchar *source_key);

void tracker_cache_key_add(TrackerCache *cache,
                           const gchar *key,
                           gboolean user_key);

void tracker_cache_key_add_several(TrackerCache *cache,
                                   gchar **keys,
                                   gboolean user_keys);

void tracker_cache_key_add_unique(TrackerCache *cache,
                                  gchar **unique_keys);

void tracker_cache_key_add_concat(TrackerCache *cache,
                                  const gchar *concat_key);

gchar **tracker_cache_keys_get_tracker(TrackerCache *cache);

gchar **tracker_cache_keys_get_user(TrackerCache *cache);

void tracker_cache_values_add_results(TrackerCache *cache,
                                      GPtrArray *tracker_results);

void tracker_cache_values_add_result(TrackerCache *cache,
                                     gchar **tracker_result);

const GPtrArray *tracker_cache_values_get_results(TrackerCache *cache);

GValue *tracker_cache_value_get(TrackerCache *cache,
                                const gchar *key,
                                gint index);

GList *tracker_cache_build_metadata(TrackerCache *cache);

GHashTable *tracker_cache_build_metadata_aggregated(TrackerCache *cache,
                                                    gboolean count_childcount);

#endif /* __MAFW_TRACKER_CACHE_H__ */
