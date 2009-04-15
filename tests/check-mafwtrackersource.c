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

#include <stdio.h>
#include <check.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib.h>
#include <libmafw/mafw.h>
#include <checkmore.h>
#include <gio/gio.h>
#include "mafw-tracker-source.h"
#include "tracker-iface.h"

#define UNKNOWN_ARTIST_VALUE "(Unknown artist)"
#define UNKNOWN_ALBUM_VALUE  "(Unknown album)"
#define UNKNOWN_GENRE_VALUE  "(Unknown genre)"
#define SERVICE_MUSIC_STR "Music"

SRunner *configure_tests(void);
static void create_temporal_playlist (gchar *path, gint n_items);

/* ---------------------------------------------------- */
/*                      GLOBALS                         */
/* ---------------------------------------------------- */

static MafwSource *g_tracker_source = NULL;
static gboolean g_browse_called = FALSE;
static gboolean g_browse_error = FALSE;
static gboolean g_metadata_called = FALSE;
static gboolean g_metadata_error = FALSE;
static gboolean g_destroy_called = FALSE;
static gboolean g_destroy_error = FALSE;
static gboolean g_set_metadata_called = FALSE;
static gboolean g_set_metadata_error = FALSE;
static gboolean g_set_metadata_params_err = FALSE;
static GList *g_browse_results = NULL;
static GList *g_metadata_results = NULL;
static GList *g_destroy_results = NULL;
static GList *g_set_metadata_failed_keys = NULL;
static gchar *RUNNING_CASE = NULL;

typedef struct {
	guint browse_id;
	guint index;
	gchar *objectid;
	GHashTable *metadata;
} BrowseResult;

typedef struct {
	gchar *objectid;
	GHashTable *metadata;
} MetadataResult;

typedef void TrackerClient;

typedef enum {
        SERVICE_MUSIC = 4,
        SERVICE_VIDEOS = 5,
        SERVICE_PLAYLISTS = 19
} ServiceType;

typedef void (*TrackerGPtrArrayReply) (GPtrArray *result, GError *error, gpointer user_data);
typedef void (*TrackerArrayReply) (char **result, GError *error, gpointer user_data);

SRunner * configure_tests(void);

void tracker_disconnect(TrackerClient *client);

TrackerClient *
tracker_connect(gboolean enable_warnings);

void
tracker_metadata_get_async(TrackerClient *client,
                           ServiceType service,
                           const char *id,
                           char **keys,
                           TrackerArrayReply callback,
                           gpointer user_data);

static char **
_get_metadata(gint index,
              char **keys);

void
tracker_search_query_async (TrackerClient *client,
                            int live_query_id,
                            ServiceType service,
                            char **fields,
                            const char *search_text,
                            const char *keywords,
                            const char *query,
                            int offset, int max_hits,
                            gboolean sort_by_service,
                            char **sort_fields,
                            gboolean sort_descending,
                            TrackerGPtrArrayReply callback,
                            gpointer user_data);

void
tracker_metadata_get_unique_values_async(TrackerClient *client,
                                         ServiceType service,
                                         char **meta_types,
                                         const char *query,
                                         gboolean descending,
                                         int offset,
                                         int max_hits,
                                         TrackerGPtrArrayReply callback,
                                         gpointer user_data);

void
tracker_metadata_get_unique_values_with_concat_count_and_sum_async(TrackerClient *client,
                                                                   ServiceType service,
                                                                   char **meta_types,
                                                                   const char *query,
                                                                   char *concat,
                                                                   char *count,
                                                                   char *sum,
                                                                   gboolean descending,
                                                                   int offset,
                                                                   int max_hits,
                                                                   TrackerGPtrArrayReply callback,
                                                                   gpointer user_data);
void
tracker_metadata_set(TrackerClient *client,
		     ServiceType service,
		     const char *id,
		     char **keys,
		     char **values,
		     GError **error);

/* ---------------------------------------------------- */
/*                   HELPER FUNCTIONS                   */
/* ---------------------------------------------------- */

static void remove_browse_item(gpointer item, gpointer data)
{
	BrowseResult *br = (BrowseResult *) item;

	if (br->metadata)
		g_hash_table_unref(br->metadata);
	br->metadata = NULL;

	g_free(br->objectid);
	g_free(br);

}

static void remove_metadata_item(gpointer item, gpointer data)
{
	MetadataResult *mr = (MetadataResult *) item;

	if (mr->metadata)
	{
		mafw_metadata_release(mr->metadata);
	}
	mr->metadata = NULL;

	g_free(mr->objectid);
        g_free(mr);
}

static void clear_browse_results(void)
{
	g_browse_called = FALSE;
        g_browse_error = FALSE;
	if (g_browse_results != NULL) {
		g_list_foreach(g_browse_results, (GFunc) remove_browse_item,
			       NULL);
		g_list_free(g_browse_results);
		g_browse_results = NULL;
	}
        RUNNING_CASE = "no_case";
}

static void clear_metadata_results(void)
{
	g_metadata_called = FALSE;
        g_metadata_error = FALSE;
	if (g_metadata_results != NULL) {
		g_list_foreach(g_metadata_results, (GFunc) remove_metadata_item,
			       NULL);
		g_list_free(g_metadata_results);
		g_metadata_results = NULL;
	}
        RUNNING_CASE = "no_case";
}

static void clear_destroy_results(void)
{
	g_destroy_called = FALSE;
	g_destroy_error = FALSE;
	if (g_destroy_results != NULL) {
		g_list_foreach(g_destroy_results, (GFunc) g_free, NULL);
		g_list_free(g_destroy_results);
		g_destroy_results = NULL;
	}
	RUNNING_CASE = "no_case";
}

static void clear_set_metadata_results(void)
{
	g_set_metadata_called = FALSE;
	g_set_metadata_params_err = FALSE;
	g_set_metadata_error = FALSE;

	if (g_set_metadata_failed_keys != NULL) {
		g_list_foreach(g_set_metadata_failed_keys, (GFunc) g_free, NULL);
		g_list_free(g_set_metadata_failed_keys);
		g_set_metadata_failed_keys = NULL;
	}

	g_set_metadata_failed_keys = FALSE;

	RUNNING_CASE = "no_case";
}

/* ---------------------------------------------------- */
/*                      FIXTURES                        */
/* ---------------------------------------------------- */

static void fx_setup_dummy_tracker_source(void)
{
	GError *error = NULL;

	g_type_init();

	/* Check if we have registered the plugin, otherwise
	   do it and get a pointer to the Tracker source instance */
	MafwRegistry *registry = MAFW_REGISTRY(mafw_registry_get_instance());
	GList *sources = mafw_registry_get_sources(registry);
	if (sources == NULL) {
		mafw_tracker_source_plugin_initialize(registry, &error);
		if (error != NULL) {
			g_error ("Plugin initialization failed!");
		}
		sources = mafw_registry_get_sources (registry);
	}

	if (sources == NULL) {
		g_error ("Plugin intialization failed!");
	}

	g_tracker_source = MAFW_SOURCE(g_object_ref(G_OBJECT(sources->data)));

	fail_if(!MAFW_IS_TRACKER_SOURCE(g_tracker_source),
		"Could not create tracker source instance");
}

static void fx_teardown_dummy_tracker_source(void)
{
	g_object_unref(g_tracker_source);
}


/* ---------------------------------------------------- */
/*                     TEST CASES                       */
/* ---------------------------------------------------- */

static void
browse_result_cb(MafwSource * source, guint browse_id, gint remaining,
		 guint index, const gchar * objectid, GHashTable * metadata,
		 gpointer user_data, const GError *error)
{
	BrowseResult *result = NULL;
	const gchar *mime = NULL;
	GValue *value = NULL;

	g_browse_called = TRUE;

        if (error) {
                g_browse_error = TRUE;
                return;
        }

	/* We assume that 'mime' is the first metadata! */
	mime = metadata && (value = mafw_metadata_first(metadata,
						MAFW_METADATA_KEY_MIME))
		? g_value_get_string(value) : "?";

	g_print(" Received: (%c) %d  %s\n", mime[0], index, objectid);

	/* We get a NULL objectid if there are no items matching our query */
	if (objectid != NULL) {
		result = (BrowseResult *) g_malloc(sizeof(BrowseResult));
		result->browse_id = browse_id;
		result->index = index;
		result->objectid = g_strdup(objectid);
		result->metadata = metadata;
		if (metadata)
			g_hash_table_ref(metadata);

		g_browse_results = g_list_append(g_browse_results, result);
	}
}

/* Browse localtagfs:: */
START_TEST(test_browse_root)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_root";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse root category...\n");
	/* Browse root category */
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::", FALSE,
			   NULL, NULL, metadata, 0, 50,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE, "No browse_result signal received");

	/* We should receive two subcategories (music, video) */
	fail_if(g_list_length(g_browse_results) != 2,
		"Browse of root category returned %d items instead of 2",
		g_list_length(g_browse_results));

        clear_browse_results();
        g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music */
START_TEST(test_browse_music)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse music category...\n");
	/* Browse music category */
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music",
			   FALSE, NULL, NULL, metadata, 0, 50,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received for");

	/* We should receive five subcategories (artists, albums,
	 * songs, genres, playlists)*/
	fail_if(g_list_length(g_browse_results) != 5,
		"Browse of music category returned %d items instead of 5",
		g_list_length(g_browse_results));

        clear_browse_results();
        g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/artists */
