#ifndef __NETLINK_UTILS_HH__
#define __NETLINK_UTILS_HH__

#include <arpa/inet.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/socket.h>


in_addr_t
ipv4_broadcast_addr (in_addr_t hostaddr, int masklen);

void
masklen2ip (int masklen, struct in_addr *netmask);

in_addr_t
ipv4_first_addr (in_addr_t hostaddr, int masklen);


#endif //__NETLINK_UTILS_HH__
