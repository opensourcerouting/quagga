/*
 * bgp_rpki.c
 *
 *  Created on: 15.02.2013
 *      Author: Michael Mester
 */

#include <zebra.h>

#include "log.h"

#include "bgpd/rpkiRTR/bgp_rpki.h"
#include "rtrlib/rtrlib.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"

extern void test_rpki(void){
  zlog_debug("XXX RPKI include works!!!!");
}

static void update_cb(struct pfx_table* p, const pfx_record rec, const bool added){
  char ip[INET6_ADDRSTRLEN];
  if(added)
      printf("+ ");
  else
      printf("- ");
  ip_addr_to_str(&(rec.prefix), ip, sizeof(ip));
  printf("%-18s %3u-%-3u %10u\n", ip, rec.min_len, rec.max_len, rec.asn);
}

void rpki_init(void){
  tr_socket tr_tcp;
  tr_tcp_config config = {
        "rpki.realmv6.org",
        (const char*) 42420,
  };

  tr_tcp_init(&config, &tr_tcp);
  rtr_socket rtr;
  rtr.tr_socket = &tr_tcp;

  rtr_mgr_group groups[1];
  groups[0].sockets_len = 1;
  groups[0].sockets = malloc(1 * sizeof(rtr_socket*));
  groups[0].sockets[0] = &rtr;
  groups[0].preference = 1;

  rtr_mgr_config conf;
  conf.groups = groups;
  conf.len = 1;
  rtr_mgr_init(&conf, 1, 520, &update_cb);
//  zlog_debug("Start");
  rtr_mgr_start(&conf);
//  printf("%-40s   %3s   %3s   %3s\n", "Prefix", "Prefix Length", "", "ASN");
  pause();
//  zlog_debug("Stop");
  rtr_mgr_stop(&conf);
  rtr_mgr_free(&conf);
  free(groups[0].sockets);
}

//Validate the BGP-Router for 10.10.0.0/24 from the Origin AS 12345.
//void validate_prefix(){
//  ip_addr prefix;
//  ip_str_to_addr("10.10.0.0", &prefix);
//  pfxv_state result;
//  rtr_mgr_validate(&conf, 12345, &prefix, 24, &result);
//}