START_TEST(test_browse_music_artists)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_artists";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse artists category...\n");
	/* Browse artists category */
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/artists",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 6 artists */
        fail_if(g_list_length(g_browse_results) != 6,
                "Browse of artists category returned %d items instead of 6",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/artists/Artist%201 */
START_TEST(test_browse_music_artists_artist1)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_artists_artist1";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Artist 1' artist...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/artists/Artist%201",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 2 albums */
        fail_if(g_list_length(g_browse_results) != 2,
                "Browse of 'Artist 1' returned %d items instead of 2",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/artists/ */
START_TEST(test_browse_music_artists_unknown)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_artists_unknown";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Unknown' artist...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/artists/",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 2 albums */
        fail_if(g_list_length(g_browse_results) != 2,
                "Browse of 'Unknown' returned %d items instead of 2",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/artists// */
START_TEST(test_browse_music_artists_unknown_unknown)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_artists_unknown_unknown";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Browse album 'Unknown'...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/artists//",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 2 clips */
        fail_if(g_list_length(g_browse_results) != 2,
                "Browse of 'Unknown' returned %d items instead of 2",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/artists/Artist%201/Album%203 */
START_TEST(test_browse_music_artists_artist1_album3)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_artists_artist1_album3";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Browse album 'Album 3'...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/artists/Artist%201/Album%203",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 4 clips */
        fail_if(g_list_length(g_browse_results) != 4,
                "Browse of 'Album 3' returned %d items instead of 4",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/albums */
START_TEST(test_browse_music_albums)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_albums";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse albums category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/albums",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 6 albums */
        fail_if(g_list_length(g_browse_results) != 7,
                "Browse of artists category returned %d items instead of 7",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/albums/Album%204 */
START_TEST(test_browse_music_albums_album4)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_albums_album4";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Album 4' category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/albums/Album%204%20-%20Artist%204",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 3 clips */
        fail_if(g_list_length(g_browse_results) != 3,
                "Browse of 'Album 4' category returned %d items instead of 3",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/genres */
START_TEST(test_browse_music_genres)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_genres";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse genres category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/genres",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 4 genres */
        fail_if(g_list_length(g_browse_results) != 4,
                "Browse of artists category returned %d items instead of 4",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/genres/Genre%202 */
START_TEST(test_browse_music_genres_genre2)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_genres_genre2";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Genre 2' category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 4 artists */
        fail_if(g_list_length(g_browse_results) != 4,
                "Browse of artists category returned %d items instead of 4",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/genres/ */
START_TEST(test_browse_music_genres_unknown)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_genres_unknown";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Unknown' category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/genres/",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 3 artists */
        fail_if(g_list_length(g_browse_results) != 3,
                "Browse of artists category returned %d items instead of 3",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/genres/Genre%202/Artist%202 */
START_TEST(test_browse_music_genres_genre2_artist2)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_genres_genre2_artist2";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Artist 2' category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202/Artist%202",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 1 album */
        fail_if(g_list_length(g_browse_results) != 1,
                "Browse of artists category returned %d items instead of 1",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/genres/Genre%202/Artist%202/Album%202 */
START_TEST(test_browse_music_genres_genre2_artist2_album2)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_genres_genre2_artist2_album2";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse 'Album 2' category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202/Artist%202/Album%202",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 1 clip */
        fail_if(g_list_length(g_browse_results) != 1,
                "Browse of artists category returned %d items instead of 1",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/songs */
START_TEST(test_browse_music_songs)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_songs";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse songs category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 14 songs */
        fail_if(g_list_length(g_browse_results) != 14,
                "Browse of songs category returned %d items instead of 14",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/playlists */
START_TEST(test_browse_music_playlists)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_playlists";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse playlists category...\n");
	mafw_source_browse(g_tracker_source,
			    MAFW_TRACKER_SOURCE_UUID "::music/playlists",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 2 playlists */
        fail_if(g_list_length(g_browse_results) != 2,
                "Browse of playlists category returned %d items instead of 2",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::music/playlists/playlist1 */
START_TEST(test_browse_music_playlists_playlist1)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_music_playlists_playlist1";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_TITLE);

	g_print("> Browse playlistrecently-added playlist category...\n");
	create_temporal_playlist ("/tmp/playlist1.pls", 4);
	mafw_source_browse(g_tracker_source,
			    MAFW_TRACKER_SOURCE_UUID "::music/playlists/%2Ftmp%2Fplaylist1.pls",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);
	unlink("/tmp/playlist1.pls");
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive 4 entries */
        fail_if(g_list_length(g_browse_results) != 4,
                "Browse of recently-added playlist category returned %d items instead of '4'",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Test count parameter */
START_TEST(test_browse_count)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
        gint i;

        gint count_cases[] = {13, 14, 15};
        gint expected_count[] = {13, 14, 14};

        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* Test count greater than, equal than and lesser than the
         * number of clips */

        for (i = 0; i < 3; i++) {
                RUNNING_CASE = "test_browse_count";
                mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                                    FALSE, NULL, NULL, metadata, 0, count_cases[i],
                                    browse_result_cb, NULL);

                while (g_main_context_pending(context))
                        g_main_context_iteration(context, TRUE);

                fail_if(g_browse_called == FALSE,
                        "No browse_result signal received");

                fail_if(g_list_length(g_browse_results) != expected_count[i],
                        "Browse of artists category returned %d items instead of %d",
                        g_list_length(g_browse_results), expected_count[i]);

                clear_browse_results();
        }

        /* Special case: we want all elements */
        RUNNING_CASE = "test_browse_count";
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                            FALSE, NULL, NULL, metadata, 0, MAFW_SOURCE_BROWSE_ALL,
                            browse_result_cb, NULL);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_called == FALSE,
                "No browse_result signal received");

        fail_if(g_list_length(g_browse_results) != 14,
                "Browse of artists category returned %d items instead of 14",
                g_list_length(g_browse_results));

        clear_browse_results();

	g_main_loop_unref(loop);
}
END_TEST

/* Test offset parameter */
START_TEST(test_browse_offset)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
        gint i;

        gint offset_cases[] = {0, 13, 14, 15};
        gint expected_count[] = {14, 1, 0, 0};

        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* Test different cases of offset */
        for (i = 0; i < 4; i++) {
                RUNNING_CASE = "test_browse_offset";
                mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                                    FALSE, NULL, NULL, metadata, offset_cases[i], 50,
                                    browse_result_cb, NULL);

                while (g_main_context_pending(context))
                        g_main_context_iteration(context, TRUE);

                fail_if(g_browse_called == FALSE,
                        "No browse_result signal received");

                fail_if(g_list_length(g_browse_results) != expected_count[i],
                        "Browse of artists category returned %d items instead of %d",
                        g_list_length(g_browse_results), expected_count[i]);

                clear_browse_results();
        }
	g_main_loop_unref(loop);
}
END_TEST

/* Test invalid object_id */
START_TEST(test_browse_invalid)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* Test ill-formed objectid */
        RUNNING_CASE = "test_browse_invalid";
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/ssongs",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_error == FALSE,
	        "Browsing malformed objectid did not set error in browse callback");

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* Browse localtagfs::video */
START_TEST(test_browse_videos)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_browse_videos";
        loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_MIME);

	g_print("> Browse songs category...\n");
	mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::videos",
                            FALSE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* We should receive  videos */
        fail_if(g_list_length(g_browse_results) != 2,
                "Browse of video category returned %d items instead of '2'",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* This test canceling a browse */
START_TEST(test_browse_cancel)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
        guint browse_id;

        RUNNING_CASE = "test_browse_cancel";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* Retrieve clips */
	browse_id = mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                                        FALSE, NULL, NULL, metadata, 0, 50,
                                        browse_result_cb, NULL);

        /* Wait to receive the first 4 elements */
	while (g_main_context_pending(context) && g_list_length(g_browse_results) < 4)
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE,
		"No browse_result signal received");

        /* Now cancel the browse */
        fail_if (!mafw_source_cancel_browse (g_tracker_source, browse_id, NULL),
                 "Canceling a browse doesn't work");

        /* No elements should be received any more */
        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_list_length(g_browse_results) != 4,
                "Canceled browse of music category returned %d items more",
                g_list_length(g_browse_results));

        /* Cancelling again should return an error */
        fail_if (mafw_source_cancel_browse (g_tracker_source, browse_id, NULL),
                 "Canceling twice doesn't return an error");

	clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* This tests recursive browse */
START_TEST(test_browse_recursive)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* A couple of cases */
        RUNNING_CASE = "test_browse_recursive_songs";
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID
                            "::music/songs",
                            TRUE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_called == FALSE,
                "No browse_result signal received");

        fail_if(g_list_length(g_browse_results) != 14,
                "Recursive browse returned %d items instead of %d",
                g_list_length(g_browse_results), 14);

        clear_browse_results();

        RUNNING_CASE = "test_browse_recursive_artist1";
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID
                            "::music/artists/Artist%201",
                            TRUE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_called == FALSE,
                "No browse_result signal received");

        fail_if(g_list_length(g_browse_results) != 5,
                "Recursive browse returned %d items instead of %d",
                g_list_length(g_browse_results), 5);

        clear_browse_results();

        /* Special case: a clip is not browsable */
        RUNNING_CASE = "test_browse_recursive_clip";
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID
                            "::music/albums/Album%201/%2Fhome%2Fuser%2FMyDocs%2FSomeSong.mp3",
                            TRUE, NULL, NULL, metadata, 0, 50,
                            browse_result_cb, NULL);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_error == FALSE,
                "Browsed a non-browseable clip did not set an error in the browse callback");

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

/* This tests browse filter */
START_TEST(test_browse_filter)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	MafwFilter *filter = NULL;

	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

        /* Test a simple filter */
        RUNNING_CASE = "test_browse_filter_simple";
	filter = mafw_filter_parse("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                            TRUE, filter,
                            NULL, metadata, 0, 50,
                            browse_result_cb, NULL);
	mafw_filter_free(filter);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_called == FALSE,
                "No browse_result signal received");

        fail_if(g_list_length(g_browse_results) != 4,
                "Recursive browsing returned %d instead of 4",
                g_list_length(g_browse_results));

        clear_browse_results();

        /* Test an AND filter */
        RUNNING_CASE = "test_browse_filter_and";
	filter = mafw_filter_parse("(&(" MAFW_METADATA_KEY_ALBUM "=Album 3)("
				   MAFW_METADATA_KEY_TITLE "=Title 3))");
        mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/albums",
                            TRUE, filter,
                            NULL, metadata, 0, 50,
                            browse_result_cb, NULL);
	mafw_filter_free(filter);

        while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

        fail_if(g_browse_called == FALSE,
                "No browse_result signal received");

        fail_if(g_list_length(g_browse_results) != 1,
                "Recursive browsing returned %d instead of 1",
                g_list_length(g_browse_results));

        clear_browse_results();
	g_main_loop_unref(loop);
}
END_TEST

