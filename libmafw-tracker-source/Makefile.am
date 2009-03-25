#
# Makefile.am for MAFW tracker source library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

lib_LTLIBRARIES			= mafw-tracker-source.la

mafw_tracker_source_la_SOURCES	= mafw-tracker-source-browse.c \
				  mafw-tracker-source-metadata.c \
				  mafw-tracker-source.c \
				  tracker-iface.c \
				  album-art.c \
				  album-art.h \
				  key-mapping.c \
				  key-mapping.h \
				  tracker-cache.c \
				  tracker-cache.h \
				  mafw-tracker-source.h \
				  tracker-iface.h \
				  definitions.h \
				  util.h \
				  util.c

mafw_tracker_source_la_CFLAGS	=  -DPREFIX=\"$(prefix)\" $(_CFLAGS)
mafw_tracker_source_la_LDFLAGS	= -avoid-version -module $(_LDFLAGS)
mafw_tracker_source_la_LIBADD	= -ldl -lpthread @DEPS_LIBS@

mafw_tracker_source_la_CPPFLAGS= @DEPS_CFLAGS@
libdir				= $(plugindir)

MAINTAINERCLEANFILES		= Makefile.in *.loT
