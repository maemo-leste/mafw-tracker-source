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

#include "mafw-tracker-source.h"
#include "tracker-iface.h"
#include <check.h>
#include <checkmore.h>
#include <gio/gio.h>
#include <glib.h>
#include <libmafw/mafw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UNKNOWN_ARTIST_VALUE "(Unknown artist)"
#define UNKNOWN_ALBUM_VALUE  "(Unknown album)"
#define UNKNOWN_GENRE_VALUE  "(Unknown genre)"

SRunner *
configure_tests(void);
static void
create_temporal_playlist (gchar *path, gint n_items);
static void
create_tracker_database();

/* ---------------------------------------------------- */
/*                      GLOBALS                         */
/* ---------------------------------------------------- */

static MafwSource *g_tracker_source = NULL;
static gboolean g_browse_called = FALSE;
static gboolean g_browse_error = FALSE;
static gboolean g_metadata_called = FALSE;
static gboolean g_metadata_error = FALSE;
static gboolean g_destroy_called = FALSE;
static gboolean g_metadatas_called = FALSE;
static gboolean g_metadatas_error = FALSE;
static gboolean g_destroy_error = FALSE;
static gboolean g_set_metadata_called = FALSE;
static gboolean g_set_metadata_error = FALSE;
static gboolean g_set_metadata_params_err = FALSE;
static GList *g_browse_results = NULL;
static int g_browse_results_len = 0;
static GList *g_metadata_results = NULL;
static GList *g_destroy_results = NULL;
static GList *g_set_metadata_failed_keys = NULL;
static gchar *RUNNING_CASE = NULL;

typedef struct
{
  guint browse_id;
  guint index;
  gchar *objectid;
  GHashTable *metadata;
} BrowseResult;

typedef struct
{
  gchar *objectid;
  GHashTable *metadata;
} MetadataResult;

SRunner *
configure_tests(void);

/* ---------------------------------------------------- */
/*                   HELPER FUNCTIONS                   */
/* ---------------------------------------------------- */

static void
remove_browse_item(gpointer item, gpointer data)
{
  BrowseResult *br = (BrowseResult *)item;

  if (br->metadata)
    g_hash_table_unref(br->metadata);

  br->metadata = NULL;

  g_free(br->objectid);
  g_free(br);
}

static void
remove_metadata_item(gpointer item, gpointer data)
{
  MetadataResult *mr = (MetadataResult *)item;

  if (mr->metadata)
  {
    mafw_metadata_release(mr->metadata);
  }

  mr->metadata = NULL;

  g_free(mr->objectid);
  g_free(mr);
}

static void
clear_browse_results(void)
{
  g_browse_called = FALSE;
  g_browse_error = FALSE;

  if (g_browse_results != NULL)
  {
    g_list_foreach(g_browse_results, (GFunc)remove_browse_item,
                   NULL);
    g_list_free(g_browse_results);
    g_browse_results = NULL;
  }

  RUNNING_CASE = "no_case";
}

static void
clear_metadata_results(void)
{
  g_metadata_called = FALSE;
  g_metadata_error = FALSE;

  if (g_metadata_results != NULL)
  {
    g_list_foreach(g_metadata_results, (GFunc)remove_metadata_item,
                   NULL);
    g_list_free(g_metadata_results);
    g_metadata_results = NULL;
  }

  RUNNING_CASE = "no_case";
}

static void
clear_metadatas_results(void)
{
  g_metadatas_called = FALSE;
  g_metadatas_error = FALSE;

  if (g_metadata_results != NULL)
  {
    g_list_foreach(g_metadata_results, (GFunc)remove_metadata_item,
                   NULL);
    g_list_free(g_metadata_results);
    g_metadata_results = NULL;
  }

  RUNNING_CASE = "no_case";
}

static void
clear_destroy_results(void)
{
  g_destroy_called = FALSE;
  g_destroy_error = FALSE;

  if (g_destroy_results != NULL)
  {
    g_list_foreach(g_destroy_results, (GFunc)g_free, NULL);
    g_list_free(g_destroy_results);
    g_destroy_results = NULL;
  }

  RUNNING_CASE = "no_case";
}

static void
clear_set_metadata_results(void)
{
  g_set_metadata_called = FALSE;
  g_set_metadata_params_err = FALSE;
  g_set_metadata_error = FALSE;

  if (g_set_metadata_failed_keys != NULL)
  {
    g_list_foreach(g_set_metadata_failed_keys, (GFunc)g_free, NULL);
    g_list_free(g_set_metadata_failed_keys);
    g_set_metadata_failed_keys = NULL;
  }

  g_set_metadata_failed_keys = FALSE;

  RUNNING_CASE = "no_case";
}

/* ---------------------------------------------------- */
/*                      FIXTURES                        */
/* ---------------------------------------------------- */

static void
fx_setup_dummy_tracker_source(void)
{
  GError *error = NULL;

  g_type_init();

  /* Check if we have registered the plugin, otherwise
     do it and get a pointer to the Tracker source instance */
  MafwRegistry *registry = MAFW_REGISTRY(mafw_registry_get_instance());
  GList *sources = mafw_registry_get_sources(registry);

  if (sources == NULL)
  {
    mafw_tracker_source_plugin_initialize(registry, &error);

    if (error != NULL)
    {
      g_error("Plugin initialization failed!");
    }

    sources = mafw_registry_get_sources(registry);
  }

  if (sources == NULL)
  {
    g_error("Plugin intialization failed!");
  }

  g_tracker_source = MAFW_SOURCE(g_object_ref(G_OBJECT(sources->data)));

  ck_assert_msg(MAFW_IS_TRACKER_SOURCE(g_tracker_source),
                "Could not create tracker source instance");
}

static void
fx_teardown_dummy_tracker_source(void)
{
  g_object_unref(g_tracker_source);
}

/* ---------------------------------------------------- */
/*                     TEST CASES                       */
/* ---------------------------------------------------- */

static void
browse_result_cb(MafwSource *source, guint browse_id, gint remaining,
                 guint index, const gchar *objectid, GHashTable *metadata,
                 gpointer user_data, const GError *error)
{
  BrowseResult *result = NULL;
  const gchar *mime = NULL;
  GValue *value = NULL;

  g_browse_called = TRUE;

  if (error)
  {
    g_print("%s", error->message);
    g_main_loop_quit(user_data);
    g_browse_error = TRUE;
    return;
  }

  if (!remaining)
    g_main_loop_quit(user_data);

  /* We assume that 'mime' is the first metadata! */
  mime = metadata && (value = mafw_metadata_first(metadata,
                                                  MAFW_METADATA_KEY_MIME))
                ? g_value_get_string(value) : "?";

  g_print(" Received: (%c) %d  %s\n", mime[0], index, objectid);

  /* We get a NULL objectid if there are no items matching our query */
  if (objectid != NULL)
  {
    result = (BrowseResult *)g_malloc(sizeof(BrowseResult));
    result->browse_id = browse_id;
    result->index = index;
    result->objectid = g_strdup(objectid);
    result->metadata = metadata;

    if (metadata)
      g_hash_table_ref(metadata);

    g_browse_results = g_list_append(g_browse_results, result);

    if (g_browse_results_len &&
        (g_list_length(g_browse_results) == g_browse_results_len))
    {
      g_main_loop_quit(user_data);
    }
  }
}

