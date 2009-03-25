#!/bin/sh
# Run this to generate all the initial makefiles, etc.

autoreconf -v -f -i || exit 1
test -n "$NOCONFIGURE" || ./configure \
	--enable-debug --enable-maintainer-mode "$@"
