INCLUDES = @INCLUDES@ -I.. -Itestutils -I$(top_srcdir) -I$(top_srcdir)/lib -I$(top_builddir)/lib
DEFS = @DEFS@ $(LOCAL_OPTS) -DSYSCONFDIR=\"$(sysconfdir)/\"
LIBZEBRA_LDADD=../lib/libzebra.la @LIBCAP@
BGPD_LDADD=../bgpd/libbgp.a -lm

AM_CFLAGS = $(PICFLAGS)
AM_LDFLAGS = $(PILDFLAGS)

SUBDIRS = testutils/quagga_constants

EXTRA_DIST = \
	config/unix.exp \
	lib/bgpd.exp \
	lib/libzebra.exp \
	global-conf.exp


noinst_HEADERS = \
	testutils/prng.h

check:
	python run_tests.py -v

.PHONY: check

BGPD_TESTS=
check_PROGRAMS=

# These are C test programs

BGPD_TESTS += testcases/bgpd/test_aspath
testcases_bgpd_test_aspath_SOURCES = testcases/bgpd/test_aspath.c
testcases_bgpd_test_aspath_LDADD = $(LIBZEBRA_LDADD) $(BGPD_LDADD)

BGPD_TESTS += testcases/bgpd/test_capability_parser
testcases_bgpd_test_capability_parser_SOURCES = \
		testcases/bgpd/test_capability_parser.c
testcases_bgpd_test_capability_parser_LDADD = $(LIBZEBRA_LDADD) $(BGPD_LDADD)

BGPD_TESTS += testcases/bgpd/test_ecommunities
testcases_bgpd_test_ecommunities_SOURCES = testcases/bgpd/test_ecommunities.c
testcases_bgpd_test_ecommunities_LDADD = $(LIBZEBRA_LDADD) $(BGPD_LDADD)

BGPD_TESTS += testcases/bgpd/test_mp_attr
testcases_bgpd_test_mp_attr_SOURCES = testcases/bgpd/test_mp_attr.c
testcases_bgpd_test_mp_attr_LDADD = $(LIBZEBRA_LDADD) $(BGPD_LDADD)

BGPD_TESTS += testcases/bgpd/test_mpath
testcases_bgpd_test_mpath_SOURCES = testcases/bgpd/test_mpath.c
testcases_bgpd_test_mpath_LDADD = $(LIBZEBRA_LDADD) $(BGPD_LDADD)

check_PROGRAMS += testcases/zebra/test_nexthop_iterator
testcases_zebra_test_nexthop_iterator_SOURCES = \
		testcases/zebra/test_nexthop_iterator.c \
		testutils/prng.c
testcases_zebra_test_nexthop_iterator_LDADD = $(LIBZEBRA_LDADD)

check_PROGRAMS += testcases/zebra/test_stream
testcases_zebra_test_stream_SOURCES = testcases/zebra/test_stream.c
testcases_zebra_test_stream_LDADD = $(LIBZEBRA_LDADD)

check_PROGRAMS += testcases/zebra/test_table
testcases_zebra_test_table_SOURCES = testcases/zebra/test_table.c
testcases_zebra_test_table_LDADD = $(LIBZEBRA_LDADD)

if BGPD
check_PROGRAMS += $(BGPD_TESTS)
endif
