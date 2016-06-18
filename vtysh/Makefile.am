## Process this file with Automake to create Makefile.in


if ENABLE_BGP_VNC
BGP_VNC_RFP_SRCDIR   = @top_srcdir@/@LIBRFP@
BGP_VNC_RFP_INCDIR   = -I$(BGP_VNC_RFP_SRCDIR)
BGP_VNC_RFP_SRC      = $(BGP_VNC_RFP_SRCDIR)/*.c
BGP_VNC_RFAPI_SRCDIR = @top_srcdir@/bgpd/rfapi
BGP_VNC_RFAPI_INCDIR = -I$(BGP_VNC_RFAPI_SRCDIR) -I$(top_srcdir)/bgpd
BGP_VNC_RFAPI_SRC    = $(BGP_VNC_RFAPI_SRCDIR)/*.c
else
BGP_VNC_RFP_INCDIR   =
BGP_VNC_RFP_SRCDIR   =
BGP_VNC_RFP_SRC      =
BGP_VNC_RFAPI_INCDIR =
BGP_VNC_RFAPI_SRCDIR =
BGP_VNC_RFAPI_SRC    =
endif
AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib -I$(top_builddir)/lib \
	$(BGP_VNC_RFAPI_INCDIR) $(BGP_VNC_RFP_INCDIR)
DEFS = @DEFS@ -DSYSCONFDIR=\"$(sysconfdir)/\"

LIBS = @LIBS@ @CURSES@ @LIBPAM@

AM_CFLAGS = $(WERROR)

bin_PROGRAMS = vtysh

vtysh_SOURCES = vtysh_main.c vtysh.c vtysh_user.c vtysh_config.c
nodist_vtysh_SOURCES = vtysh_cmd.c
CLEANFILES = vtysh_cmd.c
noinst_HEADERS = vtysh.h vtysh_user.h
vtysh_LDADD = ../lib/libzebra.la @LIBCAP@ @LIBREADLINE@

examplesdir = $(exampledir)
dist_examples_DATA = vtysh.conf.sample

EXTRA_DIST = extract.pl

vtysh_cmd_FILES = $(top_srcdir)/bgpd/*.c $(top_srcdir)/isisd/*.c \
		  $(top_srcdir)/ospfd/*.c $(top_srcdir)/ospf6d/*.c \
		  $(top_srcdir)/ripd/*.c $(top_srcdir)/ripngd/*.c \
		  $(top_srcdir)/pimd/pim_cmd.c \
		  $(top_srcdir)/lib/keychain.c $(top_srcdir)/lib/routemap.c \
		  $(top_srcdir)/lib/filter.c $(top_srcdir)/lib/plist.c \
		  $(top_srcdir)/lib/distribute.c $(top_srcdir)/lib/if_rmap.c \
		  $(top_srcdir)/lib/vrf.c \
		  $(top_srcdir)/lib/vty.c $(top_srcdir)/zebra/debug.c \
		  $(top_srcdir)/zebra/interface.c \
		  $(top_srcdir)/zebra/irdp_interface.c \
		  $(top_srcdir)/zebra/rtadv.c $(top_srcdir)/zebra/zebra_vty.c \
		  $(top_srcdir)/zebra/zserv.c $(top_srcdir)/zebra/router-id.c \
		  $(top_srcdir)/zebra/zebra_routemap.c \
	          $(top_srcdir)/zebra/zebra_fpm.c \
	          $(BGP_VNC_RFAPI_SRC) $(BGP_VNC_RFP_SRC)

vtysh_cmd.c: $(vtysh_cmd_FILES)
	./$(EXTRA_DIST) $(vtysh_cmd_FILES) > vtysh_cmd.c

