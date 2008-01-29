/*
 * Module: netlink_linkstatus.cc
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
#include <stdio.h>
#include <sys/socket.h>
#include <iostream>
#include <string>
#include <syslog.h>
#include "rl_str_proc.hh"
#include "netlink_send.hh"
#include "netlink_event.hh"
#include "netlink_utils.hh"
#include "netlink_linkstatus.hh"


using namespace std;

/**
 *
 *
 **/
NetlinkLinkStatus::NetlinkLinkStatus(int send_sock, const string &link_dir, bool debug) : 
  _nl_send(debug),
  _send_sock(send_sock), 
  _link_dir(link_dir), 
  _debug(debug)
{
  if (_send_sock < 0) {
    syslog(LOG_ERR,"NetlinkListStatus::NetlinkLinkStatus(), send sock is bad value");
    cerr << "NetlinkListStatus::NetlinkLinkStatus(), send sock is bad value" << endl;
  }

  if (_link_dir.empty()) {
    syslog(LOG_ERR,"NetlinkListStatus::NetlinkLinkStatus(), no link status directory specified");
    cerr << "NetlinkListStatus::NetlinkLinkStatus(), no link status directory specified" << endl;
  }
}

/**
 *
 *
 **/
NetlinkLinkStatus::~NetlinkLinkStatus()
{
}

/**
 *
 *
 **/
void
NetlinkLinkStatus::process(const NetlinkEvent &event)
{
  bool state_event = false;

  IfaceStateIter iter = _iface_state_coll.find(event.get_index());
  if (iter == _iface_state_coll.end()) {
    if (event.get_type() == RTM_NEWLINK || event.get_type() == RTM_DELLINK) {
      //start maintaining state information here.
      _iface_state_coll.insert(pair<int,bool>(event.get_index(),event.get_running()));

      //let's clean up directory here!
      char buf[40];
      sprintf(buf,"%d",event.get_index());
      string file = _link_dir + "/" + buf;
      unlink(file.c_str());
    }
    return;
  }

  bool running_old = iter->second;
  bool running_new = event.get_running();

  //capture link status on link messages only
  if (event.get_type() == RTM_NEWLINK || 
      event.get_type() == RTM_DELLINK) {
    //    _iface_state_coll.insert(pair<int,bool>(event.get_index(),event.get_running()));
    _iface_state_coll[event.get_index()] = event.get_running();
    if (running_old != running_new) {
      state_event = true;
    }
  }

  //is this a transition from up->down, or down->up?
  if (state_event) {
    if (running_new) {
      process_going_up(event);
    }
    else {
      process_going_down(event);
    }
  }
  else {
    if (running_old) {
      process_up(event);
    }
    else {
      process_down(event);
    }
  }
}

/**
 *
 * file is in the format of IFINDEX,IP,MASK
 *
 **/
int
NetlinkLinkStatus::process_up(const NetlinkEvent &event)
{
  if (_debug) {
    cout << "NetlinkLinkStatus::process_up(): " << event.get_iface() << endl;
  }
  //can't think of anything that needs to go here yet.
  return 0;
}

/**
 *
 *
 **/
int
NetlinkLinkStatus::process_going_up(const NetlinkEvent &event)
{
  if (_debug) {
    cout << "NetlinkLinkStatus::process_going_up(): " << event.get_iface() << endl;
  }

  //check for link status file, otherwise return
  char buf[40];
  sprintf(buf,"%d",event.get_index());
  string file = _link_dir + "/" + buf;
  FILE *fp = fopen(file.c_str(), "r");
  if (fp == NULL) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_going_up(), failed to open state file");
    //    cerr << "NetlinkLinkStatus::process_going_up(), failed to open state file" << endl;
    return -1; //means we are still up, ignore...
  }

  char str[1025];
  while (fgets(str, 1024, fp)) {
    string line(str);

    StrProc tokens(line, ",");
    if (tokens.size() != 4) {
      syslog(LOG_INFO,"NetlinkLinkStatus::process_up(), failure to parse link status file, exiting(): %s, size: %d", line.c_str(), tokens.size());
      //      cerr << "NetlinkLinkStatus::process_up(), failure to parse link status file, exiting(): " << line << ", size: " << tokens.size() << endl;
      fclose(fp);
      return -1;
    }

    int ifindex = strtoul(tokens.get(0).c_str(),NULL,10);
    uint32_t local_addr = strtoul(tokens.get(1).c_str(),NULL,10);
    //    uint32_t addr = strtoul(tokens.get(2).c_str(),NULL,10);
    int mask_len = strtoul(tokens.get(3).c_str(),NULL,10);
    
    bool err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, local_addr, 32, RTM_NEWROUTE, RT_TABLE_LOCAL, RTN_LOCAL, RT_SCOPE_HOST);
    if (err) {
      syslog(LOG_INFO,"NetlinkLinkStatus::process_up(), failure in setting interface back to up");
    }
    //COMPUTE FIRST ADDRESS
    uint32_t first_addr = ipv4_first_addr(local_addr, mask_len);
    err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, first_addr, 32, RTM_NEWROUTE, RT_TABLE_LOCAL, RTN_BROADCAST, RT_SCOPE_LINK);
    if (err) {
      syslog(LOG_INFO,"NetlinkLinkStatus::process_up(), failure in setting interface back to up");
    }
    //COMPUTE LAST ADDRESS
    uint32_t last_addr = ipv4_broadcast_addr(local_addr, mask_len);
    err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, last_addr, 32, RTM_NEWROUTE, RT_TABLE_LOCAL, RTN_BROADCAST, RT_SCOPE_LINK);
    if (err) {
      syslog(LOG_INFO,"NetlinkLinkStatus::process_up(), failure in setting interface back to up");
    }

    //reinsert addresses to interface
    err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, first_addr, mask_len, RTM_NEWROUTE, RT_TABLE_MAIN, RTN_UNICAST, RT_SCOPE_LINK);
    if (err) {
      syslog(LOG_INFO,"NetlinkLinkStatus::process_up(), failure in setting interface back to up");
    }
  }

  fclose(fp);

  //remove file
  unlink(file.c_str());

  return 0;
}
  
