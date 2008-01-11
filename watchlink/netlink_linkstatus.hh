/*
 * Module: netlink_linkstatus.hh
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
#ifndef __NETLINK_LINKSTATUS_HH__
#define __NETLINK_LINKSTATUS_HH__

#include <string>
#include <map>
#include "netlink_event.hh"
#include "netlink_send.hh"

class NetlinkLinkStatus
{
public:
  typedef std::map<int,bool> IfaceStateColl;
  typedef std::map<int,bool>::iterator IfaceStateIter;

public:
  NetlinkLinkStatus(int send_sock, const std::string &link_dir, bool debug);
  ~NetlinkLinkStatus();

  void
  process(const NetlinkEvent &event);

private:
  int
  process_up(const NetlinkEvent &event);
  
  int
  process_down(const NetlinkEvent &event);
  
  int
  process_going_up(const NetlinkEvent &event);
  
  int
  process_going_down(const NetlinkEvent &event);
  

private:
  NetlinkSend _nl_send;
  int _send_sock;
  std::string _link_dir;
  bool _debug;

  //keeps track of down messages where we've issued a 
  //request for addresses but haven't received msg yet.
  IfaceStateColl _iface_state_coll;
  
};

#endif //__NETLINK_LINKSTATUS_HH__
