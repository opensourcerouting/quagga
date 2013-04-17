/*
 * bgp_rpki.c
 *
 *  Created on: 15.02.2013
 *      Author: Michael Mester
 */

#include <zebra.h>
#include <pthread.h>
#include "prefix.h"
#include "log.h"
#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/rpki/bgp_rpki.h"
#include "bgpd/rpki/rpki_commands.h"
#include "rtrlib/rtrlib.h"
#include "rtrlib/rtr_mgr.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"
#include "rtrlib/transport/ssh/ssh_transport.h"

rtr_mgr_config rtr_config;

void rpki_init(void){
  install_cli_commands();
  polling_period = POLLING_PERIOD_DEFAULT;
  timeout = TIMEOUT_DEFAULT;
  apply_rpki_filter = APPLY_RPKI_FILTER_DEFAULT;
}


void rpki_start(){
  if((rtr_config.groups = get_rtr_mgr_groups()) == NULL){
      RPKI_DEBUG("No caches were found in config. Prefix validation is off.");
      return;
  }
  rtr_config.len = get_number_of_cache_groups();
  rtr_mgr_init(&rtr_config, polling_period, timeout, NULL);
  rtr_mgr_start(&rtr_config);
  RPKI_DEBUG("Waiting for rtr connection to synchronize.");
  while(!rtr_mgr_conf_in_sync(&rtr_config)){
      RPKI_DEBUG("Still waiting.");
      sleep(1);
  }
  RPKI_DEBUG("Got it!");
}

void rpki_finish(void){
  RPKI_DEBUG("Stopping");
  rtr_mgr_stop(&rtr_config);
  rtr_mgr_free(&rtr_config);
  free_rtr_mgr_groups(rtr_config.groups, rtr_config.len);
  delete_cache_group_list();
}

void rpki_test(void){
  RPKI_DEBUG("XXX RPKI include works!!!!");
}

int validation_policy_check(int validation_result){

  return validation_result == RPKI_VALID;
}

/*
 * return 1 if route is filtered
 * return 0 if route is not filtered
 */
int rpki_validation_filter(struct peer *peer, struct prefix *p,
    struct attr *attr, afi_t afi, safi_t safi) {
  // First check if filter has to be applied
  if (!apply_rpki_filter) {
    return 0;
  }
  /*
   Route Origin ASN: The origin AS number derived from a Route as
   follows:

   *  the rightmost AS in the final segment of the AS_PATH attribute
   in the Route if that segment is of type AS_SEQUENCE, or

   *  the BGP speaker's own AS number if that segment is of type
   AS_CONFED_SEQUENCE or AS_CONFED_SET or if the AS_PATH is empty,
   or

   *  the distinguished value "NONE" if the final segment of the
   AS_PATH attribute is of any other type.
   */
  int validation_result = validate_prefix(peer, p, attr);
  return validation_policy_check(validation_result);
}

int validate_prefix(struct prefix *prefix, uint32_t asn, uint8_t mask_len) {
  ip_addr ip_addr_prefix;
  pfxv_state result;

//  if(ip_str_to_addr(address, &prefix) == 0){
//    RPKI_DEBUG("ERROR validate_prefix: Could not make ip address out of string.");
//    return -1;
//  }

  switch (prefix->family) {
    case AF_INET:
      ip_addr_prefix.ver = IPV4;
      ip_addr_prefix.u.addr4.addr = (uint32_t)prefix->u.prefix4.s_addr;
      break;
    case AF_INET6:
#ifdef HAVE_IPV6
      ip_addr_prefix.ver = IPV6;
//      ip_addr_prefix.u.addr6.addr = (uint32_t[4])prefix->u.prefix6.s6_addr32;
#endif /* HAVE_IPV6 */
      break;
    default:
      return -1;
  }
  rtr_mgr_validate(&rtr_config, asn, &ip_addr_prefix, mask_len, &result);

  switch (result) {
    case BGP_PFXV_STATE_VALID:
      return RPKI_VALID;
    case BGP_PFXV_STATE_NOT_FOUND:
      return RPKI_NOTFOUND;
    case BGP_PFXV_STATE_INVALID:
      return RPKI_INVALID;
    default:
      break;
  }
  return -1;
}

// TODO implement method
//static void update_cb(struct pfx_table* p, const pfx_record rec, const bool added){
//  char ip[INET6_ADDRSTRLEN];
//  if(added)
//      printf("+ ");
//  else
//      printf("- ");
//  ip_addr_to_str(&(rec.prefix), ip, sizeof(ip));
//  printf("%-18s %3u-%-3u %10u\n", ip, rec.min_len, rec.max_len, rec.asn);
//}