#if 0
START_TEST(test_browse_sort)
{
	const gchar *const *metadata = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	gchar *filter = NULL;
	gchar *sort_criteria = NULL;
	gchar *first_item_1 = NULL;
	gchar *last_item_1 = NULL;
	gchar *first_item_2 = NULL;
	gchar *last_item_2 = NULL;
	GHashTable *metadata_values = NULL;

	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_GENRE,
		MAFW_METADATA_KEY_ALBUM);

	/* browse Album 3 results sorting by title */
	clear_browse_results();
	sort_criteria = g_strdup("+" MAFW_METADATA_KEY_TITLE);
	filter = g_strdup("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
	mafw_source_browse(g_tracker_source,
			   MAFW_TRACKER_SOURCE_UUID "::music/songs",
			   FALSE, filter, sort_criteria, metadata,
			   0, MAFW_SOURCE_BROWSE_ALL,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE, "No browse_result signal received");

	fail_if(g_list_length(g_browse_results) != 4,
		"Browsing of music/songs category filtering by \"%s\" "
		"category returned %d items instead of %d",
		filter, g_list_length(g_browse_results), 4);

	g_free(filter);
	g_free(sort_criteria);

	/* Get the first and last items */
	metadata_values = ((BrowseResult *) ((g_list_nth(g_browse_results, 0))
					     ->data))->metadata;
	first_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));
	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 3))->data))->metadata;
	last_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));

	/* Now, do reverse sorting and compare results */
	clear_browse_results();
	sort_criteria = g_strdup("-" MAFW_METADATA_KEY_TITLE);
	filter = g_strdup("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
	mafw_source_browse(g_tracker_source,
			   MAFW_TRACKER_SOURCE_UUID "::music/songs",
			   FALSE, filter, sort_criteria, metadata,
			   0, MAFW_SOURCE_BROWSE_ALL,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE, "No browse_result signal received");

	fail_if(g_list_length(g_browse_results) != 4,
		"Browsing of music/songs category filtering by \"%s\" "
		"category returned %d items instead of %d",
		filter, g_list_length(g_browse_results), 4);

	g_free(filter);
	g_free(sort_criteria);

	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 0))->data))->metadata;
	first_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));
	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 3))->data))->metadata;
	last_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));

	/* First item should be the same as previously retrieved last item */
	fail_if(strcmp(last_item_1, first_item_2) != 0,
		"Sorting behaviour is not working");
	fail_if(strcmp(last_item_2, first_item_1) != 0,
		"Sorting behaviour is not working");

	g_free(first_item_1);
	g_free(last_item_1);
	g_free(first_item_2);
	g_free(last_item_2);

	/* Browse Album 3 sorting by genre and title  */
	clear_browse_results();
	sort_criteria = g_strdup("+" MAFW_METADATA_KEY_GENRE ",+"
				 MAFW_METADATA_KEY_TITLE);
	filter = g_strdup("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
	mafw_source_browse(g_tracker_source,
			   MAFW_TRACKER_SOURCE_UUID "::music/songs",
			   FALSE, filter, sort_criteria, metadata,
			   0, MAFW_SOURCE_BROWSE_ALL,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE, "No browse_result signal received");

	fail_if(g_list_length(g_browse_results) != 4,
		"Browsing of music/songs category filtering by \"%s\" "
		"category returned %d items instead of %d",
		filter, g_list_length(g_browse_results), 4);

	g_free(filter);
	g_free(sort_criteria);

	/* Get the first and last items */
	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 0))->data))->metadata;
	first_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));
	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 3))->data))->metadata;
	last_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));

	/* Now, do reverse sorting and compare results */
	clear_browse_results();
	sort_criteria = g_strdup("-" MAFW_METADATA_KEY_GENRE ",-"
				 MAFW_METADATA_KEY_TITLE);
	filter = g_strdup("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
	mafw_source_browse(g_tracker_source,
			   MAFW_TRACKER_SOURCE_UUID "::music/songs",
			   FALSE, filter, sort_criteria, metadata,
			   0, MAFW_SOURCE_BROWSE_ALL,
			   browse_result_cb, NULL);

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_browse_called == FALSE, "No browse_result signal received");

	fail_if(g_list_length(g_browse_results) != 4,
		"Browsing of music/songs category filtering by \"%s\" "
		"category returned %d items instead of %d",
		filter, g_list_length(g_browse_results), 4);

	g_free(filter);
	g_free(sort_criteria);

	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 0))->data))->metadata;
	first_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));
	metadata_values = ((BrowseResult *)
			   ((g_list_nth(g_browse_results, 3))->data))->metadata;
	last_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
				metadata_values, MAFW_METADATA_KEY_TITLE)));

	/* First item should be the same as previously retrieved last item */
	fail_if(strcmp(last_item_1, first_item_2) != 0,
		"Sorting behaviour is not working when using more "
		"than one field");
	fail_if(strcmp(last_item_2, first_item_1) != 0,
		"Sorting behaviour is not working when using more "
		"than one field");

	g_free(first_item_1);
	g_free(last_item_1);
	g_free(first_item_2);
	g_free(last_item_2);

        clear_browse_results();

	g_main_loop_unref(loop);

}
END_TEST
#endif

static void
metadata_result_cb(MafwSource * source, const gchar * objectid,
		   GHashTable * metadata,
		   gpointer user_data, const GError *error)
{
	MetadataResult *result = NULL;

	g_metadata_called = TRUE;

        if (error) {
                g_metadata_error = TRUE;
                return;
        }

	result = (MetadataResult *) malloc(sizeof(MetadataResult));
	result->objectid = g_strdup(objectid);
	result->metadata = metadata;
	if (metadata)
		g_hash_table_ref(metadata);

	g_metadata_results = g_list_append(g_metadata_results, result);
}

START_TEST(test_get_metadata_clip)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	const gchar *artist = NULL;
	const gchar *album = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;

        RUNNING_CASE = "test_get_metadata_clip";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_ALBUM_ART_LARGE_URI,
		MAFW_METADATA_KEY_ALBUM_ART_SMALL_URI,
		MAFW_METADATA_KEY_ALBUM_ART_MEDIUM_URI);

	/* Object ids to query */
	object_id =
		MAFW_TRACKER_SOURCE_UUID "::music/songs/"
                "%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "audio/x-mp3"),
		"Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
		clip_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ARTIST);
	artist = mval ? g_value_get_string(mval) : NULL;
	fail_if(artist == NULL || strcmp(artist, "Artist 1"),
		"Artist for '%s' is '%s' instead of expected 'Artist 1'",
		clip_id, artist);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ALBUM);
	album = mval ? g_value_get_string(mval) : NULL;
	fail_if(album == NULL || strcmp(album, "Album 1"),
		"Album for '%s' is '%s' instead of expected 'Album 1'",
		clip_id, album);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Title 1"),
		"Title for '%s' is '%s' instead of expected 'Title 1'",
		clip_id, title);

        /* Album art is never returned in this unit test, as the
         * requested file actually doesn't exist */

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_video)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;

        RUNNING_CASE = "test_get_metadata_video";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_TITLE);

	/* Object ids to query */
	object_id =
		MAFW_TRACKER_SOURCE_UUID "::videos/"
                "%2Fhome%2Fuser%2FMyDocs%2Fvideo1.avi";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "video/x-msvideo"),
		"Mime type for '%s' is '%s' instead of expected 'video/x-msvideo'",
		clip_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Video 1"),
		"Title for '%s' is '%s' instead of expected 'Video 1'",
		clip_id, title);

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_artist_album_clip)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	const gchar *artist = NULL;
	const gchar *album = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;

        RUNNING_CASE = "test_get_metadata_artist_album_clip";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	/* Object ids to query */
	object_id =
		MAFW_TRACKER_SOURCE_UUID "::music/artists/Artist 1/Album 1/"
		"%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "audio/x-mp3"),
		"Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
		clip_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ARTIST);
	artist = mval ? g_value_get_string(mval) : NULL;
	fail_if(artist == NULL || strcmp(artist, "Artist 1"),
		"Artist for '%s' is '%s' instead of expected 'Artist 1'",
		clip_id, artist);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ALBUM);
	album = mval ? g_value_get_string(mval) : NULL;
	fail_if(album == NULL || strcmp(album, "Album 1"),
		"Album for '%s' is '%s' instead of expected 'Album 1'",
		clip_id, album);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Title 1"),
		"Title for '%s' is '%s' instead of expected 'Title 1'",
		clip_id, title);

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST


