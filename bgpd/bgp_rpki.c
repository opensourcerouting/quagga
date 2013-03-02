/*
 * bgp_rpki.c
 *
 *  Created on: 15.02.2013
 *      Author: Michael Mester
 */

#include <zebra.h>
#include <pthread.h>
#include "log.h"
#include "bgpd/bgp_rpki.h"
#include "rtrlib/rtrlib.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"

tr_tcp_config trTcpConfig = {
  "rpki.realmv6.org",
  "42420"
};
tr_socket trSocket;
rtr_socket rtrSocket;
rtr_mgr_group rtrMgrGroups[1];
rtr_mgr_config rtrMgrConfig;

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
  tr_tcp_init(&trTcpConfig, &trSocket);
  rtrSocket.tr_socket = &trSocket;
  rtrMgrGroups[0].sockets_len = 1;
  rtrMgrGroups[0].sockets = malloc(1 * sizeof(rtr_socket*));
  rtrMgrGroups[0].sockets[0] = &rtrSocket;
  rtrMgrGroups[0].preference = 1;
  rtrMgrConfig.groups = rtrMgrGroups;
  rtrMgrConfig.len = 1;
  rtr_mgr_init(&rtrMgrConfig, 1, 520, &update_cb);
  rtr_mgr_start(&rtrMgrConfig);
}

void rpki_finish(void){
    zlog_debug("RPKI: Stopping");
    rtr_mgr_stop(&rtrMgrConfig);
    rtr_mgr_free(&rtrMgrConfig);
    free(rtrMgrGroups[0].sockets);
}

extern void test_rpki(void){
  pthread_t thread_id;
  zlog_debug("XXX RPKI include works!!!!");
}

//Validate the BGP-Router for 10.10.0.0/24 from the Origin AS 12345.
//void validate_prefix(){
//  ip_addr prefix;
//  ip_str_to_addr("10.10.0.0", &prefix);
//  pfxv_state result;
//  rtr_mgr_validate(&conf, 12345, &prefix, 24, &result);
//}
