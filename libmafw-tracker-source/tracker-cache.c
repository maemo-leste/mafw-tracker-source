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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <libmafw/mafw.h>
#include "key-mapping.h"
#include "tracker-cache.h"
#include "album-art.h"
#include "util.h"

#define SEVERAL_VALUES_DELIMITER "|"

/* ------------------------- Private API ------------------------- */

static void _replace_various_values(GValue *value)
{
        const gchar *str_value;
        /* Find if it contains several values */
        if (G_VALUE_HOLDS_STRING(value)) {
                str_value = g_value_get_string(value);

                /* Find for separator */
                if (str_value &&
                    strstr(str_value, SEVERAL_VALUES_DELIMITER)) {
                        g_value_set_string(value,
                                           MAFW_METADATA_VALUE_VARIOUS_VALUES);
                }
        }
}

static gboolean _value_is_allowed(GValue *value, const gchar *key)
{
        InfoKeyTable *t = keymap_get_info_key_table();
        MetadataKey *metadata_key;
        const gchar *str_value;

        metadata_key = g_hash_table_lookup(t->metadata_keys,
                                           key);

        if (!metadata_key) {
                return FALSE;
        }

        if (metadata_key->allowed_empty) {
                return TRUE;
        } else {
                /* Check if value is empty */
                if (!value) {
                        return FALSE;
                } else if (G_VALUE_HOLDS_STRING(value)) {
                        str_value = g_value_get_string(value);
                        return !IS_STRING_EMPTY(str_value);
                } else if (G_VALUE_HOLDS_INT(value)) {
                        return g_value_get_int(value) > 0;
                } else if (G_VALUE_HOLDS_LONG(value)) {
                        return g_value_get_long(value) > 0;
                } else if (G_VALUE_HOLDS_FLOAT(value)) {
                        return g_value_get_float(value) > 0;
                } else {
                        /* This is the case of storing a gboolean */
                        return TRUE;
                }
        }
}

static GValue *_get_title(TrackerCache *cache, gint index)
{
        GValue *value_title;
        GValue *value_uri;
        gchar *uri_title;
        gchar *filename;
        gchar *pathname;
        gchar *dot;
        const gchar *value_title_str;

        value_title = tracker_cache_value_get(cache,
                                              MAFW_METADATA_KEY_TITLE,
                                              index);

        /* If it is empty, then use the URI */
        value_title_str = g_value_get_string(value_title);
        if (IS_STRING_EMPTY(value_title_str) &&
            cache->result_type != TRACKER_CACHE_RESULT_TYPE_UNIQUE) {
                value_uri = tracker_cache_value_get(cache,
                                                MAFW_METADATA_KEY_URI,
                                                index);
                if (!value_uri) {
                        return value_title;
                }

                uri_title = (gchar *) g_value_get_string(value_uri);
                pathname = g_filename_from_uri(uri_title, NULL, NULL);

                /* Get filename */
                filename = g_path_get_basename(pathname);

                /* Remove extension */
                dot = g_strrstr(filename, ".");
                if (dot) {
                        *dot = '\0';
                }

                /* Use filename as the value */
                g_value_set_string(value_uri, filename);
                g_free(filename);
                g_free(pathname);
                util_gvalue_free(value_title);

                return value_uri;
        } else {
                return value_title;
        }
}

/* Inserts a key in the cache. 'pos' only makes sense when type is
 * TRACKER_CACHE_KEY_TYPE_TRACKER */
static void _insert_key(TrackerCache *cache,
                        const gchar *key,
                        enum TrackerCacheKeyType type,
                        gboolean user_key,
                        gint pos)
{
        TrackerCacheValue *cached_value;

        cached_value = g_new0(TrackerCacheValue, 1);
        cached_value->user_key = user_key;
        cached_value->key_type = type;
        if (type == TRACKER_CACHE_KEY_TYPE_TRACKER) {
                cached_value->tracker_index = pos;
        }
        g_hash_table_insert(cache->cache, g_strdup(key), cached_value);
}