START_TEST(test_get_metadata_genre_artist_album_clip)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	const gchar *artist = NULL;
	const gchar *album = NULL;
	const gchar *genre = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;

        RUNNING_CASE = "test_get_metadata_genre_artist_album_clip";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_GENRE,
		MAFW_METADATA_KEY_TITLE);

	/* Object ids to query */
	object_id = MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre 1/Artist1/Album 1/"
                "%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
					   object_id, metadata_keys,
					   metadata_result_cb,
					   NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "audio/x-mp3"),
		"Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
		clip_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ARTIST);
	artist = mval ? g_value_get_string(mval) : NULL;
	fail_if(artist == NULL || strcmp(artist, "Artist 1"),
		"Artist for '%s' is '%s' instead of expected 'Artist 1'",
		clip_id, artist);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ALBUM);
	album = mval ? g_value_get_string(mval) : NULL;
	fail_if(album == NULL || strcmp(album, "Album 1"),
		"Album for '%s' is '%s' instead of expected 'Album 1'",
		clip_id, album);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_GENRE);
	genre = mval ? g_value_get_string(mval) : NULL;
	fail_if(genre == NULL || strcmp(genre, "Genre 1"),
		"Genre for '%s' is '%s' instead of expected 'Genre 1'",
		clip_id, genre);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Title 1"),
		"Title for '%s' is '%s' instead of expected 'Title 1'",
		clip_id, title);

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_playlist)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;
	gint duration, childcount;

        RUNNING_CASE = "test_get_metadata_playlist";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_DURATION,
		MAFW_METADATA_KEY_CHILDCOUNT);

	object_id =
		MAFW_TRACKER_SOURCE_UUID "::music/playlists/"
                "%2Ftmp%2Fplaylist1.pls";


	create_temporal_playlist ("/tmp/playlist1.pls", 4);
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	unlink("/tmp/playlist1.pls");

	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* Check results */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "x-mafw/container"),
		"Mime type for '%s' is '%s' instead of expected 'x-mafw/container'",
		clip_id, mime);
	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_DURATION);
	duration = mval ? g_value_get_int(mval) : 0;
	fail_if(duration != 76,
		"Duration for '%s' is '%i' instead of expected '76'",
		clip_id, duration);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_CHILDCOUNT);
	childcount = mval ? g_value_get_int(mval) : 0;
	fail_if(childcount != 4,
		"Childcount for '%s' is '%i' instead of expected '4'",
		clip_id, childcount);

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_album_clip)
{
	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	gchar *clip_id = NULL;
	GHashTable *metadata = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	const gchar *artist = NULL;
	const gchar *album = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;

        RUNNING_CASE = "test_get_metadata_album_clip";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	/* Object ids to query */
	object_id =
		MAFW_TRACKER_SOURCE_UUID "::music/albums/Album 1/"
                "%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of 1 items returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	clip_id = g_strdup(((MetadataResult *)
			    (item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", clip_id);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "audio/x-mp3"),
		"Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
		clip_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ARTIST);
	artist = mval ? g_value_get_string(mval) : NULL;
	fail_if(artist == NULL || strcmp(artist, "Artist 1"),
		"Artist for '%s' is '%s' instead of expected 'Artist 1'",
		clip_id, artist);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_ALBUM);
	album = mval ? g_value_get_string(mval) : NULL;
	fail_if(album == NULL || strcmp(album, "Album 1"),
		"Album for '%s' is '%s' instead of expected 'Album 1'",
		clip_id, album);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Title 1"),
		"Title for '%s' is '%s' instead of expected 'Title 1'",
		clip_id, title);

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_invalid)
{
	const gchar *const *metadata_keys = NULL;
	gchar *clip_id = NULL;
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

        RUNNING_CASE = "test_get_metadata_invalid";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_ARTIST,
		MAFW_METADATA_KEY_ALBUM,
		MAFW_METADATA_KEY_TITLE);

	/* Execute query */
        mafw_source_get_metadata(g_tracker_source,
                                  MAFW_TRACKER_SOURCE_UUID
                                  "::music/albums/Album%202/something-"
                                  "that-does-not-exist",
                                  metadata_keys,
                                  metadata_result_cb, NULL);

	/* Check results... */
	while (g_main_context_pending(context))
                g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_results != NULL,
		"Query of metadata of non existing objectid "
                "returned non NULL metadata");

	g_free(clip_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_albums)
{
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	GHashTable *metadata = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;
	gchar *category_id = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	gint duration, childcount;

        RUNNING_CASE = "test_get_metadata_albums";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_DURATION,
		MAFW_METADATA_KEY_CHILDCOUNT);

	/* Object ids to query */
	object_id = MAFW_TRACKER_SOURCE_UUID "::music/albums";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of a category returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	category_id = g_strdup(((MetadataResult *)
				(item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", category_id);


	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Albums"),
		"Title for '%s' is '%s' instead of expected 'Albums'",
		category_id, title);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "x-mafw/container"),
		"Mime type for '%s' is '%s' instead of expected ''",
		category_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_DURATION);
	duration = mval ? g_value_get_int(mval) : 0;
	fail_if(duration != 612,
		"Duration for '%s' is '%i' instead of expected '612'",
		category_id, duration);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_CHILDCOUNT);
	childcount = mval ? g_value_get_int(mval) : 0;
	fail_if(childcount != 9,
		"Childcount for '%s' is '%i' instead of expected '9'",
		category_id, childcount);

	g_free(category_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_music)
{
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	GHashTable *metadata = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;
	gchar *category_id = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	gint duration, childcount;

        RUNNING_CASE = "test_get_metadata_music";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_DURATION,
		MAFW_METADATA_KEY_CHILDCOUNT);

	/* Object ids to query */
	object_id = MAFW_TRACKER_SOURCE_UUID "::music";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of a category returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	category_id = g_strdup(((MetadataResult *)
				(item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", category_id);


	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Music"),
		"Title for '%s' is '%s' instead of expected 'Music'",
		category_id, title);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "x-mafw/container"),
		"Mime type for '%s' is '%s' instead of expected ''",
		category_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_DURATION);
	duration = mval ? g_value_get_int(mval) : 0;
	fail_if(duration != 612,
		"Duration for '%s' is '%i' instead of expected '612'",
		category_id, duration);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_CHILDCOUNT);
	childcount = mval ? g_value_get_int(mval) : 0;
	fail_if(childcount != 5,
		"Childcount for '%s' is '%i' instead of expected '5'",
		category_id, childcount);

	g_free(category_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST


START_TEST(test_get_metadata_videos)
{
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	GHashTable *metadata = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;
	gchar *category_id = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	gint duration, childcount;

        RUNNING_CASE = "test_get_metadata_videos";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_DURATION,
		MAFW_METADATA_KEY_CHILDCOUNT);

	/* Object ids to query */
	object_id = MAFW_TRACKER_SOURCE_UUID "::videos";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of a category returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	category_id = g_strdup(((MetadataResult *)
				(item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", category_id);


	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Videos"),
		"Title for '%s' is '%s' instead of expected 'Videos'",
		category_id, title);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "x-mafw/container"),
		"Mime type for '%s' is '%s' instead of expected ''",
		category_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_DURATION);
	duration = mval ? g_value_get_int(mval) : 0;
	fail_if(duration != 53,
		"Duration for '%s' is '%i' instead of expected '53'",
		category_id, duration);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_CHILDCOUNT);
	childcount = mval ? g_value_get_int(mval) : 0;
	fail_if(childcount != 2,
		"Childcount for '%s' is '%i' instead of expected '2'",
		category_id, childcount);

	g_free(category_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_get_metadata_root)
{
	GMainLoop *loop = NULL;
	GMainContext *context = NULL;

	const gchar *const *metadata_keys = NULL;
	gchar *object_id = NULL;
	GHashTable *metadata = NULL;
	GList *item_metadata = NULL;
	GValue *mval = NULL;
	gchar *category_id = NULL;
	const gchar *mime = NULL;
	const gchar *title = NULL;
	gint duration, childcount;

        RUNNING_CASE = "test_get_metadata_root";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are interested in */
	metadata_keys = MAFW_SOURCE_LIST(
		MAFW_METADATA_KEY_TITLE,
		MAFW_METADATA_KEY_MIME,
		MAFW_METADATA_KEY_DURATION,
		MAFW_METADATA_KEY_CHILDCOUNT);

	/* Object ids to query */
	object_id = MAFW_TRACKER_SOURCE_UUID "::";

         /* Execute query */
	mafw_source_get_metadata(g_tracker_source,
				  object_id, metadata_keys,
				  metadata_result_cb,
				  NULL);

	/* Check results... */
	while (g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_metadata_called == FALSE,
		"No metadata_result signal received");

	fail_if(g_list_length(g_metadata_results) != 1,
		"Query metadata of a category returned %d results",
		g_list_length(g_metadata_results));

	item_metadata = g_metadata_results;

	/* ...for first clip */
	category_id = g_strdup(((MetadataResult *)
				(item_metadata->data))->objectid);
	metadata = (((MetadataResult *) (item_metadata->data))->metadata);

	fail_if(metadata == NULL,
		"Did not receive metadata for item '%s'", category_id);


	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_TITLE);
	title = mval ? g_value_get_string(mval) : NULL;
	fail_if(title == NULL || strcmp(title, "Root"),
		"Title for '%s' is '%s' instead of expected 'Root'",
		category_id, title);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_MIME);
	mime = mval ? g_value_get_string(mval) : NULL;
	fail_if(mime == NULL || strcmp(mime, "x-mafw/container"),
		"Mime type for '%s' is '%s' instead of expected ''",
		category_id, mime);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_DURATION);
	duration = mval ? g_value_get_int(mval) : 0;
	fail_if(duration != 665,
		"Duration for '%s' is '%i' instead of expected '665'",
		category_id, duration);

	mval = mafw_metadata_first(metadata,
				   MAFW_METADATA_KEY_CHILDCOUNT);
	childcount = mval ? g_value_get_int(mval) : 0;
	fail_if(childcount != 2,
		"Childcount for '%s' is '%i' instead of expected '2'",
		category_id, childcount);

	g_free(category_id);

        clear_metadata_results();

	g_main_loop_unref(loop);
}
END_TEST

static void
metadata_set_cb(MafwSource *self,
		const gchar *object_id,
		const gchar **failed_keys,
		gpointer user_data,
		const GError *error)
{
	gint i = 0;

	g_set_metadata_called = TRUE;

	if (error) {
		g_set_metadata_error = TRUE;
	}

	if (failed_keys != NULL) {
		while (failed_keys[i] != NULL) {
			g_set_metadata_failed_keys =
				g_list_append(g_set_metadata_failed_keys,
					      g_strdup(failed_keys[i]));
			i++;
		}
	}
}

START_TEST(test_set_metadata_audio)
{
	GMainLoop *loop;
	GMainContext *context;
	GHashTable *metadata;
	GTimeVal timeval;

	RUNNING_CASE = "test_set_metadata_audio";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are going to set */
	g_time_val_from_iso8601("2008-11-05T08:27:01Z", &timeval);
	metadata = mafw_metadata_new();
	mafw_metadata_add_long(metadata,
				MAFW_METADATA_KEY_LAST_PLAYED,
				timeval.tv_sec);
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PLAY_COUNT,
			       1);

	g_print("> Set metadata audio.../n");
	/* Set metadata */
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error == TRUE,
		"Error received during set metadata operation");

	fail_if(g_set_metadata_failed_keys != NULL,
		"Set metadata operation failed");

	clear_set_metadata_results();
	g_main_loop_unref(loop);
}
END_TEST


START_TEST(test_set_metadata_video)
{
	GMainLoop *loop;
	GMainContext *context;
	GHashTable *metadata;

	RUNNING_CASE = "test_set_metadata_video";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Metadata we are going to set */
	metadata = mafw_metadata_new();
	mafw_metadata_add_str(metadata,
			       MAFW_METADATA_KEY_PAUSED_THUMBNAIL_URI,
			       "/home/user/thumbnail.png");
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PAUSED_POSITION,
			       10);

	g_print("> Set metadata video.../n");
	/* Set metadata */
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::videos/%2Fhome%2Fuser%2FMyDocs%2Fvideo.avi",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error == TRUE,
		"Error received during set metadata operation");

	fail_if(g_set_metadata_failed_keys != NULL,
		"Set metadata operation failed");

	clear_set_metadata_results();
	g_main_loop_unref(loop);

}
END_TEST

START_TEST(test_set_metadata_invalid)
{
	GMainLoop *loop;
	GMainContext *context;
	GHashTable *metadata;

	RUNNING_CASE = "test_set_metadata_invalid_non_writable";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* 1. Non-writable metadata */

	metadata = mafw_metadata_new();
	mafw_metadata_add_str(metadata,
			       MAFW_METADATA_KEY_ARTIST,
			       "Artist X");

	g_print("> Trying to modify a non-writable metadata.../n");
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip2.mp3",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error != TRUE,
		"Error not received when trying to modify a non-writable "
		"metadata");

	fail_if(g_set_metadata_failed_keys == NULL,
		"Metadata failed keys should contain the keys that couldn't be "
		"updated");
	fail_if(g_list_length(g_set_metadata_failed_keys) != 1,
		"The number of failed keys reported is incorrect");

	clear_set_metadata_results();

        /* 2. Mixing audio and video metadata keys in the same set_metadata
	   operation */

	RUNNING_CASE = "test_set_metadata_invalid_mixed";
	metadata = mafw_metadata_new();
	/* Video metadata*/
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PAUSED_POSITION,
			       10);
	/* Audio metadata */
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PLAY_COUNT,
			       1);
	g_print("> Trying to modify audio and video metadata at the same "
		"time.../n");
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip3.mp3",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error != TRUE,
		"Error not received when trying to modify audio and video "
		"metadata at the same time");

	fail_if(g_set_metadata_failed_keys == NULL,
		"Metadata failed keys should contain the keys that couldn't be "
		"updated");

	fail_if(g_list_length(g_set_metadata_failed_keys) != 2,
		"The number of failed keys reported is incorrect");

	clear_set_metadata_results();

	/* 3. Trying to set metadata of a non-existing clip */

	RUNNING_CASE = "test_set_metadata_invalid_non_existing_clip";
	metadata = mafw_metadata_new();
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PLAY_COUNT,
			       1);
	g_print("> Trying to modify metadata of a non-existing clip.../n");
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fnonexisting.mp3",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error != TRUE,
		"Error not received when trying to modify metadata from a non-existing file");

	fail_if(g_set_metadata_failed_keys == NULL,
		"Metadata failed keys should contain the keys that couldn't be"
		"updated");

	fail_if(g_list_length(g_set_metadata_failed_keys) != 1,
		"The number of failed keys reported is incorrect");


	clear_set_metadata_results();

	/* 4. Trying to set metadata of an invalid objectid */

	RUNNING_CASE = "test_set_metadata_invalid_objectid";
	metadata = mafw_metadata_new();
	mafw_metadata_add_int(metadata,
			       MAFW_METADATA_KEY_PLAY_COUNT,
			       1);
	g_print("> Trying to modify metadata of an invalid objectid.../n");
	mafw_source_set_metadata(g_tracker_source,
				  MAFW_TRACKER_SOURCE_UUID
				  "::music/ssongs",
				  metadata,
				  metadata_set_cb,
				  NULL);

	g_hash_table_unref(metadata);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_set_metadata_called == FALSE,
		"No set metadata signal received");

	fail_if(g_set_metadata_params_err == TRUE,
		"Error in the parameters from the tracker call");

	fail_if(g_set_metadata_error != TRUE,
		"Error not received when trying to modify metadata from a non-existing file");

	fail_if(g_set_metadata_failed_keys == NULL,
		"Metadata failed keys should contain the keys that couldn't be"
		"updated");

	fail_if(g_list_length(g_set_metadata_failed_keys) != 1,
		"The number of failed keys reported is incorrect");


	clear_set_metadata_results();
	g_main_loop_unref(loop);
}
END_TEST

