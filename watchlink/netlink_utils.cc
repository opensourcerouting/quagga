#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <syslog.h>

#include "netlink_utils.hh"


/* Maskbit. */
static u_char maskbit[] = {0x00, 0x80, 0xc0, 0xe0, 0xf0,
			         0xf8, 0xfc, 0xfe, 0xff};

/* Convert masklen into IP address's netmask. */
void
masklen2ip (int masklen, struct in_addr *netmask)
{
  u_char *pnt;
  int bit;
  int offset;

  memset (netmask, 0, sizeof (struct in_addr));
  pnt = (unsigned char *) netmask;

  offset = masklen / 8;
  bit = masklen % 8;
  
  while (offset--)
    *pnt++ = 0xff;

  if (bit)
    *pnt = maskbit[bit];
}


in_addr_t
ipv4_broadcast_addr (in_addr_t hostaddr, int masklen)
{
  struct in_addr mask;

  masklen2ip (masklen, &mask);
  return (masklen != 32-1) ?
    /* normal case */
    (hostaddr | ~mask.s_addr) :
    /* special case for /31 */
    (hostaddr ^ ~mask.s_addr);
}

in_addr_t
ipv4_first_addr (in_addr_t hostaddr, int masklen)
{
  struct in_addr mask;
  masklen2ip (masklen, &mask);
  return (hostaddr & mask.s_addr);
}

