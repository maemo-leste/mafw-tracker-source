#
# configure.ac for MAFW tracker source library
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

AC_PREREQ([2.53])
AC_INIT([mafw-tracker-source], [0.2.2010.06-1])

AC_CONFIG_SRCDIR([libmafw-tracker-source/mafw-tracker-source.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([foreign])
AM_MAINTAINER_MODE

AC_DISABLE_STATIC

dnl Prevent AC_PROG_CC adding '-g -O2' to CFLAGS.
SAVEDCFLAGS="$CFLAGS"
AC_PROG_CC
if test "x$GCC" = xyes; then
	CFLAGS="$SAVEDCFLAGS"
fi

AC_PROG_LIBTOOL
AC_PROG_INSTALL

# DISABLED_BY_DEFAULT(NAME, DESCRIPTION)
# ---------------------------------
# Creates a new --enable-* option, with default value `no'.
AC_DEFUN([DISABLED_BY_DEFAULT], [dnl
         AC_ARG_ENABLE([$1], AS_HELP_STRING([--enable-$1], [$2]), [],[dnl
         m4_bpatsubst([enable_$1], [[^0-9a-z]], [_])=no])dnl
])# DISABLED_BY_DEFAULT

# ENABLED_BY_DEFAULT(NAME, DESCRIPTION)
# ---------------------------------
# Creates a new --disable-* option, with default value `yes'.
AC_DEFUN([ENABLED_BY_DEFAULT], [dnl
         AC_ARG_ENABLE([$1], AS_HELP_STRING([--disable-$1], [$2]), [],[dnl
         m4_bpatsubst([enable_$1], [[^0-9a-z]], [_])=yes])dnl
])# ENABLED_BY_DEFAULT

dnl Prerequisites.

PKG_CHECK_MODULES(DEPS,
			gobject-2.0 >= 2.0
			mafw >= 0.1
			tracker-sparql-2.0
			tracker-control-2.0
			hildon-thumbnail >= 3.0
			totem-plparser
)

AC_SUBST(DEPS_CFLAGS)
AC_SUBST(DEPS_LIBS)

plugindir=`$PKG_CONFIG --variable=plugindir mafw`
AC_SUBST(plugindir)

dnl Check for glib-genmarshal.

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

dnl Configure-time options.

dnl Tests.
ENABLED_BY_DEFAULT([tests], [disable unit tests])
if test "x$enable_tests" = xyes; then
	PKG_CHECK_MODULES(CHECKMORE, [checkmore, check >= 0.9.4])
	if test -z "$CHECKMORE_LIBS"; then
		AC_MSG_WARN([checkmore is needed for unit tests!])
	fi
fi
AM_CONDITIONAL(ENABLE_TESTS,
               [test "x$enable_tests" = xyes && test -n "$CHECKMORE_LIBS"])
dnl Tracing.
DISABLED_BY_DEFAULT([tracing], [enable function instrumentation (tracing)])
if test "x$enable_tracing" = xyes; then
	_CFLAGS="$_CFLAGS -finstrument-functions -rdynamic"
fi

dnl Coverage.
DISABLED_BY_DEFAULT([coverage], [enable coverage data generation (gcov)])
if test "x$enable_coverage" = xyes; then
	AC_PATH_PROG(LCOV, [lcov], [lcov])
	if test "x$LCOV" = x; then
		echo You need to install lcov to get actual reports!
		echo See http://ltp.sf.net/coverage/lcov.php
	fi
	if test "x$SBOX_USE_CCACHE" == xyes; then
		AC_MSG_ERROR([Please set SBOX_USE_CCACHE=no to use coverage.])
	fi
	_CFLAGS="$_CFLAGS -fprofile-arcs -ftest-coverage"
	_LDFLAGS="$_LDFLAGS -g -lgcov"
fi
AM_CONDITIONAL(ENABLE_COVERAGE,
               [test "x$enable_coverage" != xno && test -n "$LCOV"])

dnl Output files.

AC_CONFIG_FILES([
  Makefile
  mafw-tracker-source.pc
  mafw-tracker-source-uninstalled.pc
  libmafw-tracker-source/Makefile
  tests/Makefile
  debian/mafw-tracker-source.install
])

AC_OUTPUT