static GValue *_get_value_album_art(TrackerCache *cache, gint index)
{
        GValue *album_value;
        GValue *return_value;
        const gchar *album;
        gchar *album_art_uri;
        gchar **singles;
        gint i;

        album_value = tracker_cache_value_get(cache,
                                              MAFW_METADATA_KEY_ALBUM,
                                              index);
        if (album_value) {
                album = g_value_get_string(album_value);
        } else {
                album = NULL;
        }

        if (!album ||
            *album == '\0') {
                util_gvalue_free(album_value);
                return NULL;
        }

        /* As album can be actually several albums, split them and
         * show the first available cover */
        singles = g_strsplit(album, SEVERAL_VALUES_DELIMITER, 0);
        util_gvalue_free(album_value);

        i=0;
        album_art_uri = NULL;
        while (singles[i] && album_art_uri == NULL) {
                album_art_uri = albumart_get_album_art_uri(singles[i]);
                i++;
        }
        g_strfreev(singles);

        if (album_art_uri) {
                return_value = g_new0(GValue, 1);
                g_value_init(return_value, G_TYPE_STRING);
                g_value_set_string(return_value, album_art_uri);
                g_free(album_art_uri);
                return return_value;
        } else {
                return NULL;
        }
}

static GValue *_get_value_thumbnail(TrackerCache *cache,
                                    const gchar *key,
                                    gint index)
{
        GValue *uri_value;
        GValue *return_value;
        const gchar *uri;
        gchar *th_uri;
        enum thumbnail_size size;

        /* For thumbnails, uri is needed */
        if (albumart_key_is_thumbnail(key)) {
                uri_value = tracker_cache_value_get(cache,
                                                    MAFW_METADATA_KEY_URI,
                                                    index);
        } else {
                uri_value =
                        tracker_cache_value_get(cache,
                                                MAFW_METADATA_KEY_ALBUM_ART_URI,
                                                index);
        }

        if (uri_value) {
                uri = g_value_get_string(uri_value);
        } else {
                uri = NULL;
        }
        if (uri) {
                /* Compute size requested */
                if (strcmp(key, MAFW_METADATA_KEY_ALBUM_ART_SMALL_URI) == 0 ||
                    strcmp(key, MAFW_METADATA_KEY_THUMBNAIL_SMALL_URI) == 0) {
                        size = THUMBNAIL_SMALL;
                } else if (strcmp(key,
                                  MAFW_METADATA_KEY_ALBUM_ART_MEDIUM_URI) == 0 ||
                           strcmp(key,
                                  MAFW_METADATA_KEY_THUMBNAIL_MEDIUM_URI) == 0) {
                        size = THUMBNAIL_MEDIUM;
                } else {
                        size = THUMBNAIL_LARGE;
                }
                return_value = g_new0(GValue, 1);
                g_value_init(return_value, G_TYPE_STRING);
                th_uri = albumart_get_thumbnail_uri(uri, size);
                g_value_set_string(return_value, th_uri);
                g_free(th_uri);
                util_gvalue_free(uri_value);
                return return_value;
        } else {
                return NULL;
        }
}

static GValue * _aggregate_key(TrackerCache *cache,
                               const gchar *key,
                               gboolean count_childcount)
{
        GValue *result;
        gint total = 0;
        GValue *value;
        gint i;
        gint results_length;

        results_length = cache->tracker_results? cache->tracker_results->len: 0;

        if (strcmp(key, MAFW_METADATA_KEY_CHILDCOUNT) == 0 &&
            count_childcount) {
                total = results_length;
        } else {
                for (i=0; i < results_length; i++) {
                        value = tracker_cache_value_get(cache, key, i);
                        if (value) {
                                total += g_value_get_int(value);
                                util_gvalue_free(value);
                        }
                }
        }

        result = g_new0(GValue, 1);
        g_value_init(result, G_TYPE_INT);
        g_value_set_int(result, total);

        return result;
}

static void _tracker_cache_value_free(gpointer data)
{
        TrackerCacheValue *value = (TrackerCacheValue *) data;

        if (value) {
                if (value->key_type == TRACKER_CACHE_KEY_TYPE_COMPUTED) {
                        g_value_unset(&value->value);
                } else if (value->key_type == TRACKER_CACHE_KEY_TYPE_DERIVED) {
                        g_free(value->key_derived_from);
                }
                g_free(value);
        }
}

