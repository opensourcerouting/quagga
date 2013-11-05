INCLUDES = @INCLUDES@ -I$(top_srcdir) -I$(top_srcdir)/lib -I$(top_builddir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\" -DMULTIPATH_NUM=@MULTIPATH_NUM@

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

BUILT_SOURCES = header_constants_wrap.c

EXTRA_DIST = \
	__init__.py \
	header_constants.i

# This makefile uses SWIG and libtool to build a python module that permits access
# to all of libzebra's constants for use by the testsuite.
#
# TODO: We shouldn't use pkg-config to get the python cflags. ax_python_devel looks
# promising but it's GPLv3+ and I think our configure.ac is GPLv2 and therefore can't
# use it. :/

check_LTLIBRARIES = _header_constants.la

_header_constants_la_SOURCES = header_constants_wrap.c
_header_constants_la_LDFLAGS = -module -rpath `pwd`
_header_constants_la_CFLAGS = `pkg-config python --cflags`

header_constants_wrap.c: \
		header_constants.i \
		$(top_builddir)/config.h \
		$(top_builddir)/lib/route_types.h \
		$(top_srcdir)/lib/zclient.h \
		$(top_srcdir)/lib/zebra.h
	@SWIG@ -python $(INCLUDES) header_constants.i


# Make sure the shared object is installed before running the tests
check: _header_constants.a

_header_constants.a: _header_constants.la
	libtool --mode=install cp _header_constants.la $(abs_builddir)/_header_constants.la