/**
 * send an ip flush command and capture all the rtm_deladdr messages to the file...
 *
 **/
int
NetlinkLinkStatus::process_down(const NetlinkEvent &event)
{
  if (_debug) {
    cout << "NetlinkLinkStatus::process_down(): " << event.get_iface() << endl;
  }

  if (event.get_type() != RTM_NEWADDR) {
    return 0;
  }

  if (event.get_index() < 0 || event.get_local_addr().get() == 0 || event.get_mask_len() < 1) {
    return 0;
  }

  if (_debug) {
    cout << "netlinkLinkStatus::process_down(), processing valid request" << endl;
  }

  //append to file...
  char buf[40];
  sprintf(buf,"%d",event.get_index());
  string file = _link_dir + "/" + buf;
  FILE *fp = fopen(file.c_str(), "a");
  if (fp == NULL) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_down(), failed to open state file");
    //    cerr << "NetlinkLinkStatus::process_down(), failed to open state file" << endl;
    return -1; 
  }

  int ifindex = event.get_index();
  uint32_t local_addr = event.get_local_addr().get();
  int mask_len = event.get_mask_len();

  //create file on system
  //CRAJ--NEED TO HAVE THIS BE FROM A COLLECTION??? DEPENDS ON FORMAT OF NETLINK MSG
  sprintf(buf,"%d",ifindex);
  string line = string(buf) + ",";
  sprintf(buf,"%d",local_addr);
  line += string(buf) + ",";
  sprintf(buf,"%d",event.get_addr().get());
  line += string(buf) + ",";
  sprintf(buf,"%d",mask_len);
  line += string(buf) + "\n";
  
  fputs(line.c_str(),fp);
  
  uint32_t first_addr = ipv4_first_addr(local_addr, mask_len);
  //reinsert addresses to interface
  bool err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, first_addr, mask_len, RTM_DELROUTE, RT_TABLE_MAIN, -1,-1);
  if (err) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_down(), failure in setting interface down");
  }

  uint32_t last_addr = ipv4_broadcast_addr(local_addr, mask_len);
  err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, last_addr, 32, RTM_DELROUTE, RT_TABLE_LOCAL, -1,-1);
  if (err) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_down(), failure in setting interface down");
  }
  
  err = _nl_send.send_set_route(_send_sock, ifindex, first_addr, first_addr, 32, RTM_DELROUTE, RT_TABLE_LOCAL, -1,-1);
  if (err) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_down(), failure in setting interface down");
  }

  err = _nl_send.send_set_route(_send_sock, ifindex, local_addr, local_addr, 32, RTM_DELROUTE, RT_TABLE_LOCAL, -1,-1);
  if (err) {
    syslog(LOG_INFO,"NetlinkLinkStatus::process_down(), failure in setting interface down");
  }

  fclose(fp);

  return 0;
}

int
NetlinkLinkStatus::process_going_down(const NetlinkEvent &event)
{
  if (_debug) {
    cout << "NetlinkLinkStatus::process_going_down(): " << event.get_iface() << "(" << event.get_index() << ")" << endl;
  }

  //pull interface addresses
  if (_nl_send.send_get(_send_sock, RTM_GETADDR, event.get_index())) {
    return -1;
  }
  return 0;
}