static void _increase_key_index(TrackerCache *cache, const gchar *key)
{
        TrackerCacheValue *cached_value;

        /* Look for the key */
        cached_value = g_hash_table_lookup(cache->cache, key);

        if (cached_value &&
            cached_value->key_type == TRACKER_CACHE_KEY_TYPE_TRACKER) {
                cached_value->tracker_index++;
        }
}

/* ------------------------- Public API ------------------------- */

/*
 * tracker_cache_new:
 * @service: service that will be used in tracker
 * @result_type: type of query will be used in tracker.
 *
 * Creates a new cache.
 *
 * Returns: a new cache
 **/
TrackerCache *
tracker_cache_new(ServiceType service,
                  enum TrackerCacheResultType result_type)
{

        TrackerCache *cache;

        cache = g_new0(TrackerCache, 1);
        cache->service = service;
        cache->result_type = result_type;
        cache->cache = g_hash_table_new_full(g_str_hash,
                                             g_str_equal,
                                             g_free,
                                             _tracker_cache_value_free);
        return cache;
}

/*
 * tracker_cache_free:
 * @cache: cache to be freed
 *
 * Frees the cache and its contents
 */
void
tracker_cache_free(TrackerCache *cache)
{
        /* Free tracker results */
        if (cache->tracker_results) {
                g_ptr_array_foreach(cache->tracker_results,
                                    (GFunc) g_strfreev,
                                    NULL);
                g_ptr_array_free(cache->tracker_results, TRUE);
        }

        /* Free cache */
        g_hash_table_unref(cache->cache);

        /* Free the cache itself */
        g_free(cache);
}

/*
 * tracker_cache_key_add_precomputed:
 * @cache: the cache
 * @key: the key to be inserted
 * @user_key: @TRUE if this key has been requested by the user
 * @value: the value to be inserted
 *
 * Inserts a key with its value in the cache. If the key already
 * exists, it does nothing.
 */
void
tracker_cache_key_add_precomputed(TrackerCache *cache,
                                  const gchar *key,
                                  gboolean user_key,
                                  const GValue *value)
{
        TrackerCacheValue *cached_value;

        /* Look if the key already exists */
        if (!g_hash_table_lookup(cache->cache, key)) {
                /* Create the value to be cached */
                cached_value = g_new0(TrackerCacheValue, 1);
                cached_value->key_type = TRACKER_CACHE_KEY_TYPE_COMPUTED;
                cached_value->user_key = user_key;
                g_value_init(&cached_value->value, G_VALUE_TYPE(value));
                g_value_copy(value, &cached_value->value);

                /* Add to cache */
                g_hash_table_insert(cache->cache, g_strdup(key), cached_value);
        }
}

/*
 * tracker_cache_key_add_precomputed_string:
 * @cache: the cache
 * @key: the key to be inserted
 * @user_key: @TRUE if this key has been requested by the user
 * @value: the value to be inserted
 *
 * Inserts a key with its value in the cache. If the key already
 * exists, it does nothing.
 */
void
tracker_cache_key_add_precomputed_string(TrackerCache *cache,
                                         const gchar *key,
                                         gboolean user_key,
                                         const gchar *value)
{
        GValue gv = { 0 };

        g_value_init(&gv, G_TYPE_STRING);
        g_value_set_string(&gv, value);
        tracker_cache_key_add_precomputed(cache, key, user_key, &gv);
        g_value_unset(&gv);
}

/*
 * tracker_cache_key_add_precomputed_int:
 * @cache: the cache
 * @key: the key to be inserted
 * @user_key: @TRUE if this key has been requested by the user
 * @value: the value to be inserted
 *
 * Inserts a key with its value in the cache. If the key already
 * exists, it does nothing.
 */
void
tracker_cache_key_add_precomputed_int(TrackerCache *cache,
                                      const gchar *key,
                                      gboolean user_key,
                                      gint value)
{
        GValue gv = { 0 };

        g_value_init(&gv, G_TYPE_INT);
        g_value_set_int(&gv, value);
        tracker_cache_key_add_precomputed(cache, key, user_key, &gv);
        g_value_unset(&gv);
}

/*
 * tracker_cache_key_add_derived:
 * @cache: the cache
 * @key: the key to be inserted
 * @user_key: @TRUE if this key has been requested by user
 * @source_key: the key which will be used to get the real value
 *
 * Inserts a key which value will be obtained from other key. If the
 * key already exists, it does nothing.
 */