static void
object_destroyed_cb(MafwSource *self,
		    const gchar *object_id,
		    gpointer user_data,
		    const GError *error)
{
	g_destroy_called = TRUE;

	if (error) {
		g_destroy_error = TRUE;
	}
}

START_TEST(test_destroy_item)
{
	GMainLoop *loop;
	GMainContext *context;

	RUNNING_CASE = "test_destroy_item";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Destroy clip2 item...\n");
	/* Destroy the item */
	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID
				    "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip2.mp3",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

	fail_if(g_destroy_error == TRUE,
		"Error received in destroy object callback");

	fail_if(g_destroy_results == NULL,
		"Destroy object operation failed");

	fail_if((g_list_length(g_destroy_results) != 1) ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 0),
			"/home/user/MyDocs/clip2.mp3"),
		 "Unexpected results in destroy object operation");

	clear_destroy_results();
	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_destroy_playlist)
{
	GMainLoop *loop;
	GMainContext *context;

	RUNNING_CASE = "test_destroy_playlist";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Destroy playlist item...\n");
	/* Destroy the playlist */
	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID
				    "::music/playlists/%2Fhome%2Fuser%2FMyDocs%2Fplaylist1.pls",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

	fail_if(g_destroy_error == TRUE,
		"Error received in destroy object callback");

	fail_if(g_destroy_results == NULL,
		"Destroy object operation failed");

	fail_if((g_list_length(g_destroy_results) != 1) ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 0),
			"/home/user/MyDocs/playlist1.pls"),
		 "Unexpected results in destroy object operation");

	clear_destroy_results();
	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_destroy_container)
{
	GMainLoop *loop;
	GMainContext *context;

	RUNNING_CASE = "test_destroy_container";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Destroy Artist 1 container...\n");
	/* Destroy the item */
	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID
				    "::music/artists/Artist%201",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

       	fail_if(g_destroy_error == TRUE,
		"Error received in destroy object callback");

	fail_if((g_list_length(g_destroy_results) != 5) ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 0),
			"/home/user/MyDocs/clip1.mp3") ||
		g_ascii_strcasecmp(
			 (gchar *) g_list_nth_data(g_destroy_results, 1),
			 "/home/user/MyDocs/clip3.mp3") ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 2),
			"/home/user/MyDocs/clip4.mp3") ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 3),
			"/home/user/MyDocs/clip5.wma") ||
		g_ascii_strcasecmp(
			(gchar *) g_list_nth_data(g_destroy_results, 4),
			"/home/user/MyDocs/clip6.wma"),
		"Unexpected results in destroy object operation");

	clear_destroy_results();
	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_destroy_invalid_category)
{
	GMainLoop *loop;
	GMainContext *context;

	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	/* Destroy an invalid category */
	RUNNING_CASE = "test_destroy_invalid_category";
	g_print("> Destroy an invalid category...\n");

	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID "::music/songs",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

	fail_if(g_destroy_error != TRUE,
		"Error not received when trying to destroy an invalid category");

	fail_if(g_destroy_results != NULL,
		"Objects destroyed when that is not expected");

	clear_destroy_results();

	/* Destroy a malformed objectid */
	RUNNING_CASE = "test_destroy_invalid";
	g_print("> Destroy a malformed objectid ...\n");

	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID "::music/songsss",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

	fail_if(g_destroy_error != TRUE,
		"Error not received when trying to destroy a malformed objectid");

	fail_if(g_destroy_results != NULL,
		"Objects destroyed when that is not expected");

	clear_destroy_results();

	g_main_loop_unref(loop);
}
END_TEST

START_TEST(test_destroy_failed)
{
	GMainLoop *loop;
	GMainContext *context;

	RUNNING_CASE = "test_destroy_failed";
	loop = g_main_loop_new(NULL, FALSE);
	context = g_main_loop_get_context(loop);

	g_print("> Destroy a non-existent file...\n");
	/* Destroy the item */
	mafw_source_destroy_object(g_tracker_source,
				    MAFW_TRACKER_SOURCE_UUID
				    "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fnonexistent.mp3",
                                    object_destroyed_cb,
                                    NULL);

	while(g_main_context_pending(context))
		g_main_context_iteration(context, TRUE);

	fail_if(g_destroy_called == FALSE,
		"No destroy signal received");

	fail_if(g_destroy_error != TRUE,
		"Error not received when trying to destroy a non-existent file");

       	fail_if(g_destroy_results != NULL,
		"Objects destroyed when that is not expected");

	clear_destroy_results();
	g_main_loop_unref(loop);
}
END_TEST

/* ---------------------------------------------------- */
/*                  Suite creation                      */
/* ---------------------------------------------------- */
SRunner * configure_tests(void)
{
	SRunner *sr = NULL;
	Suite *s = NULL;

	checkmore_wants_dbus();
	/* Create the suite */
	s = suite_create("MafwTrackerSource");

	/* Create test cases */
	TCase *tc_browse = tcase_create("Browse");
	TCase *tc_get_metadata = tcase_create("GetMetadata");
	TCase *tc_set_metadata = tcase_create("SetMetadata");
	TCase *tc_destroy = tcase_create("DestroyObject");

	/* Create unit tests for test case "Browse" */
	tcase_add_checked_fixture(tc_browse, fx_setup_dummy_tracker_source,
				  fx_teardown_dummy_tracker_source);

	tcase_add_test(tc_browse, test_browse_root);
	tcase_add_test(tc_browse, test_browse_music);
	tcase_add_test(tc_browse, test_browse_music_artists);
	tcase_add_test(tc_browse, test_browse_music_artists_artist1);
	tcase_add_test(tc_browse, test_browse_music_artists_unknown);
	tcase_add_test(tc_browse, test_browse_music_artists_unknown_unknown);
	tcase_add_test(tc_browse, test_browse_music_artists_artist1_album3);
	tcase_add_test(tc_browse, test_browse_music_albums);
	tcase_add_test(tc_browse, test_browse_music_albums_album4);
	tcase_add_test(tc_browse, test_browse_music_genres);
	tcase_add_test(tc_browse, test_browse_music_genres_genre2);
	tcase_add_test(tc_browse, test_browse_music_genres_unknown);
	tcase_add_test(tc_browse, test_browse_music_genres_genre2_artist2);
	tcase_add_test(tc_browse, test_browse_music_genres_genre2_artist2_album2);
	tcase_add_test(tc_browse, test_browse_music_songs);
	tcase_add_test(tc_browse, test_browse_music_playlists);
	tcase_add_test(tc_browse, test_browse_music_playlists_playlist1);
	tcase_add_test(tc_browse, test_browse_videos);
	tcase_add_test(tc_browse, test_browse_count);
	tcase_add_test(tc_browse, test_browse_offset);
	tcase_add_test(tc_browse, test_browse_invalid);
	tcase_add_test(tc_browse, test_browse_cancel);
	tcase_add_test(tc_browse, test_browse_recursive);
	tcase_add_test(tc_browse, test_browse_filter);
/* 	tcase_add_test(tc_browse, test_browse_sort); */

	suite_add_tcase(s, tc_browse);

	/* Create unit tests for test case "GetMetadata" */
	tcase_add_checked_fixture(tc_get_metadata, fx_setup_dummy_tracker_source,
				  fx_teardown_dummy_tracker_source);

	tcase_add_test(tc_get_metadata, test_get_metadata_clip);
	tcase_add_test(tc_get_metadata, test_get_metadata_video);
	tcase_add_test(tc_get_metadata, test_get_metadata_playlist);
	tcase_add_test(tc_get_metadata, test_get_metadata_artist_album_clip);
	tcase_add_test(tc_get_metadata, test_get_metadata_genre_artist_album_clip);
	tcase_add_test(tc_get_metadata, test_get_metadata_album_clip);
	tcase_add_test(tc_get_metadata, test_get_metadata_invalid);
	tcase_add_test(tc_get_metadata, test_get_metadata_albums);
	tcase_add_test(tc_get_metadata, test_get_metadata_music);
	tcase_add_test(tc_get_metadata, test_get_metadata_videos);
	tcase_add_test(tc_get_metadata, test_get_metadata_root);

	suite_add_tcase(s, tc_get_metadata);

	/* Create unit tests for test case "SetMetadata" */
	tcase_add_checked_fixture(tc_set_metadata, fx_setup_dummy_tracker_source,
				  fx_teardown_dummy_tracker_source);

	tcase_add_test(tc_set_metadata, test_set_metadata_audio);
	tcase_add_test(tc_set_metadata, test_set_metadata_video);
	tcase_add_test(tc_set_metadata, test_set_metadata_invalid);

	suite_add_tcase(s, tc_set_metadata);

	/* Create unit tests for test case "DestroyObject" */
	tcase_add_checked_fixture(tc_destroy, fx_setup_dummy_tracker_source,
				  fx_teardown_dummy_tracker_source);

	tcase_add_test(tc_destroy, test_destroy_item);
	tcase_add_test(tc_destroy, test_destroy_playlist);
	tcase_add_test(tc_destroy, test_destroy_container);
	tcase_add_test(tc_destroy, test_destroy_invalid_category);
	tcase_add_test(tc_destroy, test_destroy_failed);

	suite_add_tcase(s, tc_destroy);

	/*Valgrind may require more time to run*/
	tcase_set_timeout(tc_browse, 60);
	tcase_set_timeout(tc_get_metadata, 60);
	tcase_set_timeout(tc_set_metadata, 60);
	tcase_set_timeout(tc_destroy, 60);

	/* Create srunner object with the test suite */
	sr = srunner_create(s);

	return sr;
}

/* ---------------------------------------------------- */
/*                      MOCKUPS                         */
/* ---------------------------------------------------- */

#define DB_SIZE 18

#define DB_FILENAME 0
#define DB_TITLE 1
#define DB_ARTIST 2
#define DB_ALBUM 3
#define DB_GENRE 4
#define DB_MIME 5
#define DB_LENGTH 6

