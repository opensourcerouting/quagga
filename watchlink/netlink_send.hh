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
  send_set(int sock, int ifindex, uint32_t addr, int mask_len, int type);

private:
  int 
  addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen);

private:
  bool _debug;

};

#endif //__NETLINK_SEND_HH__
