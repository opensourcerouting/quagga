/*
 * Module: netlink_send.cc
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
#include <errno.h>
#include <arpa/inet.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <syslog.h>

#include <vector>
#include <string>
#include <iostream>

#include "netlink_utils.hh"
#include "netlink_send.hh"

using namespace std;


/**
 *
 *
 **/
NetlinkSend::NetlinkSend(bool debug) : _debug(debug)
{
}


/**
 *
 *
 **/
NetlinkSend::~NetlinkSend()
{
}

/**
 *
 *
 **/
int
NetlinkSend::send_set(int sock, int ifindex, uint32_t local_addr, uint32_t addr, int mask_len, int type)
{
  int ret;
  struct sockaddr_nl snl;
  struct {
    struct nlmsghdr     n;
    struct ifaddrmsg    ifa;
    char                buf[256];
  } req;

  /* Check netlink socket. */
  if (sock < 0) {
    syslog(LOG_ERR,"sock is not active, exiting");
    cerr << "sock is not active, exiting" << endl;
    return -1;
  }

  if (_debug) {
    struct in_addr in;
    in.s_addr = local_addr;
    char *lbuf = inet_ntoa(in);

    in.s_addr = addr;
    char *buf = inet_ntoa(in);

    char sbuf[1024];
    sprintf(sbuf, "NetlinkSend::send_set(): %d, %s/%d, to this address: %s, on interface: %d",type,buf,mask_len,lbuf,ifindex);
    cout << sbuf << endl;

    syslog(LOG_INFO,sbuf);
  }

  memset(&req, 0, sizeof(req));
  
  req.n.nlmsg_len       = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
  req.n.nlmsg_flags     = NLM_F_REQUEST;
  req.n.nlmsg_pid       = getpid();
  req.n.nlmsg_type      = type;
  req.n.nlmsg_seq       = time(NULL);
  req.ifa.ifa_family    = AF_INET;
  req.ifa.ifa_index     = ifindex;
  req.ifa.ifa_prefixlen = mask_len;

  //  addr = htonl( addr );
  addattr_l(&req.n, sizeof(req), IFA_LOCAL, &local_addr, sizeof(local_addr) );

  in_addr_t broadcast_addr = ipv4_broadcast_addr(local_addr,mask_len);
  addattr_l(&req.n, sizeof(req), IFA_BROADCAST, &broadcast_addr, sizeof(broadcast_addr) );

  if (addr != -1 && local_addr != addr) {
    addattr_l(&req.n, sizeof(req), IFA_ADDRESS, &addr, sizeof(addr) );
  }

  memset(&snl, 0, sizeof(snl));
  snl.nl_family = AF_NETLINK;

  ret = sendto (sock, (void*) &req, sizeof req, 0, 
		(struct sockaddr*) &snl, sizeof snl);
  if (ret < 0) {
    syslog(LOG_ERR,"netlink_send failed on send: %d, %d",ret,errno);
    cerr << "netlink_send failed on send: " << ret << ", " << errno << endl;
    return -1;
  }
  return 0;
}

/**
 *
 *
 **/