void
tracker_cache_key_add_derived(TrackerCache *cache,
                              const gchar *key,
                              gboolean user_key,
                              gchar *source_key)
{
        TrackerCacheValue *cached_value;

        /* Look if the key already exists */
        if (!g_hash_table_lookup(cache->cache, key)) {
                /* Create the value to be cached */
                cached_value = g_new0(TrackerCacheValue, 1);
                cached_value->key_type = TRACKER_CACHE_KEY_TYPE_DERIVED;
                cached_value->user_key = user_key;
                cached_value->key_derived_from = g_strdup(source_key);

                /* Add to cache */
                g_hash_table_insert(cache->cache, g_strdup(key), cached_value);
        }
}

/*
 * tracker_cache_key_add:
 * @cache: the cache
 * @key: the key to be inserted
 * @user_key: @TRUE if the user has requested this key
 *
 * Inserts in the cache a new key. If the key exists, and the new one
 * is a user's key, then mark the old one as requested by user.
 * Notice that depending on the service and query type used to create
 * the cache, some keys could be discarded. Keys not supported in
 * tracker will be discarded.
 */
void
tracker_cache_key_add(TrackerCache *cache,
                      const gchar *key,
                      gboolean user_key)
{
        TrackerCacheValue *value;
        gint offset;
        MetadataKey *metadata_key;
        InfoKeyTable *t = keymap_get_info_key_table();

        metadata_key = g_hash_table_lookup(t->metadata_keys,
                                           key);

        /* Discard unsupported keys */
        if (!metadata_key) {
                return;
        }

        /* Look if the key already exists */
        if ((value = g_hash_table_lookup(cache->cache, key))) {
                /* The key already exists. If now user asks for this
                 * key, update it */
                if (user_key) {
                        value->user_key = user_key;
                }
                return;
        }

        /* Insert dependencies */
        if (metadata_key->depends_on) {
                tracker_cache_key_add(cache, metadata_key->depends_on, FALSE);
        }

        /* With unique, only duration, childcount and mime keys
         * makes sense */
        if ((cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE) &&
            strcmp(key, MAFW_METADATA_KEY_CHILDCOUNT) != 0 &&
            strcmp(key, MAFW_METADATA_KEY_DURATION) != 0 &&
            strcmp(key, MAFW_METADATA_KEY_MIME) != 0 &&
            !albumart_key_is_album_art(key)) {
                _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_VOID,
                            user_key, -1);
                return;
        }

        /* Insert album-art and thumbnail keys */
        if (albumart_key_is_album_art(key) ||
            albumart_key_is_thumbnail(key)) {
                _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_THUMBNAILER,
                            user_key, -1);
                return;
        }

        /* In case of Uri, it is always returned in first position
         * when querying */
        if (strcmp(key, MAFW_METADATA_KEY_URI) == 0 &&
            cache->result_type == TRACKER_CACHE_RESULT_TYPE_QUERY) {
                _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_TRACKER,
                            user_key, 0);
                return;
        }

        /* If unique is used, childcount is the first value after
         * the concat key (if it was inserted) */
        if (cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE &&
            strcmp(key, MAFW_METADATA_KEY_CHILDCOUNT) == 0) {
                if (cache->concat_added) {
                        offset = 1;
                } else {
                        offset = 0;
                }
                _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_TRACKER,
                            user_key, cache->last_tracker_index + offset);
                /* Increase index for the 'sum' key */
                if (cache->sum_added) {
                        _increase_key_index(cache, MAFW_METADATA_KEY_DURATION);
                }
                cache->count_added = TRUE;
                return;
        }

        /* If unique_count is used, duration is the second value
         * after the unique values and count */
        if (cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE &&
            strcmp(key, MAFW_METADATA_KEY_DURATION) == 0) {
                if (cache->concat_added && cache->count_added) {
                        offset = 2;
                } else if (cache->concat_added || cache->count_added) {
                        offset = 1;
                } else {
                        offset = 0;
                }
                _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_TRACKER,
                            user_key, cache->last_tracker_index + offset);
                cache->sum_added = TRUE;
                return;
        }

        /* Childcount is 0 for all clips, unless playlists */
        if (strcmp(key, MAFW_METADATA_KEY_CHILDCOUNT) == 0 &&
            cache->service != SERVICE_PLAYLISTS) {
                tracker_cache_key_add_precomputed_int(cache, key, user_key, 0);
                return;
        }

        /* MIME for playlists and non-leaf nodes are always 'container' */
        if (strcmp(key, MAFW_METADATA_KEY_MIME) == 0 &&
            (cache->service == SERVICE_PLAYLISTS ||
             cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE)) {
                tracker_cache_key_add_precomputed_string(
                        cache,
                        key,
                        user_key,
                        MAFW_METADATA_VALUE_MIME_CONTAINER);
                return;
        }

        /* In case of title and non-unique, ask also for URI, as it
         * could be used as title just if there it doesn't have one */
        if (strcmp(key, MAFW_METADATA_KEY_TITLE) == 0 &&
            cache->result_type != TRACKER_CACHE_RESULT_TYPE_UNIQUE) {
                tracker_cache_key_add(cache, MAFW_METADATA_KEY_URI, FALSE);
        }

        /* Insert remaining keys */
        if (cache->result_type == TRACKER_CACHE_RESULT_TYPE_QUERY) {
                /* In case of queries, skip uri and service */
                offset = 2;
        } else {
                offset = 0;
        }
        _insert_key(cache, key, TRACKER_CACHE_KEY_TYPE_TRACKER,
                    user_key, cache->last_tracker_index + offset);
        cache->last_tracker_index++;
        return;
}

