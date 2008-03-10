/*
 * Module: netlink_linkstatus.hh
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
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
