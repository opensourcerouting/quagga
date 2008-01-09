// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

// Copyright (c) 2001-2006 International Computer Science Institute
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software")
// to deal in the Software without restriction, subject to the conditions
// listed in the XORP LICENSE file. These conditions include: you must
// preserve this copyright notice, and you cannot mention the copyright
// holders in advertising related to the Software without their permission.
// The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
// notice is a summary of the XORP LICENSE file; the license in that file is
// legally binding.

#include "libfeaclient_module.h"
#include "libxorp/xorp.h"
#include "libxorp/xlog.h"
#include "libxorp/debug.h"

#include <linux/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <sys/time.h>

#include <syslog.h>
#include <map>

#include <errno.h>

#include "netlink_listener.hh"
#include "netlink_event.hh"
#include "netlink_send.hh"
#include "ifmgr_xrl_mirror_target.hh"

#include "netlink_head.hh"

//
// Netlink Sockets (see netlink(7)) communication with the kernel
//

//quagga implementation of netlink notifications
//http://cvs.quagga.net/cgi-bin/viewcvs.cgi/quagga/zebra/rt_netlink.c?rev=1.9

/**
 *
 *
 **/
NetlinkHead::NetlinkHead(EventLoop& eventloop)
    : _eventloop(eventloop), 
      _fd(-1), 
      _tgt(NULL), 
      _send_request(true), 
      _done_prime(false)
{
    XLOG_INFO("NetlinkHead::NetlinkHead() entry");
    
}

/**
 *
 *
 **/
NetlinkHead::~NetlinkHead()
{
    string error_msg;

    if (stop(error_msg) != XORP_OK) {
	XLOG_ERROR("NetlinkHead::~NetlinkHead() Cannot stop the netlink socket: %s", error_msg.c_str());
    }
}

/**
 *
 *
 **/
int
NetlinkHead::stop(string& error_msg)
{
    //
    // Remove the socket from the event loop
    //
    if (!_eventloop.remove_ioevent_cb(_fd, IOT_READ)) {
	error_msg = string("Failed to remove netlink socket from EventLoop");
	return (XORP_ERROR);
    }

    //
    // XXX: Even if the system doesn not support netlink sockets, we
    // still allow to call the no-op stop() method and return success.
    // This is needed to cover the case when a NetlinkHead is allocated
    // but not used.
    //
    return (XORP_OK);
}

/**
 *
 *
 **/
int
NetlinkHead::start(string& error_msg, IfMgrXrlMirrorTarget *tgt)
{
    XLOG_INFO("NetlinkHead::start() entry");

    _tgt = tgt;

    //init vyatta netlink listener 'ere
    _fd = _nl_listen.init();
    //
    // Add the socket to the event loop
    //
    if (_eventloop.add_ioevent_cb(_fd, IOT_READ,
				callback(this, &NetlinkHead::io_event))
	== false) {
	error_msg = c_format("Failed to add netlink socket to EventLoop");
	stop(error_msg);
	return (XORP_ERROR);
    }

    //now in order to prime the pump we need to send a request to netlink to initialize our data structure...
    _nl_send.send(_fd, RTM_GETLINK);
    _nl_listen.set_multipart(true);
    return (XORP_OK);
}

