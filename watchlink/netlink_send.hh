/*
 * Module: netlink_send.hh
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#ifndef __NETLINK_SEND_HH__
#define __NETLINK_SEND_HH__

class NetlinkSend
{
public:
  NetlinkSend(bool debug);
  ~NetlinkSend();
  
  int
  send_get(int sock, int type, int ifindex = -1);

  int
  send_set(int sock, int ifindex, uint32_t local_addr, uint32_t addr, int mask_len, int type);

  int
  send_set_route(int sock, int ifindex, uint32_t local_addr, uint32_t dst_addr, int mask_len, int type, int table, int rtn_type, int rt_scope);

private:
  int 
  addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen);

  int 
  addattr32(struct nlmsghdr *n, int maxlen, int type, int data);

private:
  bool _debug;

};

#endif //__NETLINK_SEND_HH__
