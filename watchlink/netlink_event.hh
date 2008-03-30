/*
 * Module: netlink_event.hh
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#ifndef __NETLINK_EVENT_HH__
#define __NETLINK_EVENT_HH__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <string>
#include <vector>
#include <ostream>
#include <linux/rtnetlink.h>

class Mac
{
public:
  Mac() {}
private:
  unsigned char _addr[6];
};

class IPv4 
{
public:
  IPv4() : _addr(-1) {}
  IPv4(uint32_t addr) : _addr(addr) {}
  virtual ~IPv4() {}

  uint32_t 
  get() {return _addr;}

  virtual
  std::string 
  str() 
  {
    in_addr addr;
    addr.s_addr = _addr;
    char* buf = inet_ntoa(addr);
    return std::string(buf);
  }
  
protected:
  uint32_t _addr;
};


class IPv4net : protected IPv4
{
public:
  IPv4net() : IPv4(-1),_mask_length(32) {}
  IPv4net(uint32_t addr, char mask_len) : IPv4(addr),_mask_length(mask_len) {}
  IPv4net(std::string &net) {
    int pos = net.find("/");
    if (pos == std::string::npos) {
      pos = net.length();
      _mask_length = 32;
    }
    else {
      uint32_t m_val = strtoul(net.substr(pos+1,net.length()-1).c_str(),NULL,10);
      if (m_val > 32) {
	m_val = 32;
      }
      _mask_length = m_val;
    }
    uint32_t a_val = inet_addr(net.substr(0,pos).c_str());
    if (a_val == INADDR_NONE) {
      this->_addr = 0;
      return;
    }
    this->_addr = a_val;
  }

  uint32_t 
  get_addr() {return get();}

  char
  get_mask_length() {return _mask_length;}

  std::string 
  str() 
  {
    in_addr addr;
    addr.s_addr = _addr;
    char *buf = inet_ntoa(addr);
    
    std::string tmp(buf);

    char buf2[80];
    sprintf(buf2,"%d",_mask_length);
    tmp += std::string("/") + buf2;
    return tmp;
  }

private:
  char _mask_length;
};


/**
 *
 *
 **/
class NetlinkEvent
{
public:
  NetlinkEvent(int type,
	       std::string iface,
	       int mtu,
	       unsigned char *mac,
	       bool enabled,
	       bool running,		    
	       IPv4 local,
	       IPv4 addr,
	       IPv4 broadcast,
	       int mask_len,
	       int index);

  NetlinkEvent() :
  _type(-1),
  _mtu(-1),
  _enabled(false),
  _running(false),
  _mask_len(-1),
  _index(-1) {}

  ~NetlinkEvent();


  std::string 
  get_iface() const {return _iface;}
  
  int 
  get_mtu() const {return _mtu;}

  std::string
  get_mac_str() const {
    char buf[18];
      sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
	  _mac[0], _mac[1], _mac[2],
	  _mac[3], _mac[4], _mac[5]
	  );
    return std::string(buf);
  }

  int
  get_type() const {return _type;}
  /*
  unsigned char*
  get_mac() const {
    return _mac;
  }
  */

  int
  get_ifi_type() const {return _ifinfo.ifi_type;}

  bool
  get_enabled() const {return _enabled;}

  bool
  get_running() const {return _running;}

  IPv4
  get_local_addr() const {return _local;}
  
  IPv4
  get_addr() const {return _addr;}
  
  IPv4
  get_broadcast() const {return _broadcast;}

  int
  get_mask_len() const {return _mask_len;}

  bool
  is_link_up() const {return _enabled;}

  int
  get_index() const {return _index;}

  bool 
  is_vif() const {return _vif;}

  void
  log();

  void
  set_ifinfomsg(ifinfomsg *ifinfo) {_ifinfo = *ifinfo;}

  std::string
  get_ifinfomsg();

private:
  int _type;
  std::string _iface;
  bool _vif;
  int _mtu;
  unsigned char _mac[6];
  bool _enabled;
  bool _running;
  IPv4 _local;
  IPv4 _addr;
  IPv4 _broadcast;
  int _mask_len;
  int _index;

  //debug
  struct ifinfomsg _ifinfo;
};



/**
 *
 *
 **/
class NetlinkEventManager
{
public:
  typedef std::vector<NetlinkEvent> NLEventColl;
  typedef std::vector<NetlinkEvent>::iterator NLEventIter;

public: //methods
  friend std::ostream & operator<< (std::ostream &, const NetlinkEvent &);

  NetlinkEventManager(bool debug);

  ~NetlinkEventManager();

  void 
  process(unsigned char *pkt, int size);

  bool
  pop(NetlinkEvent &e);

  

private: //methods
  void
  parse_msg(const struct nlmsghdr*);

private: //variables
  NLEventColl _coll;
  bool _debug;
};


#endif // __NETLINK_EVENT_HH__
