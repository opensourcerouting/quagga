/*
 * Module: netlink_listener.hh
 *
 * **** License ****
 * Version: VPL 1.0
 *
 * The contents of this file are subject to the Vyatta Public License
 * Version 1.0 ("License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.vyatta.com/vpl
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * This code was originally developed by Vyatta, Inc.
 * Portions created by Vyatta are Copyright (C) 2008 Vyatta, Inc.
 * All Rights Reserved.
 *
 * Author: Michael Larson
 * Date: 2008
 * Description:
 *
 * **** End License ****
 *
 */
#ifndef __NETLINK_LISTENER_HH__
#define __NETLINK_LISTENER_HH__

#include "netlink_event.hh"
#include <set>
#include <string>

class NetlinkListener
{
public: //methods
  NetlinkListener(bool debug);
  ~NetlinkListener();

  /*
   * returns socket fd
   */
  int
  init();

  bool
  process(NetlinkEvent &e);

  bool
  process(NetlinkEvent &e, std::set<std::string> filter);

  int
  get_sock() {return _fd;}

  void
  set_multipart(bool state) {_is_multipart_message_read = state;}

private: //methods
int
comm_sock_set_rcvbuf(int sock, int desired_bufsize, int min_bufsize);

private: //vraiables
  int _fd;
  bool _is_multipart_message_read;
  NetlinkEventManager _nl_event_mgr;
};

#endif //__NETLINK_LISTENER_HH__
