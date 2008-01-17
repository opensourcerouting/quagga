/*
 * Module: netlink_send.hh
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

private:
  int 
  addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen);

private:
  bool _debug;

};

#endif //__NETLINK_SEND_HH__