/* Browse localtagfs:: */
START_TEST(test_browse_root)
{
  const gchar *const *metadata = NULL;
  GMainLoop *loop = NULL;

  RUNNING_CASE = "test_browse_root";
  loop = g_main_loop_new(NULL, FALSE);

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
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive two subcategories (music, video) */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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

  RUNNING_CASE = "test_browse_music";
  loop = g_main_loop_new(NULL, FALSE);

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
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received for");

  /* We should receive five subcategories (artists, albums,
   * songs, genres, playlists)*/
  ck_assert_msg(g_list_length(g_browse_results) == 5,
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

  RUNNING_CASE = "test_browse_music_artists";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse artists category...\n");
  /* Browse artists category */
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/artists",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 6 artists */
  ck_assert_msg(g_list_length(g_browse_results) == 6,
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

  RUNNING_CASE = "test_browse_music_artists_artist1";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Artist 1' artist...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/artists/Artist%201",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 2 albums */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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

  RUNNING_CASE = "test_browse_music_artists_unknown";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Unknown' artist...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/artists/",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 2 albums */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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

  RUNNING_CASE = "test_browse_music_artists_unknown_unknown";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Browse album 'Unknown'...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/artists//",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 2 clips */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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

  RUNNING_CASE = "test_browse_music_artists_artist1_album3";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Browse album 'Album 3'...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/artists/Artist%201/Album%203",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 4 clips */
  ck_assert_msg(g_list_length(g_browse_results) == 4,
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

  RUNNING_CASE = "test_browse_music_albums";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse albums category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/albums",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 7 albums */
  ck_assert_msg(g_list_length(g_browse_results) == 7,
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

  RUNNING_CASE = "test_browse_music_albums_album4";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Album 4' category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/albums/Album%204",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 3 clips */
  ck_assert_msg(g_list_length(g_browse_results) == 3,
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

  RUNNING_CASE = "test_browse_music_genres";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse genres category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/genres",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 4 genres */
  ck_assert_msg(g_list_length(g_browse_results) == 4,
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

  RUNNING_CASE = "test_browse_music_genres_genre2";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Genre 2' category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 4 artists */
  ck_assert_msg(g_list_length(g_browse_results) == 4,
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

  RUNNING_CASE = "test_browse_music_genres_unknown";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Unknown' category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/genres/",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 3 artists */
  ck_assert_msg(g_list_length(g_browse_results) == 3,
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

  RUNNING_CASE = "test_browse_music_genres_genre2_artist2";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Artist 2' category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202/Artist%202",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 1 album */
  ck_assert_msg(g_list_length(g_browse_results) == 1,
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

  RUNNING_CASE = "test_browse_music_genres_genre2_artist2_album2";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse 'Album 2' category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/genres/Genre%202/Artist%202/Album%202",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 1 clip */
  ck_assert_msg(g_list_length(g_browse_results) == 1,
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

  RUNNING_CASE = "test_browse_music_songs";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse songs category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 14 songs */
  ck_assert_msg(g_list_length(g_browse_results) == 14,
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

  RUNNING_CASE = "test_browse_music_playlists";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse playlists category...\n");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/playlists",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 2 playlists */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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

  RUNNING_CASE = "test_browse_music_playlists_playlist1";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_TITLE);

  g_print("> Browse playlistrecently-added playlist category...\n");
  create_temporal_playlist("/tmp/playlist1.pls", 4);
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/playlists/%2Ftmp%2Fplaylist1.pls",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  unlink("/tmp/playlist1.pls");

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive 4 entries */
  ck_assert_msg(g_list_length(
                  g_browse_results) == 4,
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
  gint i;

  gint count_cases[] = { 13, 14, 15 };
  gint expected_count[] = { 13, 14, 14 };

  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  /* Test count greater than, equal than and lesser than the
   * number of clips */

  for (i = 0; i < 3; i++)
  {
    RUNNING_CASE = "test_browse_count";
    mafw_source_browse(g_tracker_source,
                       MAFW_TRACKER_SOURCE_UUID "::music/songs",
                       FALSE,
                       NULL,
                       NULL,
                       metadata,
                       0,
                       count_cases[i],
                       browse_result_cb,
                       loop);

    g_main_loop_run(loop);

    ck_assert_msg(g_browse_called != FALSE,
                  "No browse_result signal received");

    ck_assert_msg(g_list_length(g_browse_results) == expected_count[i],
                  "Browse of artists category returned %d items instead of %d",
                  g_list_length(g_browse_results), expected_count[i]);

    clear_browse_results();
  }

  /* Special case: we want all elements */
  RUNNING_CASE = "test_browse_count";
  mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, NULL, NULL, metadata, 0, MAFW_SOURCE_BROWSE_ALL,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 14,
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
  gint i;

  gint offset_cases[] = { 0, 13, 14, 15 };
  gint expected_count[] = { 14, 1, 0, 0 };

  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  /* Test different cases of offset */
  for (i = 0; i < 4; i++)
  {
    RUNNING_CASE = "test_browse_offset";
    mafw_source_browse(g_tracker_source,
                       MAFW_TRACKER_SOURCE_UUID "::music/songs",
                       FALSE,
                       NULL,
                       NULL,
                       metadata,
                       offset_cases[i],
                       50,
                       browse_result_cb,
                       loop);

    g_main_loop_run(loop);

    ck_assert_msg(g_browse_called != FALSE,
                  "No browse_result signal received");

    ck_assert_msg(g_list_length(g_browse_results) == expected_count[i],
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

  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_ARTIST,
    MAFW_METADATA_KEY_ALBUM,
    MAFW_METADATA_KEY_TITLE);

  /* Test ill-formed objectid */
  RUNNING_CASE = "test_browse_invalid";
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/ssongs",
                     FALSE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_error != FALSE,
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

  RUNNING_CASE = "test_browse_videos";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME);

  g_print("> Browse songs category...\n");
  mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::videos",
                     FALSE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  /* We should receive  videos */
  ck_assert_msg(g_list_length(g_browse_results) == 2,
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
  browse_id = mafw_source_browse(g_tracker_source,
                                 MAFW_TRACKER_SOURCE_UUID "::music/songs",
                                 FALSE,
                                 NULL,
                                 NULL,
                                 metadata,
                                 0,
                                 50,
                                 browse_result_cb,
                                 loop);

  /* Wait to receive the first 4 elements */
  g_browse_results_len = 4;
  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Browse cancle returned %d items instead of '4'",
                g_list_length(g_browse_results));

  /* Now cancel the browse */
  g_browse_results_len = 0;
  ck_assert_msg(mafw_source_cancel_browse(g_tracker_source, browse_id, NULL),
                "Canceling a browse doesn't work");

  /* No elements should be received any more */
  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Canceled browse of music category returned %d items more",
                g_list_length(g_browse_results));

  /* Cancelling again should return an error */
  ck_assert_msg(mafw_source_cancel_browse(g_tracker_source, browse_id,
                                          NULL) == 0,
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

  loop = g_main_loop_new(NULL, FALSE);

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
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 14,
                "Recursive browse returned %d items instead of %d",
                g_list_length(g_browse_results), 14);

  clear_browse_results();

  RUNNING_CASE = "test_browse_recursive_artist1";
  mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID
                     "::music/artists/Artist%201",
                     TRUE, NULL, NULL, metadata, 0, 50,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 5,
                "Recursive browse returned %d items instead of %d",
                g_list_length(g_browse_results), 5);

  clear_browse_results();

  /* Special case: a clip is not browsable */
  RUNNING_CASE = "test_browse_recursive_clip";
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID
                     "::music/albums/Album%201/%2Fhome%2Fuser%2FMyDocs%2FSomeSong.mp3",
                     TRUE,
                     NULL,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_error != FALSE,
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
  MafwFilter *filter = NULL;

  loop = g_main_loop_new(NULL, FALSE);

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
                     browse_result_cb, loop);
  mafw_filter_free(filter);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Recursive browsing returned %d instead of 4",
                g_list_length(g_browse_results));

  clear_browse_results();

  /* Test a NOT filter */
  RUNNING_CASE = "test_browse_filter_not";
  filter = mafw_filter_parse("(!(" MAFW_METADATA_KEY_ALBUM "=Album 3))");
  mafw_source_browse(g_tracker_source, MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     TRUE, filter,
                     NULL, metadata, 0, 50,
                     browse_result_cb, loop);
  mafw_filter_free(filter);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 10,
                "Recursive browsing returned %d instead of 10",
                g_list_length(g_browse_results));

  clear_browse_results();

  /* Test an AND filter */
  RUNNING_CASE = "test_browse_filter_and";
  filter = mafw_filter_parse("(&(" MAFW_METADATA_KEY_ALBUM "=Album 3)("
                             MAFW_METADATA_KEY_TITLE "=Title 3))");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/albums",
                     TRUE,
                     filter,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);
  mafw_filter_free(filter);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 1,
                "Recursive browsing returned %d instead of 1",
                g_list_length(g_browse_results));

  clear_browse_results();

  /* Test an OR filter */
  RUNNING_CASE = "test_browse_filter_or";
  filter = mafw_filter_parse("(|(" MAFW_METADATA_KEY_ALBUM "=Album 2)("
                             MAFW_METADATA_KEY_ALBUM "=Album 3))");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/albums",
                     TRUE,
                     filter,
                     NULL,
                     metadata,
                     0,
                     50,
                     browse_result_cb,
                     loop);
  mafw_filter_free(filter);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 5,
                "Recursive browsing returned %d instead of 5",
                g_list_length(g_browse_results));

  clear_browse_results();

  g_main_loop_unref(loop);
}

END_TEST

START_TEST(test_browse_sort)
{
  const gchar *const *metadata = NULL;
  GMainLoop *loop = NULL;
  MafwFilter *filter = NULL;
  gchar *sort_criteria = NULL;
  gchar *first_item_1 = NULL;
  gchar *last_item_1 = NULL;
  gchar *first_item_2 = NULL;
  gchar *last_item_2 = NULL;
  GHashTable *metadata_values = NULL;

  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_GENRE,
    MAFW_METADATA_KEY_ALBUM);

  /* browse Album 3 results sorting by title */
  clear_browse_results();
  sort_criteria = g_strdup("+" MAFW_METADATA_KEY_TITLE);
  filter = mafw_filter_parse("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, filter, sort_criteria, metadata,
                     0, MAFW_SOURCE_BROWSE_ALL,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Browsing of music/songs category filtering by \"%s\" "
                "category returned %d items instead of %d",
                mafw_filter_to_string(filter),
                g_list_length(g_browse_results), 4);

  mafw_filter_free(filter);
  g_free(sort_criteria);

  /* Get the first and last items */
  metadata_values = ((BrowseResult *)((g_list_nth(g_browse_results, 0))
                                      ->data))->metadata;
  first_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
                                               metadata_values,
                                               MAFW_METADATA_KEY_TITLE)));
  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 3))->data))->metadata;
  last_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
                                              metadata_values,
                                              MAFW_METADATA_KEY_TITLE)));

  /* Now, do reverse sorting and compare results */
  clear_browse_results();
  sort_criteria = g_strdup("-" MAFW_METADATA_KEY_TITLE);
  filter = mafw_filter_parse("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, filter, sort_criteria, metadata,
                     0, MAFW_SOURCE_BROWSE_ALL,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Browsing of music/songs category filtering by \"%s\" "
                "category returned %d items instead of %d",
                mafw_filter_to_string(filter),
                g_list_length(g_browse_results), 4);

  mafw_filter_free(filter);
  g_free(sort_criteria);

  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 0))->data))->metadata;
  first_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
                                               metadata_values,
                                               MAFW_METADATA_KEY_TITLE)));
  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 3))->data))->metadata;
  last_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
                                              metadata_values,
                                              MAFW_METADATA_KEY_TITLE)));

  /* First item should be the same as previously retrieved last item */
  ck_assert_msg(strcmp(last_item_1, first_item_2) == 0,
                "Sorting behaviour is not working");
  ck_assert_msg(strcmp(last_item_2, first_item_1) == 0,
                "Sorting behaviour is not working");

  g_free(first_item_1);
  g_free(last_item_1);
  g_free(first_item_2);
  g_free(last_item_2);

  /* Browse Album 3 sorting by genre and title  */
  clear_browse_results();
  sort_criteria = g_strdup("+" MAFW_METADATA_KEY_GENRE ",+"
                           MAFW_METADATA_KEY_TITLE);
  filter = mafw_filter_parse("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, filter, sort_criteria, metadata,
                     0, MAFW_SOURCE_BROWSE_ALL,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE,
                "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Browsing of music/songs category filtering by \"%s\" "
                "category returned %d items instead of %d",
                mafw_filter_to_string(filter),
                g_list_length(g_browse_results),
                4);

  mafw_filter_free(filter);
  g_free(sort_criteria);

  /* Get the first and last items */
  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 0))->data))->metadata;
  first_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
                                               metadata_values,
                                               MAFW_METADATA_KEY_TITLE)));
  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 3))->data))->metadata;
  last_item_1 = g_strdup(g_value_get_string(mafw_metadata_first(
                                              metadata_values,
                                              MAFW_METADATA_KEY_TITLE)));

  /* Now, do reverse sorting and compare results */
  clear_browse_results();
  sort_criteria = g_strdup("-" MAFW_METADATA_KEY_GENRE ",-"
                           MAFW_METADATA_KEY_TITLE);
  filter = mafw_filter_parse("(" MAFW_METADATA_KEY_ALBUM "=Album 3)");
  mafw_source_browse(g_tracker_source,
                     MAFW_TRACKER_SOURCE_UUID "::music/songs",
                     FALSE, filter, sort_criteria, metadata,
                     0, MAFW_SOURCE_BROWSE_ALL,
                     browse_result_cb, loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_browse_called != FALSE, "No browse_result signal received");

  ck_assert_msg(g_list_length(g_browse_results) == 4,
                "Browsing of music/songs category filtering by \"%s\" "
                "category returned %d items instead of %d",
                mafw_filter_to_string(filter),
                g_list_length(g_browse_results), 4);

  mafw_filter_free(filter);
  g_free(sort_criteria);

  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 0))->data))->metadata;
  first_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
                                               metadata_values,
                                               MAFW_METADATA_KEY_TITLE)));
  metadata_values = ((BrowseResult *)
                     ((g_list_nth(g_browse_results, 3))->data))->metadata;
  last_item_2 = g_strdup(g_value_get_string(mafw_metadata_first(
                                              metadata_values,
                                              MAFW_METADATA_KEY_TITLE)));

  /* First item should be the same as previously retrieved last item */
  ck_assert_msg(strcmp(last_item_1, first_item_2) == 0,
                "Sorting behaviour is not working when using more "
                "than one field");
  ck_assert_msg(strcmp(last_item_2, first_item_1) == 0,
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

static void
metadata_result_cb(MafwSource *source, const gchar *objectid,
                   GHashTable *metadata, gpointer user_data,
                   const GError *error)
{
  g_metadata_called = TRUE;

  if (error)
  {
    g_print("%s", error->message);
    g_metadata_error = TRUE;
    g_main_loop_quit(user_data);
    return;
  }

  if (metadata)
  {
    MetadataResult *result = NULL;

    result = (MetadataResult *)malloc(sizeof(MetadataResult));
    result->objectid = g_strdup(objectid);
    result->metadata = metadata;

    g_hash_table_ref(metadata);

    g_metadata_results = g_list_append(g_metadata_results, result);
  }

  g_main_loop_quit(user_data);
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;

  RUNNING_CASE = "test_get_metadata_clip";
  loop = g_main_loop_new(NULL, FALSE);

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
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "audio/x-mp3"),
                "Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
                clip_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ARTIST);
  artist = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(artist != NULL && !strcmp(artist, "Artist 1"),
                "Artist for '%s' is '%s' instead of expected 'Artist 1'",
                clip_id, artist);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ALBUM);
  album = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(album != NULL && !strcmp(album, "Album 1"),
                "Album for '%s' is '%s' instead of expected 'Album 1'",
                clip_id, album);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Title 1"),
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;

  RUNNING_CASE = "test_get_metadata_video";
  loop = g_main_loop_new(NULL, FALSE);

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
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime,
                                        "video/x-msvideo"),
                "Mime type for '%s' is '%s' instead of expected 'video/x-msvideo'",
                clip_id,
                mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Video 1"),
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;

  RUNNING_CASE = "test_get_metadata_artist_album_clip";
  loop = g_main_loop_new(NULL, FALSE);

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
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "audio/x-mp3"),
                "Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
                clip_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ARTIST);
  artist = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(artist != NULL && !strcmp(artist, "Artist 1"),
                "Artist for '%s' is '%s' instead of expected 'Artist 1'",
                clip_id, artist);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ALBUM);
  album = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(album != NULL && !strcmp(album, "Album 1"),
                "Album for '%s' is '%s' instead of expected 'Album 1'",
                clip_id, album);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Title 1"),
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;

  RUNNING_CASE = "test_get_metadata_genre_artist_album_clip";
  loop = g_main_loop_new(NULL, FALSE);

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
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "audio/x-mp3"),
                "Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
                clip_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ARTIST);
  artist = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(artist != NULL && !strcmp(artist, "Artist 1"),
                "Artist for '%s' is '%s' instead of expected 'Artist 1'",
                clip_id, artist);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ALBUM);
  album = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(album != NULL && !strcmp(album, "Album 1"),
                "Album for '%s' is '%s' instead of expected 'Album 1'",
                clip_id, album);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_GENRE);
  genre = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(genre != NULL && !strcmp(genre, "Genre 1"),
                "Genre for '%s' is '%s' instead of expected 'Genre 1'",
                clip_id, genre);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Title 1"),
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;
  gint duration, childcount;

  RUNNING_CASE = "test_get_metadata_playlist";
  loop = g_main_loop_new(NULL, FALSE);

  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_DURATION,
    MAFW_METADATA_KEY_CHILDCOUNT_1);

  object_id = MAFW_TRACKER_SOURCE_UUID "::music/playlists/"
    "%2Ftmp%2Fplaylist1.pls";

  create_temporal_playlist("/tmp/playlist1.pls", 4);
  mafw_source_get_metadata(g_tracker_source,
                           object_id, metadata_keys,
                           metadata_result_cb,
                           loop);

  g_main_loop_run(loop);
  unlink("/tmp/playlist1.pls");

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* Check results */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime,
                                        "x-mafw/container"),
                "Mime type for '%s' is '%s' instead of expected 'x-mafw/container'",
                clip_id,
                mime);
  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_DURATION);
  duration = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(duration == 76,
                "Duration for '%s' is '%i' instead of expected '76'",
                clip_id, duration);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_CHILDCOUNT_1);
  childcount = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(childcount == 4,
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
  GList *item_metadata = NULL;
  GValue *mval = NULL;

  RUNNING_CASE = "test_get_metadata_album_clip";
  loop = g_main_loop_new(NULL, FALSE);

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
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of 1 items returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  clip_id = g_strdup(((MetadataResult *)
                      (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", clip_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "audio/x-mp3"),
                "Mime type for '%s' is '%s' instead of expected 'audio/x-mp3'",
                clip_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ARTIST);
  artist = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(artist != NULL && !strcmp(artist, "Artist 1"),
                "Artist for '%s' is '%s' instead of expected 'Artist 1'",
                clip_id, artist);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_ALBUM);
  album = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(album != NULL && !strcmp(album, "Album 1"),
                "Album for '%s' is '%s' instead of expected 'Album 1'",
                clip_id, album);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Title 1"),
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

  RUNNING_CASE = "test_get_metadata_invalid";
  loop = g_main_loop_new(NULL, FALSE);

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
                           metadata_result_cb, loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_results == NULL,
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

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_CHILDCOUNT_1,
    MAFW_METADATA_KEY_DURATION);

  /* Object ids to query */
  object_id = MAFW_TRACKER_SOURCE_UUID "::music/albums";

  /* Execute query */
  mafw_source_get_metadata(g_tracker_source,
                           object_id, metadata_keys,
                           metadata_result_cb,
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of a category returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  category_id = g_strdup(((MetadataResult *)
                          (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", category_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Albums"),
                "Title for '%s' is '%s' instead of expected 'Albums'",
                category_id, title);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "x-mafw/container"),
                "Mime type for '%s' is '%s' instead of expected ''",
                category_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_DURATION);
  duration = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(duration == 612,
                "Duration for '%s' is '%i' instead of expected '612'",
                category_id, duration);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_CHILDCOUNT_1);
  childcount = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(childcount == 7,
                "Childcount for '%s' is '%i' instead of expected '7'",
                category_id, childcount);

  g_free(category_id);

  clear_metadata_results();

  g_main_loop_unref(loop);
}

