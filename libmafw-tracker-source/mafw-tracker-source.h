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

#ifndef __MAFWSOURCETRACKER_H__
#define __MAFWSOURCETRACKER_H__

#include <glib.h>
#include <glib-object.h>

#include <libmafw/mafw.h>
#include <libmafw/mafw-source.h>

G_BEGIN_DECLS

#define MAFW_TYPE_TRACKER_SOURCE               \
        (mafw_tracker_source_get_type ())
#define MAFW_TRACKER_SOURCE(obj)                                      \
        (G_TYPE_CHECK_INSTANCE_CAST ((obj), MAFW_TYPE_TRACKER_SOURCE, \
                                     MafwTrackerSource))
#define MAFW_IS_TRACKER_SOURCE(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MAFW_TYPE_TRACKER_SOURCE))
#define MAFW_TRACKER_SOURCE_CLASS(klass)                            \
        (G_TYPE_CHECK_CLASS_CAST((klass), MAFW_TYPE_TRACKER_SOURCE, \
                                 MafwTrackerSourceClass))
#define MAFW_IS_TRACKER_SOURCE_CLASS(klass)                            \
        (G_TYPE_CHECK_CLASS_TYPE((klass), MAFW_TYPE_TRACKER_SOURCE))
#define MAFW_TRACKER_SOURCE_GET_CLASS(obj)                           \
        (G_TYPE_INSTANCE_GET_CLASS ((obj), MAFW_TYPE_TRACKER_SOURCE, \
                                    MafwTrackerSourceClass))

/* Tracker source plugin name for the plugin descriptor */
#define MAFW_TRACKER_SOURCE_PLUGIN_NAME "Mafw-Tracker-Source-Plugin"
/* Tracker source name */
#define MAFW_TRACKER_SOURCE_NAME "Mafw-Tracker-Source"
/* Tracker source UUID */
#define MAFW_TRACKER_SOURCE_UUID "localtagfs"

typedef struct _MafwTrackerSource MafwTrackerSource;
typedef struct _MafwTrackerSourceClass MafwTrackerSourceClass;

typedef struct _MafwTrackerSourcePrivate MafwTrackerSourcePrivate;

struct _MafwTrackerSource {
	MafwSource parent;

	/* private */
	MafwTrackerSourcePrivate *priv;
};

struct _MafwTrackerSourceClass {
	MafwSourceClass parent_class;
	/* Used internally to provide a new browse id for
	   each browse request */
	volatile gint browse_id_counter;
};

GType mafw_tracker_source_get_type(void);

gboolean mafw_tracker_source_plugin_initialize(MafwRegistry * registry,
					GError **error);
void mafw_tracker_source_plugin_deinitialize(GError **error);

MafwSource *mafw_tracker_source_new(void);
guint mafw_tracker_source_browse(MafwSource * self,
                                 const gchar * object_id,
                                 gboolean recursive,
                                 const MafwFilter* filter,
                                 const gchar * sort_criteria,
                                 const gchar * const * metadata_keys,
                                 guint skip_count,
                                 guint item_count,
                                 MafwSourceBrowseResultCb browse_cb,
                                 gpointer user_data);
gboolean mafw_tracker_source_cancel_browse(MafwSource * self,
                                           guint browse_id,
                                           GError **error);
gint mafw_tracker_source_get_update_progress(MafwSource * self,
                                             gint *processed_items,
                                             gint *remaining_items,
                                             gint *remaining_time);
void mafw_tracker_source_get_metadata(MafwSource * self,
                                      const gchar * object_id,
                                      const gchar * const * metadata_keys,
                                      MafwSourceMetadataResultCb metadata_cb,
                                      gpointer user_data);
void mafw_tracker_source_get_metadatas(MafwSource *self,
                                       const gchar **object_ids,
                                       const gchar *const *metadata_keys,
                                       MafwSourceMetadataResultsCb metadata_cb,
                                       gpointer user_data);
void mafw_tracker_source_set_metadata(MafwSource *self,
                                      const gchar *object_id,
                                      GHashTable *metadata,
                                      MafwSourceMetadataSetCb cb,
                                      gpointer user_data);
void mafw_tracker_source_destroy_object(MafwSource *self,
                                        const gchar *object_id,
                                        MafwSourceObjectDestroyedCb cb,
                                        gpointer user_data);

gchar* mafw_tracker_source_escape_string(const gchar* original);

void mafw_tracker_source_get_playlist_duration(
	MafwSource *self,
	const gchar *object_id,
	MafwSourceBrowseResultCb callback,
	gpointer user_data);

G_END_DECLS
#endif				/* __MAFWSOURCETRACKER_H__ */
