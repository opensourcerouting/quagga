#include <stdio.h>
#include <sys/socket.h>
#include <iostream>
#include <string>
#include "rl_str_proc.hh"
#include "netlink_send.hh"
#include "netlink_event.hh"
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
    cerr << "NetlinkListStatus::NetlinkLinkStatus(), send sock is bad value" << endl;
  }

  if (_link_dir.empty()) {
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
    cerr << "NetlinkLinkStatus::process_down(), failed to open state file" << endl;
    return -1; //means we are still up, ignore...
  }

  char str[1025];
  while (fgets(str, 1024, fp)) {
    string line(str);

    StrProc tokens(line, ",");
    if (tokens.size() != 3) {
      cerr << "NetlinkLinkStatus::process_up(), failure to parse link status file, exiting(): " << line << ", size: " << tokens.size() << endl;
      fclose(fp);
      return -1;
    }

    int ifindex = strtoul(tokens.get(0).c_str(),NULL,10);
    uint32_t addr = strtoul(tokens.get(1).c_str(),NULL,10);
    int mask_len = strtoul(tokens.get(2).c_str(),NULL,10);
    
    //reinsert addresses to interface
    if (_nl_send.send_set(_send_sock, ifindex, addr, mask_len, RTM_NEWADDR)) {
      cerr << "NetlinkLinkStatus::process_up(), failure in setting interface back to up" << endl;
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

  if (event.get_index() < 0 || event.get_addr().get() == 0 || event.get_mask_len() < 1) {
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
    cerr << "NetlinkLinkStatus::process_down(), failed to open state file" << endl;
    return -1; 
  }

  //create file on system
  //CRAJ--NEED TO HAVE THIS BE FROM A COLLECTION??? DEPENDS ON FORMAT OF NETLINK MSG
  sprintf(buf,"%d",event.get_index());
  string line = string(buf) + ",";
  sprintf(buf,"%d",event.get_addr().get());
  line += string(buf) + ",";
  sprintf(buf,"%d",event.get_mask_len());
  line += string(buf) + "\n";
  
  fputs(line.c_str(),fp);


 //pull interface addresses
  if (_nl_send.send_set(_send_sock, event.get_index(), event.get_addr().get(), event.get_mask_len(), RTM_DELADDR)) {
    fclose(fp);
    return -1;
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