int
NetlinkSend::send_set_route(int sock, int ifindex, uint32_t local_addr, uint32_t dst_addr, int mask_len, int type, int table, int rtn_type, int rt_scope)
{
  int ret;
  struct sockaddr_nl snl;
  struct {
    struct nlmsghdr     n;
    struct rtmsg        rt_message;
    char                buf[8192];
  } req;
  req.rt_message.rtm_table = 0;
  req.rt_message.rtm_protocol = 0;
  req.rt_message.rtm_scope = 0;
  req.rt_message.rtm_type = 0;
  req.rt_message.rtm_src_len = 0;
  req.rt_message.rtm_dst_len = 0;
  req.rt_message.rtm_tos = 0;

  memset(&snl, 0, sizeof(snl));
  snl.nl_family = AF_NETLINK;

  /* Check netlink socket. */
  if (sock < 0) {
    syslog(LOG_ERR,"sock is not active, exiting");
    cerr << "sock is not active, exiting" << endl;
    return -1;
  }

  if (_debug) {
    struct in_addr in;
    in.s_addr = local_addr;
    char *lbuf = inet_ntoa(in);

    in.s_addr = dst_addr;
    char *buf = inet_ntoa(in);

    char sbuf[1024];
    sprintf(sbuf, "NetlinkSend::send_set_route(): %d, %s/%d, to this address: %s, on interface: %d, for this table: ",type,buf,mask_len,lbuf,ifindex,table);
    cout << sbuf << endl;

    syslog(LOG_INFO,sbuf);
  }

  memset(&req, 0, sizeof(req));
  
  req.n.nlmsg_len       = NLMSG_LENGTH(sizeof(struct rtmsg));
  if (type == RTM_NEWROUTE) {
    req.n.nlmsg_flags = NLM_F_REQUEST|NLM_F_CREATE|NLM_F_EXCL;
  }
  else {
    req.n.nlmsg_flags     = NLM_F_REQUEST;
  }
  req.n.nlmsg_pid       = getpid();
  req.n.nlmsg_type      = type;
  req.n.nlmsg_seq       = time(NULL);

  req.rt_message.rtm_family    = AF_INET;
  req.rt_message.rtm_dst_len   = mask_len;
  req.rt_message.rtm_table     = table;
  if (type == RTM_NEWROUTE) {        
    req.rt_message.rtm_protocol = RTPROT_KERNEL;
    if (rt_scope != -1) {
      req.rt_message.rtm_scope = rt_scope;//RT_SCOPE_HOST; //will need to pass this in to get RT_SCOPE_HOST
    }
    if (rtn_type != -1) {
      req.rt_message.rtm_type = rtn_type;//RTN_LOCAL;
    }
  }
  else {
    req.rt_message.rtm_scope = RT_SCOPE_NOWHERE;
    req.rt_message.rtm_type = RTN_UNSPEC;
  }

  addattr_l(&req.n, sizeof(req), RTA_PREFSRC, &local_addr, sizeof(local_addr));
  addattr_l(&req.n, sizeof(req), RTA_DST, &dst_addr, sizeof(dst_addr));
  addattr32(&req.n, sizeof(req), RTA_OIF, ifindex);

  if (_debug) {
    cout << "NetlinkSend::send_set_route():" << endl;
    cout << "  interface: " << ifindex << endl;
    cout << "  type: " << string(req.n.nlmsg_type == RTM_NEWROUTE ? string("RTM_NEWROUTE") : string("RTM_DELROUTE")) << endl;
    cout << "  flags: " << req.n.nlmsg_flags << endl;
    cout << "  protocol: " << int(req.rt_message.rtm_protocol) << endl;
    cout << "  scope: " << int(req.rt_message.rtm_scope) << endl;
    cout << "  addr(s): " << local_addr << ", " << dst_addr << ", " << mask_len << ", " << int(req.rt_message.rtm_dst_len) << endl;
    cout << endl;
  }

  ret = sendto (sock, (void*) &req, sizeof req, 0, 
		(struct sockaddr*) &snl, sizeof snl);
  if (ret < 0) {
    syslog(LOG_ERR,"netlink_send_route failed on send: %d, %d",ret,errno);
    cerr << "netlink_send_route failed on send: " << ret << ", " << errno << endl;
    return -1;
  }
  return 0;
}

/**
 *
 *
 **/
int
NetlinkSend::send_get(int sock, int type, int ifindex)
{
  int ret;
  struct sockaddr_nl snl;
  struct {
    struct nlmsghdr     n;
    struct ifaddrmsg    ifa;
    char                buf[256];
  } req;

  /* Check netlink socket. */
  if (sock < 0) {
    syslog(LOG_ERR,"sock is not active, exiting");
    cerr << "sock is not active, exiting" << endl;
    return -1;
  }

  if (_debug) {
    char sbuf[1024];
    sprintf(sbuf,"NetlinkSend::send_get(): type: %d, ifindex: %d",type,ifindex);
    cout << sbuf << endl;
    syslog(LOG_INFO,sbuf);
  }

  memset(&req, 0, sizeof(req));
  
  req.n.nlmsg_len       = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
  if (ifindex > -1) {
    req.n.nlmsg_flags     = NLM_F_REQUEST | NLM_F_MATCH;
  }
  else {
    req.n.nlmsg_flags     = NLM_F_REQUEST | NLM_F_DUMP;
  }
  req.n.nlmsg_pid       = getpid();
  req.n.nlmsg_type      = type;
  req.n.nlmsg_seq       = time(NULL);
  req.ifa.ifa_family    = AF_INET;
  if (ifindex > -1) {
    req.ifa.ifa_index     = ifindex;
  }

  /*
    note bug right now in that all interfaces are provided back when a specific index is requested
   */  
  memset(&snl, 0, sizeof(snl));
  snl.nl_family = AF_NETLINK;
  
  ret = sendto (sock, (void*) &req, sizeof req, 0, 
		(struct sockaddr*) &snl, sizeof snl);
  if (ret < 0) {
    syslog(LOG_ERR,"netlink_send failed on send: %d, %d",ret,errno);
    cerr << "netlink_send failed on send: " << ret << ", " << errno << endl;
    return -1;
  }
  return 0;
}

/**
 *
 *
 **/
int 
NetlinkSend::addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen)
{
        int len = RTA_LENGTH(alen);
        struct rtattr *rta;

        if ((int)NLMSG_ALIGN(n->nlmsg_len) + len > maxlen)
                return -1;
        rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
        rta->rta_type = type;
        rta->rta_len = len;
        memcpy(RTA_DATA(rta), data, alen);
        n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
        return 0;
}

int 
NetlinkSend::addattr32(struct nlmsghdr *n, int maxlen, int type, int data)
{
  int len;
  struct rtattr *rta;

  len = RTA_LENGTH (4);

  if (NLMSG_ALIGN (n->nlmsg_len) + len > maxlen)
    return -1;

  rta = (struct rtattr *) (((char *) n) + NLMSG_ALIGN (n->nlmsg_len));
  rta->rta_type = type;
  rta->rta_len = len;
  memcpy (RTA_DATA (rta), &data, 4);
  n->nlmsg_len = NLMSG_ALIGN (n->nlmsg_len) + len;

  return 0;
}
