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
#include <linux/rtnetlink.h>
#include <syslog.h>

#include <vector>
#include <string>
#include <iostream>

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
    char *buf = inet_ntoa(in);
    cout << "NetlinkSend::send_set(): " << type << ", " << buf << "/" << mask_len;

    in.s_addr = addr;
    buf = inet_ntoa(in);
    cout << ", to this address: " << buf << ", on interface: " << ifindex << endl;
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
  addattr_l(&req.n, sizeof(req), IFA_LOCAL, &local_addr, sizeof(addr) );
  if (local_addr != addr) {
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

