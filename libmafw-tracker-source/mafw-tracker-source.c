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
#include <gio/gio.h>
#include <gmodule.h>

#include "mafw-tracker-source.h"
#include "tracker-iface.h"
#include "util.h"
#include "definitions.h"

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "mafw-tracker-source"

#define MAFW_TRACKER_SOURCE_GET_PRIVATE(object) \
	(G_TYPE_INSTANCE_GET_PRIVATE((object), MAFW_TYPE_TRACKER_SOURCE, \
				      MafwTrackerSourcePrivate))


/* Flag to indicate if the plugin has been initialized */
static gboolean plugin_initialized = FALSE;

/*_________________________ Data types  ________________________*/


struct _destroy_object_closure {
        /* Source instance */
        MafwSource *source;
        /* The objectid to be removed */
        gchar *object_id;
        /* The user callback to notify what has happened */
        MafwSourceObjectDestroyedCb callback;
        /* User data for callback */
        gpointer user_data;
	/* Callback error */
	GError *error;
	/* List of uris to destroy */
	GList *uris;
	/* Some extra fields used to destroy the files */
	guint current_index;
	guint remaining_count;
	/* We may have to allocate memory for URI resolution */
	gchar **metadata_keys;
};


/*________________________ Plugin init  ________________________*/


/*
 * Registers the plugin descriptor making this plugin available to the
 * framework and applications
 */
G_MODULE_EXPORT MafwPluginDescriptor mafw_tracker_source_plugin_description =
{
	{ .name = MAFW_TRACKER_SOURCE_PLUGIN_NAME },
	.initialize = mafw_tracker_source_plugin_initialize,
	.deinitialize = mafw_tracker_source_plugin_deinitialize,
};

/*
 * Plugin initialization. Initializes connectivity with Tracker
 * and then creates a MafwTrackerSource instance and registers it.
 */
gboolean mafw_tracker_source_plugin_initialize(MafwRegistry * registry,
					       GError **error)
{
	MafwSource *source;

	/* First, check connectivity with Tracker */
	plugin_initialized = ti_init();
	if (plugin_initialized == FALSE) {
		/* Disable plugin if we cannot connect to Tracker */
		return FALSE;
	}

	/* Create a tracker source instance and register it */
	source = mafw_tracker_source_new();
	mafw_registry_add_extension(registry, MAFW_EXTENSION(source));

	return TRUE;
}

/*
 * Plugin deinit
 */
void mafw_tracker_source_plugin_deinitialize(GError **error)
{
	ti_deinit();
}

/*_______________________________ Utilities _____________________________*/

static void _destroy_object_closure_free(gpointer data)
{
	struct _destroy_object_closure *dc;

	dc = (struct _destroy_object_closure *) data;

	/* Free objectid of the destroyed item */
	g_free(dc->object_id);

	/* Free list of uris */
	if (dc->uris) {
		g_list_foreach(dc->uris, (GFunc) g_free, NULL);
		g_list_free(dc->uris);
	}

	/* Free metadata keys */
	g_strfreev(dc->metadata_keys);

	/* Free callback error */
	if (dc->error) {
		g_error_free(dc->error);
	}
	g_free(dc);
}

gchar* mafw_tracker_source_escape_string(const gchar* original)
{
	return g_uri_escape_string(original,
				  "abcdefghijklmnopqrstuvwxyz"
				  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
				  TRUE);
}

/* ___________________ GObject private implementation __________________ */

static gboolean _destroy_object_idle(gpointer data)
{

	GFile *file;
	gchar *uri;

	struct _destroy_object_closure *dc =
		(struct _destroy_object_closure *) data;

	if (dc->remaining_count == 0) {
		dc->callback(dc->source, dc->object_id, dc->user_data,
			     dc->error);
		return FALSE;
	} else {
		uri = get_data(g_list_nth(dc->uris, dc->current_index));
		file = g_file_new_for_uri(uri);
		if (!g_file_delete(file, NULL, NULL)) {
			if (!dc->error) {
				dc->error =g_error_new(
                                      MAFW_SOURCE_ERROR,
                                      MAFW_SOURCE_ERROR_DESTROY_OBJECT_FAILED,
                                      "One or more files can't be deleted");
			}
		}

		dc->current_index++;
		dc->remaining_count--;

		g_object_unref(file);
	}

	return TRUE;
}

static void _get_uri(gpointer metadata, gpointer uris_list)
{
	GValue *gval;
	const gchar *uri;
	GList **uris = (GList **) uris_list;

	gval =  mafw_metadata_first(metadata,
				    MAFW_METADATA_KEY_URI);

	if (gval) {
		uri = g_value_get_string(gval);
		if (uri) {
			*uris = g_list_append(*uris, (gchar *) uri);
		}
	}
}

static void _destroy_object_tracker_cb(MafwResult *clips, GError *error,
				       gpointer user_data)
{
	GList *uris = NULL;

	struct _destroy_object_closure *dc =
		(struct _destroy_object_closure *) user_data;

	if (error == NULL) {
		g_list_foreach(clips->metadata_values, _get_uri, &uris);
		dc->uris = uris;
		dc->current_index = 0;
		dc->remaining_count = g_list_length(dc->uris);

		g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
				(GSourceFunc) _destroy_object_idle, dc,
				(GDestroyNotify) _destroy_object_closure_free);
		g_list_foreach(clips->ids,
			       (GFunc) g_free, NULL);
		g_list_free(clips->ids);
		g_free(clips);
	} else {
		/* Emit error */
		GError *mafw_error;
		mafw_error = g_error_new(MAFW_SOURCE_ERROR,
					  MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
					  "%s not found", dc->object_id);
		dc->callback(dc->source, dc->object_id, dc->user_data,
			     mafw_error);
		_destroy_object_closure_free(dc);
		g_error_free(mafw_error);
	}
}


