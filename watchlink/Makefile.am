## Process this file with automake to produce Makefile.in.

INCLUDES = @INCLUDES@ -I.. -I$(top_srcdir) -I$(top_srcdir)/lib 
DEFS = @DEFS@ -DSYSCONFDIR=\"$(sysconfdir)/\" 

sbin_PROGRAMS = watchlink

watchlink_SOURCES = watchlink.cc netlink_utils.cc netlink_send.cc netlink_listener.cc netlink_event.cc netlink_linkstatus.cc rl_str_proc.cc