END_TEST
START_TEST(test_get_metadata_music)
{
  GMainLoop *loop = NULL;

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

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_CHILDCOUNT_1,
    MAFW_METADATA_KEY_DURATION);

  /* Object ids to query */
  object_id = MAFW_TRACKER_SOURCE_UUID "::music";

  /* Execute query */
  mafw_source_get_metadata(g_tracker_source,
                           object_id, metadata_keys,
                           metadata_result_cb,
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of a category returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  category_id = g_strdup(((MetadataResult *)
                          (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", category_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Music"),
                "Title for '%s' is '%s' instead of expected 'Music'",
                category_id, title);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "x-mafw/container"),
                "Mime type for '%s' is '%s' instead of expected ''",
                category_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_DURATION);
  duration = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(duration == 612,
                "Duration for '%s' is '%i' instead of expected '612'",
                category_id, duration);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_CHILDCOUNT_1);
  childcount = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(childcount == 5,
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

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_CHILDCOUNT_1,
    MAFW_METADATA_KEY_DURATION);

  /* Object ids to query */
  object_id = MAFW_TRACKER_SOURCE_UUID "::videos";

  /* Execute query */
  mafw_source_get_metadata(g_tracker_source,
                           object_id, metadata_keys,
                           metadata_result_cb,
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of a category returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  category_id = g_strdup(((MetadataResult *)
                          (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", category_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Videos"),
                "Title for '%s' is '%s' instead of expected 'Videos'",
                category_id, title);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "x-mafw/container"),
                "Mime type for '%s' is '%s' instead of expected ''",
                category_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_DURATION);
  duration = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(duration == 53,
                "Duration for '%s' is '%i' instead of expected '53'",
                category_id, duration);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_CHILDCOUNT_1);
  childcount = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(childcount == 2,
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

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_CHILDCOUNT_1,
    MAFW_METADATA_KEY_DURATION);

  /* Object ids to query */
  object_id = MAFW_TRACKER_SOURCE_UUID "::";

  /* Execute query */
  mafw_source_get_metadata(g_tracker_source,
                           object_id, metadata_keys,
                           metadata_result_cb,
                           loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadata_called != FALSE,
                "No metadata_result signal received");

  ck_assert_msg(g_list_length(g_metadata_results) == 1,
                "Query metadata of a category returned %d results",
                g_list_length(g_metadata_results));

  item_metadata = g_metadata_results;

  /* ...for first clip */
  category_id = g_strdup(((MetadataResult *)
                          (item_metadata->data))->objectid);
  metadata = (((MetadataResult *)(item_metadata->data))->metadata);

  ck_assert_msg(metadata != NULL,
                "Did not receive metadata for item '%s'", category_id);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_TITLE);
  title = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(title != NULL && !strcmp(title, "Root"),
                "Title for '%s' is '%s' instead of expected 'Root'",
                category_id, title);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_MIME);
  mime = mval ? g_value_get_string(mval) : NULL;
  ck_assert_msg(mime != NULL && !strcmp(mime, "x-mafw/container"),
                "Mime type for '%s' is '%s' instead of expected ''",
                category_id, mime);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_DURATION);
  duration = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(duration == 665,
                "Duration for '%s' is '%i' instead of expected '665'",
                category_id, duration);

  mval = mafw_metadata_first(metadata,
                             MAFW_METADATA_KEY_CHILDCOUNT_1);
  childcount = mval ? g_value_get_int(mval) : 0;
  ck_assert_msg(childcount == 2,
                "Childcount for '%s' is '%i' instead of expected '2'",
                category_id, childcount);

  g_free(category_id);

  clear_metadata_results();

  g_main_loop_unref(loop);
}