/*
 * tracker_cache_key_add_several:
 * @cache: the cache
 * @keys: NULL-ending array of keys
 * @user_keys: @TRUE if the user has requested these keys
 *
 * Inserts in the cache the keys specified. See @tracker_cache_key_add
 * for more information.
 */
void
tracker_cache_key_add_several(TrackerCache *cache,
                              gchar **keys,
                              gboolean user_keys)
{
        gint i;

        for (i=0; keys[i]; i++) {
                tracker_cache_key_add(cache, keys[i], user_keys);
        }
}

/*
 * tracker_cache_key_add_unique:
 * @cache: tracker cache
 * @unique_keys: a NULL-ending array of keys
 *
 * Add a list of keys that will be used to query tracker with 'unique'
 * functions. If some of the keys already exists, they are
 * skipped. Warning! This function only works if the case was created
 * with TRACKER_CACHE_RESULT_TYPE_UNIQUE.
 */
void
tracker_cache_key_add_unique(TrackerCache *cache,
                             gchar **unique_keys)
{
        gint i;

        /* Check if cache was created to store data from
         * unique_count_sum functions */
        g_return_if_fail(
                cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        i = 0;
        while (unique_keys[i]) {
                /* Skip unsupported keys */
                if (keymap_is_key_supported(unique_keys[i])) {
                        /* Check the key doesn't exist */
                        if (g_hash_table_lookup(cache->cache,
                                                unique_keys[i]) == NULL) {
                                /* Special case: 'unique' functions are used
                                 * when processing containers. In this case,
                                 * mime type of a container must be always
                                 * @MAFW_METADATA_VALUE_MIME_CONTAINER, no
                                 * matter the result tracker returns. */
                                if (strcmp(unique_keys[i],
                                           MAFW_METADATA_KEY_MIME) == 0) {
                                        tracker_cache_key_add_precomputed_string(
                                                cache,
                                                unique_keys[i],
                                                FALSE,
                                                MAFW_METADATA_VALUE_MIME_CONTAINER);
                                } else {
                                        _insert_key(
                                                cache,
                                                unique_keys[i],
                                                TRACKER_CACHE_KEY_TYPE_TRACKER,
                                                FALSE,
                                                cache->last_tracker_index);
                                }
                        }
                        /* Though the key already exists, skip its place in
                         * the results. That is, we will use the already
                         * stored value even tracker will be returning a
                         * different value */
                        cache->last_tracker_index++;
                }
                i++;
        }
}

/*
 * tracker_cache_key_add_concat:
 * @cache: tracker cache
 * @concat_key: key that will be concatenated
 *
 * Add the key to be concatenated with 'unique_concat'
 * function. Warning! This function only works if the cache was
 * created with TRACKER_CACHE_RESULT_TYPE_UNIQUE.
 */
void
tracker_cache_key_add_concat(TrackerCache *cache,
                             const gchar *concat_key)
{
        gboolean user_req;
        TrackerCacheValue *value;

        g_return_if_fail(cache->result_type == TRACKER_CACHE_RESULT_TYPE_UNIQUE);

        /* Check if the key already exists, and if so maintains the user request value */
        value = g_hash_table_lookup(cache->cache, concat_key);

        if (value) {
                user_req = value->user_key;
        } else {
                user_req = FALSE;

        }

        /* 'concat' key comes in the first position after the unique keys */
        _insert_key(cache, concat_key, TRACKER_CACHE_KEY_TYPE_TRACKER,
                    user_req, cache->last_tracker_index);

        /* Now, increase index for childcount and duration, if they were
         * inserted */
        if (cache->count_added) {
                _increase_key_index(cache, MAFW_METADATA_KEY_CHILDCOUNT);
        }

        if (cache->sum_added) {
                _increase_key_index(cache, MAFW_METADATA_KEY_DURATION);
        }

        cache->concat_added = TRUE;
}

/*
 * tracker_cache_keys_get_tracker:
 * @cache: the cache with the keys/values
 *
 * Returns a strv with the keys to add to tracker. Note the keys are
 * MAFW keys. In case of using 'unique' functions, it returns the keys
 * that will be used to group the values.
 *
 * Returns: MAFW keys to ask tracker
 */
gchar **
tracker_cache_keys_get_tracker(TrackerCache *cache)
{
        gchar **ask_keys;
        GHashTableIter cache_iter;
        gchar *key;
        TrackerCacheValue *value;
        gint offset;
        gint limit;

        ask_keys = g_new0(gchar *, cache->last_tracker_index + 1);

        /* Skip uri and service in case of query */
        if (cache->result_type == TRACKER_CACHE_RESULT_TYPE_QUERY) {
                offset = 2;
                limit = cache->last_tracker_index + 2;
        } else {
                offset = 0;
                limit = cache->last_tracker_index;
        }

        /* Search tracker keys and add them to the strv */
        g_hash_table_iter_init(&cache_iter,
                               cache->cache);
        while (g_hash_table_iter_next(&cache_iter,
                                      (gpointer *)&key,
                                      (gpointer *)&value)) {
                /* The keys must be between offset and last_tracker_index */
                if (value->key_type == TRACKER_CACHE_KEY_TYPE_TRACKER &&
                    value->tracker_index >= offset &&
                    value->tracker_index < limit) {
                        ask_keys[value->tracker_index - offset] = g_strdup(key);
                }
        }

        return ask_keys;
}

/*
 * tracker_cache_keys_get_user:
 * @cache: tracker cache
 *
 * Get a NULL-ending array with the keys requested by the user.
 *
 * Returns: a NULL-ending array
 */
gchar **
tracker_cache_keys_get_user(TrackerCache *cache)
{
        gchar **user_keys = NULL;
        GHashTableIter cache_iter;
        gchar *key;
        TrackerCacheValue *value;
        gint index = 0;

        /* We don't know how may elements are from user, but the worst
         * case is all keys are requested by user */
        user_keys = g_new0(gchar *, g_hash_table_size(cache->cache) + 1);

        g_hash_table_iter_init(&cache_iter,
                               cache->cache);
        while (g_hash_table_iter_next(&cache_iter,
                                      (gpointer *)&key,
                                      (gpointer *)&value)) {
                if (value->user_key) {
                        user_keys[index] = g_strdup(key);
                        index++;
                }
        }

        return user_keys;
}

/*
 * tracker_cache_values_add_results:
 * @cache: tracker cache
 * @tracker_results: results returned by tracker.
 *
 * Adds to the cache the results returned by a tracker query.
 */
void
tracker_cache_values_add_results(TrackerCache *cache,
                                 GPtrArray *tracker_results)
{
        cache->tracker_results = tracker_results;
}

/*
 * tracker_cache_values_add_result:
 * @cache: tracker cache
 * @tracker_result: result returned by tracker with tracker_get_metadata
 *
 * Adds to the cache the result returned by getting metadata from
 * tracker. Warning! Only works if cache was created with
 * TRACKER_CACHE_RESULT_TYPE_GET_METADATA.
 */
void
tracker_cache_values_add_result(TrackerCache *cache,
                                gchar **tracker_result)
{
        g_return_if_fail(
                cache->result_type == TRACKER_CACHE_RESULT_TYPE_GET_METADATA);

        /* Wrap the result around a gptrarray */
        cache->tracker_results = g_ptr_array_sized_new(1);
        g_ptr_array_add(cache->tracker_results, tracker_result);
}

/*
 * tracker_cache_values_get_results:
 * @cache: tracker cache
 *
 * Returns the results obtained by tracker.
 *
 * Returns: results from tracker.
 */
const GPtrArray *
tracker_cache_values_get_results(TrackerCache *cache)
{
        return cache->tracker_results;
}

/*
 * tracker_cache_value_get:
 * @cache: tracker cache
 * @key: key to query
 * @index: which result should be used (from tracker), or -1 if none.
 *
 * Returns  the  value  associated  with  the key,  or  @NULL  if  not
 * present. If  -1 is used  as index, then  it doesn't use  any result
 * from tracker, but those that were precomputed.
 *
 * Returns: the value for the queried key. Must be freed.
 */
GValue *
tracker_cache_value_get(TrackerCache *cache,
                        const gchar *key,
                        gint index)
{
        GValue *return_value = NULL;
        TrackerCacheValue *cached_value = NULL;
        gchar **queried_result = NULL;
        gchar *uri;

        cached_value = g_hash_table_lookup(cache->cache, key);

        /* Check if key is present */
        if (!cached_value) {
                return NULL;
        }

        /* If the value was precomputed */
        if (cached_value->key_type == TRACKER_CACHE_KEY_TYPE_COMPUTED) {
                /* Precalculated value */
                return_value = g_new0(GValue, 1);
                g_value_init(return_value, G_VALUE_TYPE(&cached_value->value));
                g_value_copy(&cached_value->value, return_value);
                return return_value;
        }

        /* if the value is derived */
        if (cached_value->key_type == TRACKER_CACHE_KEY_TYPE_DERIVED) {
                return tracker_cache_value_get(cache,
                                               cached_value->key_derived_from,
                                               index);
        }

        /* If the value must be obtained from hildon-thumbnailer */
        if (cached_value->key_type == TRACKER_CACHE_KEY_TYPE_THUMBNAILER) {
                if (strcmp(key, MAFW_METADATA_KEY_ALBUM_ART_URI) == 0) {
                        return _get_value_album_art(cache, index);
                } else {
                        return _get_value_thumbnail(cache, key, index);
                }
        }

        /* If the value must be obtained from tracker */
        if (cached_value->key_type == TRACKER_CACHE_KEY_TYPE_TRACKER) {
                /* Check if the value can be obtained from tracker */
                if (index < 0) {
                        return NULL;
                }

                /* Verify there is data, and index is within the range */
                if (!cache->tracker_results ||
                    cache->tracker_results->len <= index) {
                        g_warning ("index is out of range");
                        return NULL;
                }

                queried_result = (gchar **) g_ptr_array_index(
                        cache->tracker_results,
                        index);

                return_value = g_new0(GValue, 1);
                if (strcmp(key, MAFW_METADATA_KEY_PLAY_COUNT) == 0 ||
		    strcmp(key, MAFW_METADATA_KEY_CHILDCOUNT) == 0 ||
                    strcmp(key, MAFW_METADATA_KEY_DURATION) == 0 ||
                    strcmp(key, MAFW_METADATA_KEY_PAUSED_POSITION) == 0 ||
		    strcmp(key, TRACKER_PKEY_VALID_DURATION) == 0) {
                        g_value_init(return_value, G_TYPE_INT);
                        g_value_set_int(
                                return_value,
                                atoi(queried_result[cached_value->tracker_index]));
		} else if (strcmp(key, MAFW_METADATA_KEY_ADDED) == 0) {
			g_value_init(return_value, G_TYPE_LONG);
			g_value_set_long(
				return_value,
				atol(queried_result[cached_value->tracker_index]));
                } else {
                        g_value_init(return_value, G_TYPE_STRING);
                        /* Special case: convert pathname to URI */
                        if (strcmp(key, MAFW_METADATA_KEY_URI) == 0) {
                                uri = g_filename_to_uri(
                                        queried_result[cached_value->tracker_index],
                                        NULL,
                                        NULL);
                                g_value_set_string(return_value, uri);
                                g_free(uri);
			} else {
				g_value_set_string(
                                        return_value,
                                        queried_result[cached_value->tracker_index]);
                        }
                }

                return return_value;
        }

        return NULL;
}

/*
 * tracker_cache_build_metadata:
 * @cache: tracker cache
 *
 * Builds a list of MAFW-metadata from cached results.
 *
 * Returns: list of MAFW-metadata
 */
GList *
tracker_cache_build_metadata(TrackerCache *cache)
{
        GList *mafw_list = NULL;
        gchar **user_keys;
        GValue *value;
        gint result_index;
        gint key_index;
        GHashTable *metadata = NULL;

        /* Check if there are results */
        if (!cache->tracker_results ||
            cache->tracker_results->len == 0) {
                return NULL;
        }

        /* Get the list of keys user requested */
        user_keys = tracker_cache_keys_get_user(cache);

        /* Create metadata */
        for (result_index = 0;
             result_index < cache->tracker_results->len;
             result_index++) {
                metadata = mafw_metadata_new();
                for (key_index = 0; user_keys[key_index]; key_index++) {
                        /* Special cache: title must use filename if
                         * it doesn't contain title */
                        if (strcmp(user_keys[key_index],
                                   MAFW_METADATA_KEY_TITLE) == 0) {
                                value = _get_title(cache, result_index);
                        } else {
                                value = tracker_cache_value_get(
                                        cache,
                                        user_keys[key_index],
                                        result_index);
                        }
                        if (_value_is_allowed(value, user_keys[key_index])) {
                                _replace_various_values(value);
                                mafw_metadata_add_val(metadata,
                                                      user_keys[key_index],
                                                      value);
                        }
                        util_gvalue_free(value);
                }
                mafw_list = g_list_prepend(mafw_list, metadata);
        }

        /* Place elements in right order */
        mafw_list = g_list_reverse(mafw_list);

        /* Free unneeded data */
        g_strfreev(user_keys);

        return mafw_list;
}

/*
 * tracker_cache_build_metadata_aggregated:
 * @cache: tracker cache
 * @count_childcount: @TRUE if childcount must be aggregated counting number of
 * times it appears
 *
 * Build a MAFW-metadata from cached aggregating durations and childcounts.
 *
 * Returns: a MAFW-metadata
 */
GHashTable *
tracker_cache_build_metadata_aggregated(TrackerCache *cache,
                                        gboolean count_childcount)
{
        gchar **user_keys;
        gint key_index;
        GValue *value;
        GHashTable *metadata;

        /* Get the list of user-requested keys */
        user_keys = tracker_cache_keys_get_user(cache);

        /* Create metadata */
        metadata = mafw_metadata_new();
        for (key_index = 0; user_keys[key_index]; key_index++) {
                /* Special cases */
                if (strcmp(user_keys[key_index],
                           MAFW_METADATA_KEY_CHILDCOUNT) == 0 ||
                    strcmp(user_keys[key_index],
                           MAFW_METADATA_KEY_DURATION) == 0) {
                        value = _aggregate_key(cache, user_keys[key_index],
                                               count_childcount);
                } else {
                        value = tracker_cache_value_get(cache,
                                                        user_keys[key_index],
                                                        0);
                }

                if (_value_is_allowed(value, user_keys[key_index])) {
                        _replace_various_values(value);
                        mafw_metadata_add_val(metadata,
                                              user_keys[key_index],
                                              value);
                }
                util_gvalue_free(value);
        }

        g_strfreev(user_keys);

        return metadata;
}

/*
 * tracker_cache_key_exists:
 * @cache: tracker cache
 * @key: key too look
 *
 * Check if the cache contains the key
 *
 * Returns: @TRUE if the key is in the cache
 */
gboolean
tracker_cache_key_exists(TrackerCache *cache,
                         const gchar *key){
        return g_hash_table_lookup_extended(cache->cache, key, NULL, NULL);
}