gchar *DB[DB_SIZE][7] = {
        /*00*/{"/home/user/MyDocs/SomeSong.mp3", "Some Title", "Some Artist", "Some Album", "Some Genre", "audio/x-mp3", "36"},
        /*01*/{"/home/user/MyDocs/clip1.mp3", "Title 1", "Artist 1", "Album 1", "Genre 1", "audio/x-mp3", "23"},
        /*02*/{"/home/user/MyDocs/clip2.mp3", "Title 2", "Artist 2", "Album 2", "Genre 2", "audio/x-mp3", "17"},
        /*03*/{"/home/user/MyDocs/clip3.mp3", "Title 3", "Artist 1", "Album 3", "Genre 2", "audio/x-mp3", "76"},
        /*04*/{"/home/user/MyDocs/clip4.mp3", "Title 4", "Artist 1", "Album 3", "Genre 1", "audio/x-mp3", "42"},
        /*05*/{"/home/user/MyDocs/clip5.wma", "Title 5", "Artist 1", "Album 3", "Genre 2", "audio/x-wma", "21"},
        /*06*/{"/home/user/MyDocs/clip6.wma", "Title 6", "Artist 1", "Album 3", "Genre 1", "audio/x-wma", "90"},
        /*07*/{"/home/user/MyDocs/clip7.mp3", "", "", "", "", "audio/x-mp3", "70"},
        /*08*/{"/home/user/MyDocs/clip8.mp3", "Title V2", "Artist V2", "Album V2", "", "audio/x-mp3", "64"},
        /*09*/{"/home/user/MyDocs/unknown-album.mp3", "Title 7", "Artist 4", "", "Genre 2", "audio/x-mp3", "8"},
        /*10*/{"/home/user/MyDocs/unknown-artist.mp3", "Title 8", "", "Album 4", "Genre 2", "audio/x-mp3", "47"},
        /*11*/{"/home/user/MyDocs/unknown-title.mp3", "", "Artist 4", "Album 4", "Genre 2", "audio/x-mp3", "57"},
        /*12*/{"/home/user/MyDocs/unknown-genre.mp3", "Title 9", "Artist 4", "Album 4", "", "audio/x-mp3", "12"},
        /*13*/{"/home/user/MyDocs/unknown.mp3", "", "", "", "", "audio/x-mp3", "49"},


        /*14*/{"/home/user/MyDocs/playlist1.pls", "", "", "", "", "audio/x-scpls", "0"},
        /*15*/{"/home/user/MyDocs/playlist2.m3u", "", "", "", "", "audio/x-mpegurl", "0"},
	/*16*/{"/home/user/MyDocs/video1.avi", "Video 1", "", "", "", "video/x-msvideo", "30"},
	/*17*/{"/home/user/MyDocs/video2.avi", "Video 2", "", "", "", "video/x-msvideo", "23"}
};

static void create_temporal_playlist (gchar *path, gint nitems)
{
	FILE *pf;
	gint i;

	pf = fopen(path, "w");
	if (pf != NULL) {
		gchar *lines = g_strdup_printf ("[playlist]\nNumberOfEntries=%d\n\n",
						nitems);
		fwrite (lines, strlen(lines), 1, pf);
		g_free (lines);
		/* Add some local items */
		for (i=0; i<(nitems - 1); i++) {
			gint p = i % DB_SIZE;
			gchar *file = DB[p][DB_FILENAME];
			lines = g_strdup_printf("File%d=file://%s\n", i+1, file);
			fwrite (lines, strlen(lines), 1, pf);
			g_free(lines);
		}
		/* Add a non-local item */
		lines = g_strdup_printf("File%d=http://www.mafwradio.com:8086\n",
					nitems);
		fwrite (lines, strlen(lines), 1, pf);
		g_free(lines);

		fclose(pf);
	}
}

static gboolean
_check_query_case(const gchar *actual_query)
{
        if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists") == 0) {
                return actual_query == NULL;
        } else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_destroy_container") == 0)) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String>Artist 1</rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String></rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown_unknown") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:and>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String></rdf:String>  </rdfq:equals>  <rdfq:equals>    <rdfq:Property name=\"Audio:Album\"/>    <rdf:String></rdf:String>  </rdfq:equals>  </rdfq:and></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1_album3") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                         "<rdfq:Condition>  <rdfq:and>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String>Artist 1</rdf:String>  </rdfq:equals>  <rdfq:equals>    <rdfq:Property name=\"Audio:Album\"/>    <rdf:String>Album 3</rdf:String>  </rdfq:equals>  </rdfq:and></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums") == 0) {
                return actual_query == NULL;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums_album4") == 0) {
                return g_ascii_strcasecmp(actual_query,
					  "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Album\"/>    <rdf:String>Album 4 - Artist 4</rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
       } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres") == 0) {
                return actual_query == NULL;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Genre\"/>    <rdf:String>Genre 2</rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_unknown") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Genre\"/>    <rdf:String></rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:and>  <rdfq:equals>    <rdfq:Property name=\"Audio:Genre\"/>    <rdf:String>Genre 2</rdf:String>  </rdfq:equals>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String>Artist 2</rdf:String>  </rdfq:equals>  </rdfq:and></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2_album2") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:and>  <rdfq:equals>    <rdfq:Property name=\"Audio:Genre\"/>    <rdf:String>Genre 2</rdf:String>  </rdfq:equals>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String>Artist 2</rdf:String>  </rdfq:equals>  <rdfq:equals>    <rdfq:Property name=\"Audio:Album\"/>    <rdf:String>Album 2</rdf:String>  </rdfq:equals>  </rdfq:and></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_songs") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_cancel") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_recursive_songs") == 0 ||
		   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_playlists") == 0 ||
		   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_videos") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music") == 0) {
                return actual_query == NULL;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_playlists_playlist1") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:inSet>    <rdfq:Property name=\"File:NameDelimited\"/>    <rdf:String>/home/user/MyDocs/SomeSong.mp3,/home/user/MyDocs/clip1.mp3,/home/user/MyDocs/clip2.mp3</rdf:String>  </rdfq:inSet></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_count") == 0 ||
		   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_offset") == 0) {
		return actual_query == NULL;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_recursive_artist1") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition>  <rdfq:equals>    <rdfq:Property name=\"Audio:Artist\"/>    <rdf:String>Artist 1</rdf:String>  </rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_filter_simple") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition><rdfq:equals><rdfq:Property name=\"Audio:Album\"/><rdf:String>Album 3</rdf:String></rdfq:equals></rdfq:Condition>") == 0;
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_filter_and") == 0) {
                return g_ascii_strcasecmp(actual_query,
                                          "<rdfq:Condition><rdfq:and><rdfq:equals><rdfq:Property name=\"Audio:Album\"/><rdf:String>Album 3</rdf:String></rdfq:equals><rdfq:equals><rdfq:Property name=\"Audio:Title\"/><rdf:String>Title 3</rdf:String></rdfq:equals></rdfq:and></rdfq:Condition>") == 0;
	} else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_albums") == 0) ||
 		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_music") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_videos") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_root") == 0)) {
		return actual_query == NULL;
        } else {
                return FALSE;
        }
}

static gboolean
_check_concat_case(gchar *actual_concat)
{
        if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists") == 0) ||
            (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2") == 0) ||
            (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_unknown") == 0)) {
                return g_ascii_strcasecmp(actual_concat, "Audio:Album") == 0;
        } else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres") == 0)) {
                return g_ascii_strcasecmp(actual_concat, "Audio:Artist") == 0;
	} else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_music") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_albums") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_videos") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_root") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2") == 0)) {
		return (actual_concat == NULL);
	} else {
                return FALSE;
        }
}

static gboolean
_check_count_case(gchar *actual_count)
{
	if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_albums") == 0) {
		return g_ascii_strcasecmp(actual_count, "Audio:Album") == 0;
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_music") == 0 ||
		   g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_videos") == 0 ||
		   g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_root") == 0) {
                return g_ascii_strcasecmp(actual_count, "*") == 0;
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_music") == 0) {
		return (g_ascii_strcasecmp(actual_count, "*") == 0);
	} else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_unknown") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2") == 0)) {
		return (actual_count == NULL);
        } else {
                return FALSE;
        }
}

static gboolean
_check_sum_case(gchar *actual_sum, ServiceType service)
{
	if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_unknown") == 0) ||
	    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2") == 0))	{
		return (actual_sum == NULL);
	}

	if (service == SERVICE_MUSIC) {
		return g_ascii_strcasecmp(actual_sum, "Audio:Duration") == 0;
	} else if (service == SERVICE_VIDEOS) {
		return g_ascii_strcasecmp(actual_sum, "Video:Duration") == 0;
	} else if (service == SERVICE_PLAYLISTS) {
                return g_ascii_strcasecmp(actual_sum, "Playlist:Duration") == 0;
        } else {
		return FALSE;
	}
}

static gboolean
_check_params(ServiceType service, const char *id, char **keys, char **values)
{
	GHashTable *metadata;
	gchar *value_1;
	gchar *value_2;
	gboolean retval = TRUE;
	gint i = 0;

	if ((keys == NULL) || (values == NULL) || (id == NULL))
		return FALSE;

	metadata = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	while (keys[i] != NULL) {
		g_hash_table_insert(metadata, keys[i], values[i]);
		i++;
	}

	if (g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_audio") == 0) {
		value_1 = (gchar *) g_hash_table_lookup(metadata, "Audio:LastPlay");
		value_2 = (gchar *) g_hash_table_lookup(metadata, "Audio:PlayCount");

		if ((service != SERVICE_MUSIC) ||
		    (g_ascii_strcasecmp(id, "/home/user/MyDocs/clip1.mp3") != 0) ||
		    (g_ascii_strcasecmp(value_1, "2008-11-05T08:27:01Z") != 0) ||
		    (g_ascii_strcasecmp(value_2, "1") != 0)) {
			retval = FALSE;
		}
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_video") == 0) {
		value_1 = (gchar *) g_hash_table_lookup(metadata, "Video:LastPlayedFrame");
		value_2 = (gchar *) g_hash_table_lookup(metadata, "Video:PausePosition");

		if ((service != SERVICE_VIDEOS) ||
		    (g_ascii_strcasecmp(id, "/home/user/MyDocs/video.avi") != 0) ||
		    (g_ascii_strcasecmp(value_1, "/home/user/thumbnail.png") != 0) ||
		    (g_ascii_strcasecmp(value_2, "10") != 0)) {
			retval = FALSE;
		}
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_invalid_mixed") == 0 ) {
		value_1 = (gchar *) g_hash_table_lookup(metadata, "Video:PausePosition");
		value_2 = (gchar *) g_hash_table_lookup(metadata, "Audio:PlayCount");

		if ((g_ascii_strcasecmp(id, "/home/user/MyDocs/clip3.mp3") != 0) ||
		    (g_ascii_strcasecmp(value_1, "10") != 0) ||
		    (g_ascii_strcasecmp(value_2, "1") != 0)) {
			    retval = FALSE;
		    }
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_invalid_non_existing_clip") == 0) {
		value_1 = (gchar *) g_hash_table_lookup(metadata, "Audio:PlayCount");

		if ((service != SERVICE_MUSIC) ||
		    (g_ascii_strcasecmp(id, "/home/user/MyDocs/nonexisting.mp3") != 0) ||
		    (g_ascii_strcasecmp(value_1, "1") != 0)) {
			retval = FALSE;
		}
	}

	g_hash_table_destroy(metadata);
	/* Set an error if the parameters in the tracker call are incorrect */
	g_set_metadata_params_err = !retval;
	return retval;
}