END_TEST

static void
metadatas_result_cb(MafwSource *source,
                    GHashTable *metadatas,
                    gpointer user_data, const GError *error)
{
  MetadataResult *result = NULL;
  GList *object_ids;
  GList *current_obj;

  g_metadatas_called = TRUE;

  if (error)
  {
    g_metadatas_error = TRUE;
    g_main_loop_quit(user_data);
    return;
  }

  object_ids = g_hash_table_get_keys(metadatas);
  current_obj = object_ids;

  while (current_obj)
  {
    result = (MetadataResult *)malloc(sizeof(MetadataResult));
    result->objectid = g_strdup(current_obj->data);
    result->metadata = g_hash_table_lookup(metadatas, current_obj->data);

    if (result->metadata)
      g_hash_table_ref(result->metadata);

    g_metadata_results = g_list_append(g_metadata_results, result);
    current_obj = g_list_next(current_obj);
  }

  g_list_free(object_ids);
  g_main_loop_quit(user_data);
}

START_TEST(test_get_metadatas_none)
{
  GMainLoop *loop = NULL;
  const gchar *const *metadata_keys = NULL;
  gchar **object_ids = NULL;

  RUNNING_CASE = "test_get_metadatas_none";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_DURATION,
    MAFW_METADATA_KEY_CHILDCOUNT_1);

  object_ids = g_new0(gchar *, 1);

  /* Execute query */
  mafw_source_get_metadatas(g_tracker_source,
                            (const gchar **)object_ids, metadata_keys,
                            metadatas_result_cb,
                            loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadatas_called != FALSE,
                "No metadatas_result signal received");

  ck_assert_msg(g_metadatas_error != FALSE,
                "No error was obtained");

  ck_assert_msg(g_list_length(g_metadata_results) == 0,
                "Getting metadata from none returns some result");

  g_strfreev(object_ids);
  clear_metadatas_results();

  g_main_loop_unref(loop);
}
END_TEST
START_TEST(test_get_metadatas_several)
{
  GMainLoop *loop = NULL;
  const gchar *const *metadata_keys = NULL;
  gchar **object_ids = NULL;

  RUNNING_CASE = "test_get_metadatas_several";
  loop = g_main_loop_new(NULL, FALSE);

  /* Metadata we are interested in */
  metadata_keys = MAFW_SOURCE_LIST(
    MAFW_METADATA_KEY_TITLE,
    MAFW_METADATA_KEY_MIME,
    MAFW_METADATA_KEY_CHILDCOUNT_1,
    MAFW_METADATA_KEY_DURATION);

  object_ids = g_new(gchar *, 4);
  object_ids[0] = g_strdup(MAFW_TRACKER_SOURCE_UUID
                           "::music/songs/"
                           "%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3");
  object_ids[1] = g_strdup(MAFW_TRACKER_SOURCE_UUID
                           "::music/artists/Artist 1/Album 1/"
                           "%2Fhome%2Fuser%2FMyDocs%2Fclip1.mp3");
  object_ids[2] = g_strdup(MAFW_TRACKER_SOURCE_UUID
                           "::music/albums");
  object_ids[3] = NULL;

  /* Execute query */
  mafw_source_get_metadatas(g_tracker_source,
                            (const gchar **)object_ids, metadata_keys,
                            metadatas_result_cb,
                            loop);

  /* Check results... */
  g_main_loop_run(loop);

  ck_assert_msg(g_metadatas_called != FALSE,
                "No metadatas_result signal received");

  ck_assert_msg(g_metadatas_error != TRUE,
                "An error was obtained");

  ck_assert_msg(g_list_length(g_metadata_results) == 3,
                "Query metadatas of 3 elements returned %d results",
                g_list_length(g_metadata_results));

  g_strfreev(object_ids);
  clear_metadatas_results();

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

  if (error)
  {
    g_print("%s\n", error->message);
    g_set_metadata_error = TRUE;
  }

  if (failed_keys != NULL)
  {
    while (failed_keys[i] != NULL)
    {
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

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error != TRUE,
                "Error received during set metadata operation");

  ck_assert_msg(g_set_metadata_failed_keys == NULL,
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
                           "::videos/%2Fhome%2Fuser%2FMyDocs%2Fvideo1.avi",
                           metadata,
                           metadata_set_cb,
                           NULL);

  g_hash_table_unref(metadata);

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error != TRUE,
                "Error received during set metadata operation");

  ck_assert_msg(g_set_metadata_failed_keys == NULL,
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

  g_print("> Trying to modify a non-writable metadata...\n");
  mafw_source_set_metadata(g_tracker_source,
                           MAFW_TRACKER_SOURCE_UUID
                           "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip2.mp3",
                           metadata,
                           metadata_set_cb,
                           NULL);

  g_hash_table_unref(metadata);

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error == TRUE,
                "Error not received when trying to modify a non-writable "
                "metadata");

  ck_assert_msg(g_set_metadata_failed_keys != NULL,
                "Metadata failed keys should contain the keys that couldn't be "
                "updated");
  ck_assert_msg(g_list_length(g_set_metadata_failed_keys) == 1,
                "The number of failed keys reported is incorrect");

  clear_set_metadata_results();
#if 0
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
          "time...\n");
  mafw_source_set_metadata(g_tracker_source,
                           MAFW_TRACKER_SOURCE_UUID
                           "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip3.mp3",
                           metadata,
                           metadata_set_cb,
                           NULL);

  g_hash_table_unref(metadata);

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error == TRUE,
                "Error not received when trying to modify audio and video "
                "metadata at the same time");

  ck_assert_msg(g_set_metadata_failed_keys != NULL,
                "Metadata failed keys should contain the keys that couldn't be "
                "updated");

  ck_assert_msg(g_list_length(g_set_metadata_failed_keys) == 2,
                "The number of failed keys reported is incorrect");

  clear_set_metadata_results();