/*_________________________ Tracker Source GObject ________________________*/

G_DEFINE_TYPE(MafwTrackerSource, mafw_tracker_source, MAFW_TYPE_SOURCE);

/*
 * Class initialization
 */
static void mafw_tracker_source_class_init(MafwTrackerSourceClass * klass)
{
	MafwSourceClass *source_class = MAFW_SOURCE_CLASS(klass);

	source_class->browse = mafw_tracker_source_browse;
        source_class->cancel_browse = mafw_tracker_source_cancel_browse;
	source_class->get_metadata = mafw_tracker_source_get_metadata;
	source_class->get_metadatas = mafw_tracker_source_get_metadatas;
	source_class->destroy_object = mafw_tracker_source_destroy_object;
        source_class->set_metadata = mafw_tracker_source_set_metadata;

	klass->browse_id_counter = 0;

	g_type_class_add_private(klass, sizeof(MafwTrackerSourcePrivate));
}

/*
 * Instance initialization
 */
static void mafw_tracker_source_init(MafwTrackerSource * source_tracker)
{
	source_tracker->priv =
                MAFW_TRACKER_SOURCE_GET_PRIVATE(source_tracker);

        /* Initialize list of pending browse operations */
        source_tracker->priv->pending_browse_ops = NULL;
}

/**
 * mafw_tracker_source_new:
 *
 * Creates a new tracker source.
 *
 * Returns: a new tracker source.
 */
MafwSource *mafw_tracker_source_new(void)
{
	MafwSource *source = NULL;

	if (plugin_initialized == FALSE) {
		g_critical ("Plugin has not been initialized. "
			    "Cannot create a MafwTrackerSource instance.");
	} else {
		source = MAFW_SOURCE(
			g_object_new(MAFW_TYPE_TRACKER_SOURCE,
				     "plugin", MAFW_TRACKER_SOURCE_PLUGIN_NAME,
				     "uuid", MAFW_TRACKER_SOURCE_UUID,
				     "name", MAFW_TRACKER_SOURCE_NAME,
				     NULL));
		
		/* Connect to notifications about changes on the filesystem */
		ti_init_watch(G_OBJECT(source));
	}

	return source;
}

void mafw_tracker_source_destroy_object(MafwSource *self,
					 const gchar *object_id,
					 MafwSourceObjectDestroyedCb cb,
					 gpointer user_data)
{
	CategoryType category;
	gchar *genre, *artist, *album, *clip;
	GError *error = NULL;

        g_return_if_fail(MAFW_IS_TRACKER_SOURCE(self));
        g_return_if_fail(object_id != NULL);

	category = util_extract_category_info(object_id, &genre, &artist,
                                              &album, &clip);
	if (category == CATEGORY_ERROR) {
		g_set_error(&error,
			MAFW_SOURCE_ERROR,
			MAFW_SOURCE_ERROR_INVALID_OBJECT_ID,
			"Malformed object id: %s", object_id);
		/* Emit the error */
		cb(self, object_id, user_data, error);
		g_error_free(error);
		return;
	}

	/* Prepare destroy operation */
	struct _destroy_object_closure *dc =
		g_new0(struct _destroy_object_closure, 1);
 	dc->source = self;
	dc->object_id = g_strdup(object_id);
	dc->callback = cb;
	dc->user_data = user_data;
	dc->error = NULL;
	dc->uris = NULL;
	dc->current_index = 0;
	dc->remaining_count = 1;
	dc->metadata_keys = NULL;

	/* Destroy operation */
	if (clip) {
		/* Delete a video, a song or a playlist file */
		dc->uris = g_list_append(dc->uris, g_strdup(clip));
		g_idle_add_full(
			G_PRIORITY_DEFAULT_IDLE,
			_destroy_object_idle,
			dc,
			(GDestroyNotify) _destroy_object_closure_free);
	} else if (artist || album){
		/* Delete a an album or an artist container and its files */
		dc->metadata_keys =
			g_strdupv((gchar **)
				  MAFW_SOURCE_LIST(MAFW_METADATA_KEY_URI));

		ti_get_songs(genre, artist, album, dc->metadata_keys, NULL,
			     NULL, FALSE, 0, 0,	_destroy_object_tracker_cb, dc);
	} else {
		/* Delete other containers is not allowed */
		error = g_error_new(MAFW_SOURCE_ERROR,
				    MAFW_SOURCE_ERROR_DESTROY_OBJECT_FAILED,
				    "Operation not allowed for category: %s",
				    dc->object_id);
		/* Emit the error */
		cb(self, object_id, user_data, error);
		/* Frees */
		g_error_free(error);
		_destroy_object_closure_free(dc);
	}

	/* Frees */
	if (clip)
		g_free(clip);
	if (genre)
		g_free(genre);
	if (album)
		g_free(album);
	if (artist)
		g_free(artist);

	return;
}