static void
_add_concat_count_and_sum_to_result(GPtrArray *result,
                                    gint index,
                                    gchar **keys,
                                    gchar *concat,
                                    const gchar *count,
                                    const gchar *sum)
{
        gchar **tuple;
        gint i = 0;

        tuple = g_new0(gchar *, g_strv_length(keys) + 4);

        i = 0;
        while (keys[i]) {
                if (g_ascii_strcasecmp(keys[i], "File:NameDelimited") == 0)
                        tuple[i] = g_strdup(DB[index][DB_FILENAME]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Title") == 0)
                        tuple[i] = g_strdup(DB[index][DB_TITLE]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Artist") == 0)
                        tuple[i] = g_strdup(DB[index][DB_ARTIST]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Album") == 0)
                        tuple[i] = g_strdup(DB[index][DB_ALBUM]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Genre") == 0)
                        tuple[i] = g_strdup(DB[index][DB_GENRE]);
                else if (g_ascii_strcasecmp(keys[i], "File:Mime") == 0)
                        tuple[i] = g_strdup(DB[index][DB_MIME]);
                i++;
        }

	if (concat) {
		tuple[i++] = g_strdup(concat);
	}

        tuple[i++] = g_strdup(count);
        tuple[i++] = g_strdup(sum);

        g_ptr_array_add(result, tuple);

}

static void
_add_query_to_result(GPtrArray *result,
                     gint index,
                     gchar **keys)
{
        gchar **tuple;
        gint i = 0;

        tuple = g_new0(gchar *, g_strv_length(keys) + 3);

        /* First item contains uri */
        tuple[0] = g_strdup(DB[index][DB_FILENAME]);
        /* Second item is service */
        tuple[1] = g_strdup(SERVICE_MUSIC_STR);

        i = 0;
        while (keys[i]) {
                if (g_ascii_strcasecmp(keys[i], "File:NameDelimited") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_FILENAME]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Title") == 0 || g_ascii_strcasecmp(keys[i], "Video:Title") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_TITLE]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Artist") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_ARTIST]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Album") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_ALBUM]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Genre") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_GENRE]);
                else if (g_ascii_strcasecmp(keys[i], "File:Mime") == 0)
                        tuple[i+2] = g_strdup(DB[index][DB_MIME]);
                i++;
        }

        g_ptr_array_add(result, tuple);
}

static char **
_get_metadata(gint index,
              char **keys)
{
        char **result;
        gint i;

        result = g_new0(char *, g_strv_length(keys) + 1);

        i=0;
        while (keys[i]) {
                if (g_ascii_strcasecmp(keys[i], "File:NameDelimited") == 0)
                        result[i] = g_strdup(DB[index][DB_FILENAME]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Title") == 0)
                        result[i] = g_strdup(DB[index][DB_TITLE]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Artist") == 0)
                        result[i] = g_strdup(DB[index][DB_ARTIST]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Album") == 0)
                        result[i] = g_strdup(DB[index][DB_ALBUM]);
                else if (g_ascii_strcasecmp(keys[i], "Audio:Genre") == 0)
                        result[i] = g_strdup(DB[index][DB_GENRE]);
                else if (g_ascii_strcasecmp(keys[i], "File:Mime") == 0)
                        result[i] = g_strdup(DB[index][DB_MIME]);
                else if (g_ascii_strcasecmp(keys[i], "Video:Title") == 0)
                        result[i] = g_strdup(DB[index][DB_TITLE]);
		else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_playlist") == 0) &&
			 (g_ascii_strcasecmp(keys[i], "Playlist:Duration") == 0))
			result[i] = g_strdup("76");
		else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_playlist") == 0) &&
			 (g_ascii_strcasecmp(keys[i], "Playlist:Songs") == 0))
			result[i]= g_strdup("4");
		else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_playlist") == 0) &&
			 (g_ascii_strcasecmp(keys[i], "Playlist:ValidDuration") == 0))
			result[i] = g_strdup("1");
                i++;
        }

        return result;
}


static void
_send_metadata(gint index,
               char **keys,
               TrackerArrayReply cb,
               gpointer user_data)
{
        char **result = NULL;
        GError *error = NULL;

        if (index < 0) {
                /* Domain and code are not relevant */
                error = g_error_new(1, 1, "error getting metadata");
                cb(result, error, user_data);
                g_error_free(error);
        } else {
                result = _get_metadata(index, keys);
                cb(result, NULL, user_data);
        }
}

static void
_send_concat_count_and_sum_expected_result(TrackerGPtrArrayReply callback,
                                           char **keys,
                                           ServiceType service,
                                           gpointer user_data)
{
        GPtrArray *result;

        if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists") == 0) {
                result = g_ptr_array_sized_new(8);
                _add_concat_count_and_sum_to_result(result, 0, keys, "Some Album", "1", "36");
                _add_concat_count_and_sum_to_result(result, 1, keys, "Album 1|Album 3", "2", "252");
                _add_concat_count_and_sum_to_result(result, 2, keys, "Album 2", "1", "17");
                _add_concat_count_and_sum_to_result(result, 7, keys, "|Album 4", "2", "166");
                _add_concat_count_and_sum_to_result(result, 8, keys, "Album V2", "1", "64");
                _add_concat_count_and_sum_to_result(result, 9, keys, "|Album 4", "2", "77");
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums") == 0) {
                result = g_ptr_array_sized_new(7);
                _add_concat_count_and_sum_to_result(result, 0, keys, "Some Artist", "1", "36");
                _add_concat_count_and_sum_to_result(result, 1, keys, "Artist 1", "1", "23");
                _add_concat_count_and_sum_to_result(result, 2, keys, "Artist 2", "1", "17");
                _add_concat_count_and_sum_to_result(result, 3, keys, "Artist 1", "4", "229");
                _add_concat_count_and_sum_to_result(result, 7, keys, "|Artist 4", "3", "127");
                _add_concat_count_and_sum_to_result(result, 8, keys, "Artist V2", "1", "64");
                _add_concat_count_and_sum_to_result(result, 10, keys, "|Artist 4", "3", "116");
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2") == 0) {
                result = g_ptr_array_sized_new(4);
                _add_concat_count_and_sum_to_result(result, 2, keys, "Album 2", "1", "17");
                _add_concat_count_and_sum_to_result(result, 3, keys, "Album 3", "1", "97");
                _add_concat_count_and_sum_to_result(result, 9, keys, "|Album 4", "2", "65");
                _add_concat_count_and_sum_to_result(result, 10, keys, "|Album 4", "2", "65");
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_unknown") == 0) {
                result = g_ptr_array_sized_new(3);
                _add_concat_count_and_sum_to_result(result, 7, keys, "", "1", "119");
                _add_concat_count_and_sum_to_result(result, 8, keys, "Album V2", "1", "64");
                _add_concat_count_and_sum_to_result(result, 12, keys, "Album 4", "1", "12");
	} else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_music") == 0) ||
		   (service == SERVICE_MUSIC &&
		    (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0))) {
		result = g_ptr_array_sized_new(2);
		_add_concat_count_and_sum_to_result(result, 0, keys, NULL ,"12", "501");
		_add_concat_count_and_sum_to_result(result, 5, keys, NULL, "2", "111");
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_albums") == 0) {
		result = g_ptr_array_sized_new(9);
		_add_concat_count_and_sum_to_result(result, 0, keys, NULL, "1", "36");
		_add_concat_count_and_sum_to_result(result, 1, keys, NULL, "1", "23");
		_add_concat_count_and_sum_to_result(result, 2, keys, NULL, "1", "17");
		_add_concat_count_and_sum_to_result(result, 3, keys, NULL, "4", "229");
		_add_concat_count_and_sum_to_result(result, 7, keys, NULL, "2", "119");
		_add_concat_count_and_sum_to_result(result, 8, keys, NULL, "1", "64");
		_add_concat_count_and_sum_to_result(result, 9, keys, NULL, "1", "8");
		_add_concat_count_and_sum_to_result(result, 10, keys, NULL, "1", "47");
		_add_concat_count_and_sum_to_result(result, 12, keys, NULL, "2", "69");
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_videos") == 0 ||
                   (service == SERVICE_VIDEOS &&
                    g_ascii_strcasecmp(RUNNING_CASE, "test_browse_root") == 0)) {
		result = g_ptr_array_sized_new(1);
		_add_concat_count_and_sum_to_result(result, 16, keys, NULL, "2", "53");
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_root") == 0) {
                if (service == SERVICE_MUSIC) {
			result = g_ptr_array_sized_new(2);
			_add_concat_count_and_sum_to_result(result, 0, keys, NULL, "12", "501");
			_add_concat_count_and_sum_to_result(result, 5, keys, NULL, "2", "111");
		} else if (service == SERVICE_VIDEOS) {
			result = g_ptr_array_sized_new(1);
			_add_concat_count_and_sum_to_result(result, 16, keys, NULL, "2", "53");
		} else {
			return;
		}
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music") == 0) {
                if (g_ascii_strcasecmp(keys[0], "Audio:Album") == 0) {
                        result = g_ptr_array_sized_new(9);
                        _add_concat_count_and_sum_to_result(result, 0, keys, NULL, "1", "36");
                        _add_concat_count_and_sum_to_result(result, 1, keys, NULL, "1", "23");
                        _add_concat_count_and_sum_to_result(result, 2, keys, NULL, "1", "17");
                        _add_concat_count_and_sum_to_result(result, 3, keys, NULL, "4", "229");
                        _add_concat_count_and_sum_to_result(result, 7, keys, NULL, "2", "119");
                        _add_concat_count_and_sum_to_result(result, 8, keys, NULL, "1", "64");
                        _add_concat_count_and_sum_to_result(result, 9, keys, NULL, "1", "8");
                        _add_concat_count_and_sum_to_result(result, 10, keys, NULL,"1", "47");
                        _add_concat_count_and_sum_to_result(result, 12, keys, NULL, "2", "69");
                } else if (g_ascii_strcasecmp(keys[0], "Audio:Artist") == 0) {
                        result = g_ptr_array_sized_new(6);
                        _add_concat_count_and_sum_to_result(result, 0, keys, NULL, "1", "36");
                        _add_concat_count_and_sum_to_result(result, 1, keys, NULL, "5", "252");
                        _add_concat_count_and_sum_to_result(result, 2, keys, NULL, "1", "17");
                        _add_concat_count_and_sum_to_result(result, 7, keys, NULL, "3", "166");
                        _add_concat_count_and_sum_to_result(result, 8, keys, NULL, "1", "64");
                        _add_concat_count_and_sum_to_result(result, 9, keys, NULL, "3", "77");
                } else if (g_ascii_strcasecmp(keys[0], "Audio:Genre") == 0) {
                        result = g_ptr_array_sized_new(4);
                        _add_concat_count_and_sum_to_result(result, 0, keys, NULL, "1", "36");
                        _add_concat_count_and_sum_to_result(result, 1, keys, NULL, "3", "155");
                        _add_concat_count_and_sum_to_result(result, 2, keys, NULL, "6", "226");
                        _add_concat_count_and_sum_to_result(result, 7, keys, NULL, "4", "195");
                } else if (g_ascii_strcasecmp(keys[0], "File:Mime") == 0) {
                        if (service == SERVICE_MUSIC) {
                                result = g_ptr_array_sized_new(2);
                                _add_concat_count_and_sum_to_result(result, 0, keys, NULL, "12", "501");
                                _add_concat_count_and_sum_to_result(result, 5, keys, NULL, "2", "111");
                        } else if (service == SERVICE_PLAYLISTS) {
                                result = g_ptr_array_sized_new(2);
                                _add_concat_count_and_sum_to_result(result, 14, keys, NULL, "1", "0");
                                _add_concat_count_and_sum_to_result(result, 15, keys, NULL, "1", "0");
                        } else {
				return;
			}
                } else {
			return;
		}
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_concat_count_and_sum_to_result(result, 1, keys, NULL, "1", "23");
                _add_concat_count_and_sum_to_result(result, 3, keys, NULL, "4", "229");
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_concat_count_and_sum_to_result(result, 7, keys, NULL, "2", "119");
                _add_concat_count_and_sum_to_result(result, 11, keys, NULL, "1", "47");
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres") == 0) {
                result = g_ptr_array_sized_new(4);
                _add_concat_count_and_sum_to_result(result, 0, keys, NULL, "1", "36");
                _add_concat_count_and_sum_to_result(result, 1, keys, NULL, "1", "155");
                _add_concat_count_and_sum_to_result(result, 2, keys, NULL, "4", "226");
                _add_concat_count_and_sum_to_result(result, 7, keys, NULL, "3", "195");
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2") == 0) {
                result = g_ptr_array_sized_new(1);
                _add_concat_count_and_sum_to_result(result, 2, keys, NULL, "1", "17");
        } else {
                return;
        }

        callback(result, NULL, user_data);
}