#endif
  /* 3. Trying to set metadata of a non-existing clip */

  RUNNING_CASE = "test_set_metadata_invalid_non_existing_clip";
  metadata = mafw_metadata_new();
  mafw_metadata_add_int(metadata,
                        MAFW_METADATA_KEY_PLAY_COUNT,
                        1);
  g_print("> Trying to modify metadata of a non-existing clip...\n");
  mafw_source_set_metadata(g_tracker_source,
                           MAFW_TRACKER_SOURCE_UUID
                           "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fnonexisting.mp3",
                           metadata,
                           metadata_set_cb,
                           NULL);

  g_hash_table_unref(metadata);

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error == TRUE,
                "Error not received when trying to modify metadata from a non-existing file");

  ck_assert_msg(g_set_metadata_failed_keys != NULL,
                "Metadata failed keys should contain the keys that couldn't be"
                "updated");

  ck_assert_msg(g_list_length(g_set_metadata_failed_keys) == 1,
                "The number of failed keys reported is incorrect");

  clear_set_metadata_results();

  /* 4. Trying to set metadata of an invalid objectid */

  RUNNING_CASE = "test_set_metadata_invalid_objectid";
  metadata = mafw_metadata_new();
  mafw_metadata_add_int(metadata,
                        MAFW_METADATA_KEY_PLAY_COUNT,
                        1);
  g_print("> Trying to modify metadata of an invalid objectid...\n");
  mafw_source_set_metadata(g_tracker_source,
                           MAFW_TRACKER_SOURCE_UUID
                           "::music/ssongs",
                           metadata,
                           metadata_set_cb,
                           NULL);

  g_hash_table_unref(metadata);

  while (g_main_context_pending(context))
    g_main_context_iteration(context, TRUE);

  ck_assert_msg(g_set_metadata_called != FALSE,
                "No set metadata signal received");

  ck_assert_msg(g_set_metadata_params_err != TRUE,
                "Error in the parameters from the tracker call");

  ck_assert_msg(g_set_metadata_error == TRUE,
                "Error not received when trying to modify metadata from a non-existing file");

  ck_assert_msg(g_set_metadata_failed_keys != NULL,
                "Metadata failed keys should contain the keys that couldn't be"
                "updated");

  ck_assert_msg(g_list_length(g_set_metadata_failed_keys) == 1,
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

  if (error)
  {
    g_destroy_error = TRUE;
  }

  g_main_loop_quit(user_data);
}

