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


#ifndef __NETLINK_HEAD_HH__
#define __NETLINK_HEAD_HH__

#include <list>

#include "libxorp/eventloop.hh"
#include "libxorp/exceptions.hh"

#include "netlink_listener.hh"
#include "ifmgr_xrl_mirror_target.hh"
#include "netlink_event.hh"
#include "netlink_send.hh"

/**
 * NetlinkSocket class opens a netlink socket and forwards data arriving
 * on the socket to NetlinkSocketObservers.  The NetlinkSocket hooks itself
 * into the EventLoop and activity usually happens asynchronously.
 */
class NetlinkHead {
public:
    NetlinkHead(EventLoop& eventloop);
    ~NetlinkHead();

    /**
     * Start the netlink socket operation.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int start(string& error_msg, IfMgrXrlMirrorTarget *tgt);

    /**
     * Stop the netlink socket operation.
     * 
     * @param error_msg the error message (if error).
     * @return XORP_OK on success, otherwise XORP_ERROR.
     */
    int stop(string& error_msg);

    void dump() const;


private:
    /**
     * Read data available for NetlinkHead and invoke
     * NetlinkHeadObserver::nlsock_data() on all observers of netlink
     * socket.
     */
    void io_event(XorpFd fd, IoEventType sm);

    NetlinkHead& operator=(const NetlinkHead&);	// Not implemented
    NetlinkHead(const NetlinkHead&);		// Not implemented

    EventLoop&	 _eventloop;
    int          _fd;
    NetlinkListener _nl_listen;
    IfMgrXrlMirrorTarget *_tgt;
    bool _send_request;
    bool _done_prime;
    NetlinkSend _nl_send;
};

#endif // __NETLINK_HEAD_HH__
