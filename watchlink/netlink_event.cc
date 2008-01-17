/*
 * Module: netlink_event.cc
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
#include <linux/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <syslog.h>

#include <iostream>
#include <string>
#include "netlink_event.hh"

using namespace std;

/**
 *
 *
 **/
NetlinkEvent::NetlinkEvent(int type,
			   std::string iface,
			   int mtu,
			   unsigned char *mac,
			   bool enabled,
			   bool running,
			   IPv4 local,
			   IPv4 addr,
			   IPv4 broadcast,
			   int mask_len,
			   int index) :
  _type(type),
  _iface(iface),
  _vif(false),
  _mtu(mtu),
  _enabled(enabled),
  _running(running),
  _local(local),
  _addr(addr),
  _broadcast(broadcast),
  _mask_len(mask_len),
  _index(index)
{
  memcpy(_mac, mac, 6);
  if (_iface.find(".") != string::npos) {
    _vif = true;
  }
}

/**
 *
 *
 **/
NetlinkEvent::~NetlinkEvent()
{
}

/**
 *
 *
 **/
void
NetlinkEvent::log()
{
  syslog(LOG_USER | LOG_INFO, "NetlinkEvent::log(): type: %d, iface: %s, mtu: %d, local: %s, addr: %s, bc: %s, mask: %d, index: %d", _type, _iface.c_str(), _mtu, _local.str().c_str(), _addr.str().c_str(), _broadcast.str().c_str(), _mask_len, _index);
}

/**
 *
 *
 **/
NetlinkEventManager::NetlinkEventManager()
{

}

/**
 *
 *
 *
 **/
NetlinkEventManager::~NetlinkEventManager()
{
}

/**
 *
 *
 *
 **/
void
NetlinkEventManager::process(unsigned char* pkt, int size)
{
  if (size <= 0) {
    return;
  }

  size_t ps = size_t(size);

  const struct nlmsghdr* mh;
  for (mh = reinterpret_cast<const struct nlmsghdr*>(pkt);
       NLMSG_OK(mh, ps);
       mh = NLMSG_NEXT(const_cast<struct nlmsghdr*>(mh), ps)) {
    parse_msg(mh);
  }
}

/**
 *
 *
 **/
bool
NetlinkEventManager::pop(NetlinkEvent &e)
{
  char buf[20];
  sprintf(buf, "%d", _coll.size());
  
  NLEventIter iter = _coll.begin();
  if (iter != _coll.end()) {
    e = *iter;
    _coll.erase(iter);
    return true;
  }
  return false;
}

/**
 *
 *
 *
 **/
void 
NetlinkEventManager::parse_msg(const struct nlmsghdr *nlHdr)
{
  bool enabled;
  bool running;
  string iface;
  int mtu = -1;
  int index = -1;
  unsigned char mac[6];
  IPv4 addr, local, broadcast;
  int mask_len = -1;

  bzero(mac, 6);

  struct ifinfomsg* ifInfo = (struct ifinfomsg *)NLMSG_DATA(nlHdr);

  //link state flag 
  enabled = ifInfo->ifi_flags & IFF_UP;
  running = ifInfo->ifi_flags & IFF_RUNNING;
  index = ifInfo->ifi_index;

  struct rtattr* rtAttr = (struct rtattr *)IFLA_RTA(ifInfo);
  int rtLen = IFLA_PAYLOAD(nlHdr);

  switch (nlHdr->nlmsg_type) {
  case RTM_NEWLINK:
  case RTM_DELLINK:
  case RTM_NEWADDR:
  case RTM_DELADDR:
    for(;RTA_OK(rtAttr,rtLen);rtAttr = RTA_NEXT(rtAttr,rtLen)){
      if (nlHdr->nlmsg_type == RTM_NEWLINK ||
	  nlHdr->nlmsg_type == RTM_DELLINK) {
	switch(rtAttr->rta_type) {
	case IFLA_IFNAME:
	  iface = string((char*)RTA_DATA(rtAttr));
	  break;
	case IFLA_ADDRESS:
	  memcpy(mac, RTA_DATA(rtAttr), 6);
	  break;
	case IFLA_MTU:
	  mtu = *((unsigned int *)RTA_DATA(rtAttr));
	  break;
	default:
	  break;
	}
      }
      else if (nlHdr->nlmsg_type == RTM_NEWADDR ||
	       nlHdr->nlmsg_type == RTM_DELADDR) {
	uint32_t address;
	struct ifaddrmsg *ifAddrs;
	ifAddrs = (struct ifaddrmsg *)NLMSG_DATA(nlHdr);
	mask_len = ifAddrs->ifa_prefixlen;

	switch(rtAttr->rta_type) {
	case IFA_LOCAL:
	  address = *(uint32_t *)RTA_DATA(rtAttr);
	  local = IPv4(address);
	  break;
	case IFA_ADDRESS:
	  address = *(uint32_t *)RTA_DATA(rtAttr);
	  addr = IPv4(address);
	  break;
	case IFA_LABEL:
	  iface = *((char*)RTA_DATA(rtAttr));
	  break;
	case IFA_BROADCAST:
	  address = *(uint32_t *)RTA_DATA(rtAttr);
	  broadcast = IPv4(address);
	  break;
	default:
	  break;
	}
      }
    }
    {
      NetlinkEvent e(nlHdr->nlmsg_type,
		     iface,
		     mtu,
		     mac,
		     enabled,
		     running,
		     local,
		     addr,
		     broadcast,
		     mask_len,
		     index);

      e.set_ifinfomsg(ifInfo);
      
      e.log();
      _coll.push_back(e);
    }
    break;
  case NLMSG_ERROR: {
    struct nlmsgerr *err = (struct nlmsgerr*) NLMSG_DATA(nlHdr);
    syslog(LOG_ERR,"netlink message of type ERROR received: %s",strerror(-err->error));
    cerr << "netlink message of type ERROR received: " ;
    cerr << string(strerror(-err->error)) << endl;
  }
    break;
  case NLMSG_DONE:
    cerr << "netlink message of type DONE received" << endl;
    break;
  case NLMSG_NOOP:
    cerr << "netlink message of type NOOP received" << endl;
    break;
  default:
    cerr << "unknown netlink message type received" << endl;
    break;
  }
}


/**
 *
 *
 *
 **/
typedef struct {
  unsigned int iff_flag;
  char *name;
} iff_flags_name;


string
NetlinkEvent::get_ifinfomsg()
{
  string ret;
  char buf[40];

  sprintf(buf, "%uc", _ifinfo.ifi_family);
  ret = "ifi_family: " + string(buf) + ", ";
  sprintf(buf, "%us", _ifinfo.ifi_type);
  ret += "ifi_type: " + string(buf) + ", ";
  sprintf(buf, "%d", _ifinfo.ifi_index);
  ret += "ifi_index: " + string(buf) + ", ";
  sprintf(buf, "%ud", _ifinfo.ifi_flags);
  ret += "ifi_flags: " + string(buf) + ", ";
  sprintf(buf, "%ud", _ifinfo.ifi_change);
  ret += "ifi_change: " + string(buf);
  return ret;
}

/*
string
NetlinkEvent::operator<<(const ostream &o)
{
    UNUSED(o);
  return ("");
}
*/
std::ostream & operator <<(std::ostream & Stream, const NetlinkEvent & instance) 
{ 
  //  Stream << ... fields from instance 
  const NetlinkEvent foo = instance;
  return Stream; 
}