START_TEST(test_destroy_item)
{
  GMainLoop *loop;

  RUNNING_CASE = "test_destroy_item";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Destroy clip2 item...\n");
  /* Destroy the item */
  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID
                             "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fclip2.mp3",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error != TRUE,
                "Error received in destroy object callback");

  ck_assert_msg(g_destroy_results != NULL,
                "Destroy object operation failed");

  ck_assert_msg((g_list_length(g_destroy_results) == 1) &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 0),
                  "/home/user/MyDocs/clip2.mp3"),
                "Unexpected results in destroy object operation");

  clear_destroy_results();
  g_main_loop_unref(loop);
}
END_TEST
START_TEST(test_destroy_playlist)
{
  GMainLoop *loop;

  RUNNING_CASE = "test_destroy_playlist";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Destroy playlist item...\n");
  /* Destroy the playlist */
  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID
                             "::music/playlists/%2Fhome%2Fuser%2FMyDocs%2Fplaylist1.pls",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error != TRUE,
                "Error received in destroy object callback");

  ck_assert_msg(g_destroy_results != NULL,
                "Destroy object operation failed");

  ck_assert_msg((g_list_length(g_destroy_results) == 1) &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 0),
                  "/home/user/MyDocs/playlist1.pls"),
                "Unexpected results in destroy object operation");

  clear_destroy_results();
  g_main_loop_unref(loop);
}

END_TEST
START_TEST(test_destroy_container)
{
  GMainLoop *loop;

  RUNNING_CASE = "test_destroy_container";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Destroy Artist 1 container...\n");
  /* Destroy the item */
  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID
                             "::music/artists/Artist%201",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error != TRUE,
                "Error received in destroy object callback");

  ck_assert_msg((g_list_length(g_destroy_results) == 5) &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 0),
                  "/home/user/MyDocs/clip1.mp3") &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 1),
                  "/home/user/MyDocs/clip3.mp3") &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 2),
                  "/home/user/MyDocs/clip4.mp3") &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 3),
                  "/home/user/MyDocs/clip5.wma") &&
                !g_ascii_strcasecmp(
                  (gchar *)g_list_nth_data(g_destroy_results, 4),
                  "/home/user/MyDocs/clip6.wma"),
                "Unexpected results in destroy object operation");

  clear_destroy_results();
  g_main_loop_unref(loop);
}

END_TEST
START_TEST(test_destroy_invalid_category)
{
  GMainLoop *loop;

  loop = g_main_loop_new(NULL, FALSE);

  /* Destroy an invalid category */
  RUNNING_CASE = "test_destroy_invalid_category";
  g_print("> Destroy an invalid category...\n");

  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID "::music/songs",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error == TRUE,
                "Error not received when trying to destroy an invalid category");

  ck_assert_msg(g_destroy_results == NULL,
                "Objects destroyed when that is not expected");

  clear_destroy_results();

  /* Destroy a malformed objectid */
  RUNNING_CASE = "test_destroy_invalid";
  g_print("> Destroy a malformed objectid ...\n");

  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID "::music/songsss",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error == TRUE,
                "Error not received when trying to destroy a malformed objectid");

  ck_assert_msg(g_destroy_results == NULL,
                "Objects destroyed when that is not expected");

  clear_destroy_results();

  g_main_loop_unref(loop);
}

END_TEST
START_TEST(test_destroy_failed)
{
  GMainLoop *loop;

  RUNNING_CASE = "test_destroy_failed";
  loop = g_main_loop_new(NULL, FALSE);

  g_print("> Destroy a non-existent file...\n");
  /* Destroy the item */
  mafw_source_destroy_object(g_tracker_source,
                             MAFW_TRACKER_SOURCE_UUID
                             "::music/songs/%2Fhome%2Fuser%2FMyDocs%2Fnonexistent.mp3",
                             object_destroyed_cb,
                             loop);

  g_main_loop_run(loop);

  ck_assert_msg(g_destroy_called != FALSE,
                "No destroy signal received");

  ck_assert_msg(g_destroy_error == TRUE,
                "Error not received when trying to destroy a non-existent file");

  ck_assert_msg(g_destroy_results == NULL,
                "Objects destroyed when that is not expected");

  clear_destroy_results();
  g_main_loop_unref(loop);
}

END_TEST

/* ---------------------------------------------------- */
/*                  Suite creation                      */
/* ---------------------------------------------------- */
SRunner *
configure_tests(void)
{
  SRunner *sr = NULL;
  Suite *s = NULL;

  checkmore_wants_dbus();

  create_tracker_database();

  /* Create the suite */
  s = suite_create("MafwTrackerSource");

  /* Create test cases */
  TCase *tc_browse = tcase_create("Browse");
  TCase *tc_get_metadata = tcase_create("GetMetadata");
  TCase *tc_get_metadatas = tcase_create("GetMetadatas");
  TCase *tc_set_metadata = tcase_create("SetMetadata");
  TCase *tc_destroy = tcase_create("DestroyObject");

  /* Create unit tests for test case "Browse" */
  tcase_add_checked_fixture(tc_browse, fx_setup_dummy_tracker_source,
                            fx_teardown_dummy_tracker_source);
/* *INDENT-OFF* */
  if (1) tcase_add_test(tc_browse, test_browse_root);
  if (1) tcase_add_test(tc_browse, test_browse_music);
  if (1) tcase_add_test(tc_browse, test_browse_music_artists);
  if (1) tcase_add_test(tc_browse, test_browse_music_artists_artist1);
  if (1) tcase_add_test(tc_browse, test_browse_music_artists_unknown);
  if (1) tcase_add_test(tc_browse, test_browse_music_artists_unknown_unknown);
  if (1) tcase_add_test(tc_browse, test_browse_music_artists_artist1_album3);
  if (1) tcase_add_test(tc_browse, test_browse_music_albums);
  if (1) tcase_add_test(tc_browse, test_browse_music_albums_album4);
  if (1) tcase_add_test(tc_browse, test_browse_music_genres);
  if (1) tcase_add_test(tc_browse, test_browse_music_genres_genre2);
  if (1) tcase_add_test(tc_browse, test_browse_music_genres_unknown);
  if (1) tcase_add_test(tc_browse, test_browse_music_genres_genre2_artist2);
  if (1) tcase_add_test(tc_browse, test_browse_music_genres_genre2_artist2_album2);
  if (1) tcase_add_test(tc_browse, test_browse_music_songs);
  if (1) tcase_add_test(tc_browse, test_browse_music_playlists);
  if (1) tcase_add_test(tc_browse, test_browse_music_playlists_playlist1);
  if (1) tcase_add_test(tc_browse, test_browse_videos);
  if (1) tcase_add_test(tc_browse, test_browse_count);
  if (1) tcase_add_test(tc_browse, test_browse_offset);
  if (1) tcase_add_test(tc_browse, test_browse_invalid);
  if (1) tcase_add_test(tc_browse, test_browse_cancel);
  if (1) tcase_add_test(tc_browse, test_browse_recursive);
  if (1) tcase_add_test(tc_browse, test_browse_filter);
  if (1) tcase_add_test(tc_browse, test_browse_sort);
/* *INDENT-ON* */

  suite_add_tcase(s, tc_browse);

  /* Create unit tests for test case "GetMetadata" */
  tcase_add_checked_fixture(tc_get_metadata, fx_setup_dummy_tracker_source,
                            fx_teardown_dummy_tracker_source);

/* *INDENT-OFF* */
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_clip);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_video);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_playlist);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_artist_album_clip);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_genre_artist_album_clip);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_album_clip);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_invalid);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_albums);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_music);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_videos);
  if (1) tcase_add_test(tc_get_metadata, test_get_metadata_root);