void
NetlinkHead::dump() const
{
    IfMgrIfTree i = _tgt->iftree();
    IfMgrIfTree::IfMap if_coll = i.ifs();
    map<const string, IfMgrIfAtom>::iterator if_iter = if_coll.begin();
    while (if_iter != if_coll.end()) {
	syslog(LOG_USER | LOG_INFO, "NetlinkHead::dump(): interface: %s, mac: %s, mtu: %d", if_iter->first.c_str(), if_iter->second.mac().str().c_str(), if_iter->second.mtu_bytes());

	IfMgrIfAtom::VifMap vif_coll = if_iter->second.vifs();
	map<const string, IfMgrVifAtom>::iterator vif_iter = vif_coll.begin();
	while (vif_iter != vif_coll.end()) {
	    syslog(LOG_USER | LOG_INFO, "NetlinkHead::dump(): vif interface: %s, count: %d", vif_iter->first.c_str(), vif_iter->second.ipv4addrs().size());
	    map<const IPv4, IfMgrIPv4Atom>::iterator addr_iter = vif_iter->second.ipv4addrs().begin();
	    while (addr_iter != vif_iter->second.ipv4addrs().end()) {
		syslog(LOG_USER | LOG_INFO, "NetlinkHead::dump(): vif addrs: %s", addr_iter->first.str() .c_str());
		++addr_iter;
	    }
	    ++vif_iter;
	}
	++if_iter;
    }
    syslog(LOG_USER | LOG_INFO, "NetlinkHead::dump(): end");
}

/**
 *
 *
 **/
void
NetlinkHead::io_event(XorpFd fd, IoEventType type)
{
    string error_msg;
    UNUSED(fd);
    UNUSED(type);

    XLOG_INFO("NetlinkHead::io_event() entry(mike)");
    dump();

    if (_tgt == NULL) {
	XLOG_ERROR("NetlinkHead::io_event, tgt is not defined");
	return;
    }

    NetlinkEvent e;
    //hack to support bug in multipart flag from kernel
    while (_nl_listen.process(e) == true) {

	if (_done_prime == true) {
	    _nl_listen.set_multipart(false);
	}

	if (_send_request) {
	    _send_request = false;
	    _done_prime = true;
	    if (_nl_send.send(_fd, RTM_GETADDR) != 0) {
		XLOG_WARNING("NetlinkHead::io_event(), send error");
		continue;
	    }
	}

	//DEBUG CODE
	if (e.get_addr().str().empty() == false) {
	    XLOG_INFO("NetlinkHead::io_event(), received addr of: %s", e.get_addr().str().c_str());
	}

	if (e.get_iface().empty() == true) {
	    XLOG_WARNING("NetlinkHead::io_event(), iface is empty");
	    continue;
	}
	if (e.get_index() < 1) {
	    XLOG_WARNING("NetlinkHead::io_event(), index is less than one");
	    continue;
	}

	switch (e.get_type()) {
	case RTM_NEWLINK:
	    if (_tgt->fea_ifmgr_mirror_nl_set_link(e.get_iface(),
						   e.is_vif(),
						   e.get_mtu(),
						   e.get_mac(),
						   e.get_running(),
						   e.get_index()) != XrlCmdError::OKAY()) {
		XLOG_ERROR("NetlinkHead::io_event, error on RTM_NEWLINK");
	    }
	    continue;

	case RTM_DELLINK:
	    if (_tgt->fea_ifmgr_mirror_nl_interface_remove(e.get_iface(), e.is_vif()) != XrlCmdError::OKAY()) {
		XLOG_ERROR("NetlinkHead::io_event, error on RTM_DELLINK");
	    }
	    continue;

	case RTM_NEWADDR:
	    if (!e.get_addr().is_zero() && e.get_mask_len() > -1) {
		if (_tgt->fea_ifmgr_mirror_nl_set_prefix(e.get_iface(),
							 e.is_vif(),
							 e.get_index(),
							 e.get_addr(),
							 e.get_mask_len(),
							 e.get_broadcast(),
							 e.get_enabled()) != XrlCmdError::OKAY()) {
		    XLOG_ERROR("NetlinkHead::io_event, error on RTM_NEWADDR");
		}
		
	    }
	    continue;

	case RTM_DELADDR:
	    if (_tgt->fea_ifmgr_mirror_nl_ipv4_remove(e.get_iface(),
						      e.is_vif(),
						      e.get_addr()) != XrlCmdError::OKAY()) {
		XLOG_ERROR("NetlinkHead::io_event, error on RTM_DELADDR");
	    }
	    continue;
	}
    }
    XLOG_INFO("Netlink::io_event() exit(mike)");
    dump();
}


