#ifndef __NETLINK_LISTENER_HH__
#define __NETLINK_LISTENER_HH__

#include "netlink_event.hh"
#include <string>

class NetlinkListener
{
public: //methods
  NetlinkListener();
  ~NetlinkListener();

  /*
   * returns socket fd
   */
  int
  init();

  bool
  process(NetlinkEvent &e);

  int
  get_sock() {return _fd;}

  void
  set_multipart(bool state) {_is_multipart_message_read = state;}

private: //methods
int
comm_sock_set_rcvbuf(int sock, int desired_bufsize, int min_bufsize);

private: //vraiables
  int _fd;
  bool _is_multipart_message_read;
  NetlinkEventManager _nl_event_mgr;
};

#endif //__NETLINK_LISTENER_HH__