static void
_send_query_expected_result(TrackerGPtrArrayReply callback,
                            int offset,
                            int max_hits,
                            char **keys,
                            gpointer user_data)
{
        GPtrArray *result = NULL;

        if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_unknown_unknown") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_query_to_result(result, 7, keys);
                _add_query_to_result(result, 13, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_artists_artist1_album3") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_query_to_result(result, 3, keys);
                _add_query_to_result(result, 4, keys);
                _add_query_to_result(result, 5, keys);
                _add_query_to_result(result, 6, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_albums_album4") == 0) {
                result = g_ptr_array_sized_new(3);
                _add_query_to_result(result, 10, keys);
                _add_query_to_result(result, 11, keys);
                _add_query_to_result(result, 12, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_genres_genre2_artist2_album2") == 0) {
                result = g_ptr_array_sized_new(1);
                _add_query_to_result(result, 2, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_songs") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_cancel") == 0 ||
                   g_ascii_strcasecmp(RUNNING_CASE, "test_browse_recursive_songs") == 0) {
                result = g_ptr_array_sized_new(15);
                _add_query_to_result(result, 0, keys);
                _add_query_to_result(result, 1, keys);
                _add_query_to_result(result, 2, keys);
                _add_query_to_result(result, 3, keys);
                _add_query_to_result(result, 4, keys);
                _add_query_to_result(result, 5, keys);
                _add_query_to_result(result, 6, keys);
                _add_query_to_result(result, 7, keys);
                _add_query_to_result(result, 8, keys);
                _add_query_to_result(result, 9, keys);
                _add_query_to_result(result, 10, keys);
                _add_query_to_result(result, 11, keys);
                _add_query_to_result(result, 12, keys);
                _add_query_to_result(result, 13, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_playlists") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_query_to_result(result, 14, keys);
                _add_query_to_result(result, 15, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_music_playlists_playlist1") == 0) {
                result = g_ptr_array_sized_new(3);
                _add_query_to_result(result, 0, keys);
                _add_query_to_result(result, 1, keys);
                _add_query_to_result(result, 2, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_count") == 0) {
                switch (max_hits) {
                case 13:
                        result = g_ptr_array_sized_new(13);
                        break;
                case 14:
                case 15:
                case G_MAXINT:
                        result = g_ptr_array_sized_new(14);
                        break;
		default:
			return;
                }
                _add_query_to_result(result, 0, keys);
                _add_query_to_result(result, 1, keys);
                _add_query_to_result(result, 2, keys);
                _add_query_to_result(result, 3, keys);
                _add_query_to_result(result, 4, keys);
                _add_query_to_result(result, 5, keys);
                _add_query_to_result(result, 6, keys);
                _add_query_to_result(result, 7, keys);
                _add_query_to_result(result, 8, keys);
                _add_query_to_result(result, 9, keys);
                _add_query_to_result(result, 10, keys);
                _add_query_to_result(result, 11, keys);
                _add_query_to_result(result, 12, keys);
                switch (max_hits) {
		case 13:
			break;
                case 14:
                case 15:
                case G_MAXINT:
                        _add_query_to_result(result, 13, keys);
			break;
		default:
			return;
                }
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_offset") == 0) {
                switch (offset) {
                case 0:
                        result = g_ptr_array_sized_new(14);
                        _add_query_to_result(result, 0, keys);
                        _add_query_to_result(result, 1, keys);
                        _add_query_to_result(result, 2, keys);
                        _add_query_to_result(result, 3, keys);
                        _add_query_to_result(result, 4, keys);
                        _add_query_to_result(result, 5, keys);
                        _add_query_to_result(result, 6, keys);
                        _add_query_to_result(result, 7, keys);
                        _add_query_to_result(result, 8, keys);
                        _add_query_to_result(result, 9, keys);
                        _add_query_to_result(result, 10, keys);
                        _add_query_to_result(result, 11, keys);
                        _add_query_to_result(result, 12, keys);
                        _add_query_to_result(result, 13, keys);
                        break;
                case 13:
                        result = g_ptr_array_sized_new(1);
                        _add_query_to_result(result, 13, keys);
                        break;
                case 14:
                case 15:
                        result = g_ptr_array_new();
                        break;
		default:
			return;
                }
        } else if ((g_ascii_strcasecmp(RUNNING_CASE, "test_browse_recursive_artist1") == 0) ||
		   (g_ascii_strcasecmp(RUNNING_CASE, "test_destroy_container") == 0)) {
                result = g_ptr_array_sized_new(5);
                _add_query_to_result(result, 1, keys);
                _add_query_to_result(result, 3, keys);
                _add_query_to_result(result, 4, keys);
                _add_query_to_result(result, 5, keys);
                _add_query_to_result(result, 6, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_filter_simple") == 0) {
                result = g_ptr_array_sized_new(4);
                _add_query_to_result(result, 3, keys);
                _add_query_to_result(result, 4, keys);
                _add_query_to_result(result, 5, keys);
                _add_query_to_result(result, 6, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_filter_and") == 0) {
                result = g_ptr_array_sized_new(1);
                _add_query_to_result(result, 3, keys);
        } else if (g_ascii_strcasecmp(RUNNING_CASE, "test_browse_videos") == 0) {
                result = g_ptr_array_sized_new(2);
                _add_query_to_result(result, 16, keys);
		_add_query_to_result(result, 17, keys);
        } else {
                return;
        }

        callback(result, NULL, user_data);
}

void
tracker_metadata_get_unique_values_with_concat_count_and_sum_async(TrackerClient *client,
                                                                   ServiceType service,
                                                                   char **meta_types,
                                                                   const char *query,
                                                                   char *concat,
                                                                   char *count,
                                                                   char *sum,
                                                                   gboolean descending,
                                                                   int offset,
                                                                   int max_hits,
                                                                   TrackerGPtrArrayReply callback,
                                                                   gpointer user_data)
{
        if (_check_query_case(query) &&
            _check_concat_case(concat) &&
            _check_count_case(count) &&
            _check_sum_case(sum, service)) {
                _send_concat_count_and_sum_expected_result(callback, meta_types, service, user_data);
        }
}

void
tracker_search_query_async (TrackerClient *client,
                            int live_query_id,
                            ServiceType service,
                            char **fields,
                            const char *search_text,
                            const char *keywords,
                            const char *query,
                            int offset, int max_hits,
                            gboolean sort_by_service,
                            char **sort_fields,
                            gboolean sort_descending,
                            TrackerGPtrArrayReply callback,
                            gpointer user_data)
{
        if (_check_query_case(query)) {
                _send_query_expected_result(callback, offset, max_hits, fields, user_data);
        }
}

void
tracker_metadata_get_async(TrackerClient *client,
                           ServiceType service,
                           const char *id,
                           char **keys,
                           TrackerArrayReply callback,
                           gpointer user_data)
{        if ((g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_clip") == 0 ||
	      g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_artist_album_clip") == 0 ||
	      g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_genre_artist_album_clip") == 0 ||
	      g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_album_clip") == 0) &&
	     g_ascii_strcasecmp(id, "/home/user/MyDocs/clip1.mp3") == 0) {
                _send_metadata(1, keys, callback, user_data);
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_video") == 0) {
		_send_metadata(16, keys, callback, user_data);
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_invalid") == 0) {
                _send_metadata(-1, keys, callback, user_data);
	} else if (g_ascii_strcasecmp(RUNNING_CASE, "test_get_metadata_playlist") == 0 &&
		   g_ascii_strcasecmp(id, "/tmp/playlist1.pls") == 0) {
		_send_metadata(14, keys, callback, user_data);
        }
}

void
tracker_metadata_set(TrackerClient *client,
		     ServiceType service,
		     const char *id,
		     char **keys,
		     char **values,
		     GError **error)
{
	if (_check_params(service, id, keys, values)) {
		if ((g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_invalid_mixed") == 0 ) ||
		    (g_ascii_strcasecmp(RUNNING_CASE, "test_set_metadata_invalid_non_existing_clip") == 0)) {
			*error = g_error_new(1, 1, "Error during tracker_metadata_set execution");
		}
	}
}

TrackerClient *
tracker_connect(gboolean enable_warnings)
{
        return (gchar *) "DON'T USE";
}

void
tracker_disconnect(TrackerClient *client)
{
}


/* ---------------------------------------------------- */
/*                      GIO MOCKUP                      */
/* ---------------------------------------------------- */

gboolean
g_file_delete(GFile *file, GCancellable *cancellable, GError **error)
{
	if (g_ascii_strcasecmp(RUNNING_CASE, "test_destroy_failed") == 0) {
		return FALSE;
	} else {
		g_destroy_results = g_list_append(g_destroy_results,
						  g_file_get_path(file));
		return TRUE;
	}
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