/* *INDENT-ON* */

  suite_add_tcase(s, tc_get_metadata);

  /* Create unit tests for test case "GetMetadatas" */
  tcase_add_checked_fixture(tc_get_metadatas, fx_setup_dummy_tracker_source,
                            fx_teardown_dummy_tracker_source);

/* *INDENT-OFF* */
  if (1) tcase_add_test(tc_get_metadatas, test_get_metadatas_none);
  if (1) tcase_add_test(tc_get_metadatas, test_get_metadatas_several);
/* *INDENT-ON* */

  suite_add_tcase(s, tc_get_metadatas);

  /* Create unit tests for test case "SetMetadata" */
  tcase_add_checked_fixture(tc_set_metadata, fx_setup_dummy_tracker_source,
                            fx_teardown_dummy_tracker_source);

/* *INDENT-OFF* */
  if (1) tcase_add_test(tc_set_metadata, test_set_metadata_audio);
  if (1) tcase_add_test(tc_set_metadata, test_set_metadata_video);
  if (1) tcase_add_test(tc_set_metadata, test_set_metadata_invalid);
/* *INDENT-ON* */

  suite_add_tcase(s, tc_set_metadata);

  /* Create unit tests for test case "DestroyObject" */
  tcase_add_checked_fixture(tc_destroy, fx_setup_dummy_tracker_source,
                            fx_teardown_dummy_tracker_source);

/* *INDENT-OFF* */
  if (1) tcase_add_test(tc_destroy, test_destroy_item);
  if (1) tcase_add_test(tc_destroy, test_destroy_playlist);
  if (1) tcase_add_test(tc_destroy, test_destroy_container);
  if (1) tcase_add_test(tc_destroy, test_destroy_invalid_category);
  if (1) tcase_add_test(tc_destroy, test_destroy_failed);
/* *INDENT-ON* */

  suite_add_tcase(s, tc_destroy);

  /*Valgrind may require more time to run*/
  tcase_set_timeout(tc_browse, 60);
  tcase_set_timeout(tc_get_metadata, 60);
  tcase_set_timeout(tc_get_metadatas, 60);
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

gchar *DB[DB_SIZE][7] =
{
  /*00*/ { "/home/user/MyDocs/SomeSong.mp3", "Some Title", "Some Artist",
           "Some Album", "Some Genre", "audio/x-mp3", "36" },
  /*01*/ { "/home/user/MyDocs/clip1.mp3", "Title 1", "Artist 1", "Album 1",
           "Genre 1", "audio/x-mp3", "23" },
  /*02*/ { "/home/user/MyDocs/clip2.mp3", "Title 2", "Artist 2", "Album 2",
           "Genre 2", "audio/x-mp3", "17" },
  /*03*/ { "/home/user/MyDocs/clip3.mp3", "Title 3", "Artist 1", "Album 3",
           "Genre 2", "audio/x-mp3", "76" },
  /*04*/ { "/home/user/MyDocs/clip4.mp3", "Title 4", "Artist 1", "Album 3",
           "Genre 1", "audio/x-mp3", "42" },
  /*05*/ { "/home/user/MyDocs/clip5.wma", "Title 5", "Artist 1", "Album 3",
           "Genre 2", "audio/x-wma", "21" },
  /*06*/ { "/home/user/MyDocs/clip6.wma", "Title 6", "Artist 1", "Album 3",
           "Genre 1", "audio/x-wma", "90" },
  /*07*/ { "/home/user/MyDocs/clip7.mp3", "", "", "", "", "audio/x-mp3", "70" },
  /*08*/ { "/home/user/MyDocs/clip8.mp3", "Title V2", "Artist V2", "Album V2",
           "", "audio/x-mp3", "64" },
  /*09*/ { "/home/user/MyDocs/unknown-album.mp3", "Title 7", "Artist 4", "",
           "Genre 2", "audio/x-mp3", "8" },
  /*10*/ { "/home/user/MyDocs/unknown-artist.mp3", "Title 8", "", "Album 4",
           "Genre 2", "audio/x-mp3", "47" },
  /*11*/ { "/home/user/MyDocs/unknown-title.mp3", "", "Artist 4", "Album 4",
           "Genre 2", "audio/x-mp3", "57" },
  /*12*/ { "/home/user/MyDocs/unknown-genre.mp3", "Title 9", "Artist 4",
           "Album 4", "", "audio/x-mp3", "12" },
  /*13*/ { "/home/user/MyDocs/unknown.mp3", "", "", "", "", "audio/x-mp3",
           "49" },

  /*14*/ { "/home/user/MyDocs/playlist1.pls", "", "", "", "", "audio/x-scpls",
           "0" },
  /*15*/ { "/home/user/MyDocs/playlist2.m3u", "", "", "", "", "audio/x-mpegurl",
           "0" },
  /*16*/ { "/home/user/MyDocs/video1.avi", "Video 1", "", "", "",
           "video/x-msvideo", "30" },
  /*17*/ { "/home/user/MyDocs/video2.avi", "Video 2", "", "", "",
           "video/x-msvideo", "23" }
};

#define ESCAPE(s) s ? g_uri_escape_string(s, NULL, TRUE) : NULL;

static void
_add_playlist(TrackerSparqlConnection *connection,
              const gchar *file, const gchar *mime, gint nitems)
{
  gint i;
  GString *query = g_string_new(NULL);
  GString *entries = g_string_new(NULL);
  gchar *id = ESCAPE(file);
  gchar *sql;
  GError *error = NULL;
  gchar *entry;

  g_string_printf(query,
                  "INSERT DATA { "
                  "<%s> a nfo:FileDataObject, nmm:Playlist ; "
                  "nie:url 'file://%s' ; "
                  "nie:mimeType '%s' ; "
                  "nfo:entryCounter %d ; ",
                  id, file, mime, nitems);
  g_free(id);

  /* Add some local items */
  for (i = 0; i < (nitems - 1); i++)
  {
    gint p = i % DB_SIZE;
    const char *filename = DB[p][DB_FILENAME];
    entry = ESCAPE(filename);
    g_string_append_printf(query,
                           "nfo:hasMediaFileListEntry <%s%d> ; ",
                           entry, i);
    g_string_append_printf(entries,
                           "<%s%d> a nfo:MediaFileListEntry ; "
                           "nfo:entryUrl 'file://%s' ; "
                           "nfo:listPosition %d . ",
                           entry, i, filename, i);
    g_free(entry);
  }

  /* Add a non-local item */
  const char *remote = "http://www.mafwradio.com:8086";

  entry = ESCAPE(remote);
  g_string_append_printf(query,
                         "nfo:hasMediaFileListEntry <%s%d> ; ",
                         entry, nitems);
  g_string_append_printf(entries,
                         "<%s%d> a nfo:MediaFileListEntry ; "
                         "nfo:entryUrl '%s' ; "
                         "nfo:listPosition %d . ",
                         entry, nitems, remote, nitems);
  g_free(entry);
  g_string_append(query, " . ");

  sql = g_strconcat(query->str, " ",
                    entries->str, " }",
                    NULL);

  g_string_free(query, TRUE);
  g_string_free(entries, TRUE);

  tracker_sparql_connection_update(connection, sql, 0, NULL, &error);

  if (error)
  {
    g_error("SPARQL update failed, %s", error->message);
    g_error_free(error);
  }

  g_free(sql);
}

static void
_add_music_piece(TrackerSparqlConnection *connection, int idx)
{
  const char *file = DB[idx][DB_FILENAME];
  const char *title = DB[idx][DB_TITLE];
  const char *artist = DB[idx][DB_ARTIST];
  const char *album = DB[idx][DB_ALBUM];
  const char *genre = DB[idx][DB_GENRE];
  const char *mime = DB[idx][DB_MIME];
  int duration = atoi(DB[idx][DB_LENGTH]);
  gchar *id = ESCAPE(file);
  gchar *artist_escaped = ESCAPE(artist);
  gchar *album_escaped = ESCAPE(album);
  GString *query = g_string_new(NULL);
  GError *error = NULL;

  g_string_printf(query,
                  "INSERT DATA {"
                  "<%s> a nfo:FileDataObject, nmm:MusicPiece ; "
                  "nie:mimeType '%s' ; "
                  "nie:url 'file://%s' ; "
                  "nfo:duration %d ; ",
                  id, mime, file, duration);

  if (title)
  {
    g_string_append_printf(query,
                           "nie:title '%s' ; ",
                           title);
  }

  if (genre)
  {
    g_string_append_printf(query,
                           "nfo:genre '%s' ; ",
                           genre);
  }

  if (artist_escaped)
  {
    g_string_append_printf(query,
                           "nmm:performer <%s> ; ",
                           artist_escaped);
  }

  if (album_escaped)
  {
    g_string_append_printf(query,
                           "nmm:musicAlbum <%s> ; ",
                           album_escaped);
  }

  g_string_append(query, " . ");

  if (artist_escaped)
  {
    g_string_append_printf(query,
                           "<%s> a nmm:Artist ; "
                           "nmm:artistName '%s' . ",
                           artist_escaped, artist);
  }

  if (album_escaped)
  {
    g_string_append_printf(query,
                           "<%s> a nmm:MusicAlbum ; "
                           "nie:title '%s' ; ",
                           album_escaped, album);

    if (artist_escaped)
    {
      g_string_append_printf(query,
                             "nmm:albumArtist <%s> . ",
                             artist_escaped);
    }
    else
      g_string_append(query, " . ");
  }

  g_string_append(query, "}");

  tracker_sparql_connection_update(
    connection, query->str, 0, NULL, &error);

  if (error)
    g_error("SPARQL update failed, %s", error->message);

  g_string_free(query, TRUE);
  g_free(album_escaped);
  g_free(artist_escaped);
  g_free(id);
}

static void
_add_video(TrackerSparqlConnection *connection, int idx)
{
  const char *file = DB[idx][DB_FILENAME];
  const char *title = DB[idx][DB_TITLE];
  const char *mime = DB[idx][DB_MIME];
  int duration = atoi(DB[idx][DB_LENGTH]);
  gchar *id = ESCAPE(file);
  GString *query = g_string_new(NULL);
  GError *error = NULL;

  g_string_printf(query,
                  "INSERT DATA {"
                  "<%s> a nfo:FileDataObject, nmm:Video ; "
                  "nie:mimeType '%s' ; "
                  "nie:url 'file://%s' ; "
                  "nfo:duration %d ; ",
                  id, mime, file, duration);

  if (title)
  {
    g_string_append_printf(query,
                           "nie:title '%s' ; ",
                           title);
  }

  g_string_append(query, "}");

  tracker_sparql_connection_update(
    connection, query->str, 0, NULL, &error);

  if (error)
    g_error("SPARQL update failed, %s", error->message);

  g_string_free(query, TRUE);
  g_free(id);
}

static void
create_tracker_database()
{
  GError *error = NULL;
  TrackerSparqlConnection *connection =
    tracker_sparql_connection_get(NULL, &error);

  if (connection)
  {
    int i;
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:MusicPiece}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:MusicAlbum}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:Artist}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:Playlist}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:Video}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nmm:Image}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nie:DataObject}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nfo:MediaFileListEntry}",
                                     0,
                                     NULL,
                                     NULL);
    tracker_sparql_connection_update(connection,
                                     "DELETE WHERE {?id a nfo:MediaList}",
                                     0,
                                     NULL,
                                     NULL);

    for (i = 0; i < 14; i++)
      _add_music_piece(connection, i);

    _add_playlist(connection, "/tmp/playlist1.pls", "audio/x-scpls", 4);
    _add_playlist(connection, "/tmp/playlist2.m3u", "audio/x-mpegurl", 1);

    _add_video(connection, 16);
    _add_video(connection, 17);
  }
}

static void
create_temporal_playlist (gchar *path, gint nitems)
{
  FILE *pf;
  gint i;

  pf = fopen(path, "w");

  if (pf != NULL)
  {
    gchar *lines = g_strdup_printf("[playlist]\nNumberOfEntries=%d\n\n",
                                   nitems);
    fwrite(lines, strlen(lines), 1, pf);
    g_free(lines);

    /* Add some local items */
    for (i = 0; i < (nitems - 1); i++)
    {
      gint p = i % DB_SIZE;
      gchar *file = DB[p][DB_FILENAME];
      lines = g_strdup_printf("File%d=file://%s\n", i+1, file);
      fwrite(lines, strlen(lines), 1, pf);
      g_free(lines);
    }

    /* Add a non-local item */
    lines = g_strdup_printf("File%d=http://www.mafwradio.com:8086\n",
                            nitems);
    fwrite(lines, strlen(lines), 1, pf);
    g_free(lines);

    fclose(pf);
  }
}

TrackerSparqlConnection *
tracker_sparql_connection_get(GCancellable *cancellable, GError **error)
{
  /* FIXME - tracker 3 supports in-memory databases */
  // GFile *store = g_file_new_for_path("/home/user/.cache/tracker");
  GFile *store = g_file_new_for_path("db");
  TrackerSparqlConnection *connection;

  connection = tracker_sparql_connection_local_new(
    TRACKER_SPARQL_CONNECTION_FLAGS_NONE, store, NULL,
    NULL, NULL, error);

  g_object_unref(store);

  return connection;
}

/* ---------------------------------------------------- */
/*                      GIO MOCKUP                      */
/* ---------------------------------------------------- */

gboolean
g_file_delete(GFile *file, GCancellable *cancellable, GError **error)
{
  if (g_ascii_strcasecmp(RUNNING_CASE, "test_destroy_failed") == 0)
  {
    return FALSE;
  }
  else
  {
    g_destroy_results = g_list_append(g_destroy_results,
                                      g_file_get_path(file));
    return TRUE;
  }
}

/* vi: set noexpandtab ts=8 sw=8 cino=t0,(0: */
