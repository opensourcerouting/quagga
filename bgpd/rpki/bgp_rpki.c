/*
 * bgp_rpki.c
 *
 *  Created on: 15.02.2013
 *      Author: Michael Mester
 */

#include <zebra.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include "prefix.h"
#include "log.h"
#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "thread.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/rpki/bgp_rpki.h"
#include "bgpd/rpki/rpki_commands.h"
#include "rtrlib/rtrlib.h"
#include "rtrlib/rtr_mgr.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"
#include "rtrlib/transport/ssh/ssh_transport.h"

rtr_mgr_config rtr_config;
int rtr_is_running;

extern void bgp_process(struct bgp *bgp, struct bgp_node *rn, afi_t afi, safi_t safi);

static void list_all_nodes(struct vty *vty, const lpfst_node* node, unsigned int* count);
static void print_record(struct vty *vty, const lpfst_node* node);
static int validate_prefix(struct prefix *prefix, uint32_t asn, uint8_t mask_len);
static void update_cb(struct pfx_table* p, const pfx_record rec, const bool added);

void rpki_init(void){
  install_cli_commands();
  rtr_is_running = 0;
  polling_period = POLLING_PERIOD_DEFAULT;
  timeout = TIMEOUT_DEFAULT;
}

inline int rpki_is_synchronized(void){
  return rtr_is_running &&
      rtr_mgr_conf_in_sync(&rtr_config);
}

inline int rpki_is_running(void){
  return rtr_is_running;
}

void rpki_reset_session(void){
  RPKI_DEBUG("Resetting RPKI Session");
  if(rtr_is_running){
    rtr_mgr_stop(&rtr_config);
    rtr_mgr_free(&rtr_config);
    rtr_is_running = 0;
  }
  rpki_start();
}

void rpki_start(){
  rtr_config.len = get_number_of_cache_groups();
  rtr_config.groups = get_rtr_mgr_groups();
  if(rtr_config.len == 0 || rtr_config.groups == NULL){
      RPKI_DEBUG("No caches were found in config. Prefix validation is off.");
      return;
  }
  rtr_mgr_init(&rtr_config, polling_period, timeout, &update_cb);
  rtr_mgr_start(&rtr_config);
  rtr_is_running = 1;
//  RPKI_DEBUG("Waiting for rtr connection to synchronize.");
//  while(!rtr_mgr_conf_in_sync(&rtr_config)){
//      RPKI_DEBUG("Still waiting.");
//      sleep(1);
//  }
//  RPKI_DEBUG("Got it!");
}

void rpki_finish(void){
  RPKI_DEBUG("Stopping");
  rtr_mgr_stop(&rtr_config);
  rtr_mgr_free(&rtr_config);
  rtr_is_running = 0;
  free_rtr_mgr_groups(rtr_config.groups, rtr_config.len);
  delete_cache_group_list();
}

int rpki_validate_prefix(struct peer* peer, struct attr* attr, struct prefix *prefix){
  struct assegment* as_segment;
  as_t as_number = 0;

  if(!rpki_is_synchronized()
      || bgp_flag_check(peer->bgp, BGP_FLAG_VALIDATE_DISABLE)){
    return 0;
  }

  // No aspath means route comes from iBGP
  if (!attr->aspath || !attr->aspath->segments) {
    // Set own as number
    as_number = peer->bgp->as;
  }
  else {
    as_segment = attr->aspath->segments;
    // Find last AsSegment
    while (as_segment->next) {
      as_segment = as_segment->next;
    }
    if (as_segment->type == AS_SEQUENCE) {
      // Get rightmost asn
      as_number = as_segment->as[as_segment->length - 1];
    } else if (as_segment->type == AS_CONFED_SEQUENCE
        || as_segment->type == AS_CONFED_SET) {
      // Set own as number
      as_number = peer->bgp->as;
    } else {
      // RFC says: "Take distinguished value NONE as asn"
      // we just leave as_number as zero
    }
  }

  return validate_prefix(prefix, as_number, prefix->prefixlen);
}

static int validate_prefix(struct prefix *prefix, uint32_t asn, uint8_t mask_len) {
  ip_addr ip_addr_prefix;
  pfxv_state result;
  char buf[BUFSIZ];
  char result_buf[10];
  u_char* quagga_prefix = &(prefix->u.prefix);
  u_char* rtr_prefix;
  const char* prefix_string = inet_ntop (prefix->family, &prefix->u.prefix, buf, BUFSIZ);

  switch (prefix->family) {
    case AF_INET:
      ip_addr_prefix.ver = IPV4;
      rtr_prefix = (u_char*) &(ip_addr_prefix.u.addr4.addr);
      rtr_prefix[0] = quagga_prefix[3];
      rtr_prefix[1] = quagga_prefix[2];
      rtr_prefix[2] = quagga_prefix[1];
      rtr_prefix[3] = quagga_prefix[0];
      break;

#ifdef HAVE_IPV6
    case AF_INET6:
      ip_addr_prefix.ver = IPV6;
      rtr_prefix = (u_char*) &(ip_addr_prefix.u.addr6.addr);

      rtr_prefix[0] = quagga_prefix[3];
      rtr_prefix[1] = quagga_prefix[2];
      rtr_prefix[2] = quagga_prefix[1];
      rtr_prefix[3] = quagga_prefix[0];

      rtr_prefix[4] = quagga_prefix[7];
      rtr_prefix[5] = quagga_prefix[6];
      rtr_prefix[6] = quagga_prefix[5];
      rtr_prefix[7] = quagga_prefix[4];

      rtr_prefix[8] = quagga_prefix[11];
      rtr_prefix[9] = quagga_prefix[10];
      rtr_prefix[10] = quagga_prefix[9];
      rtr_prefix[11] = quagga_prefix[8];

      rtr_prefix[12] = quagga_prefix[15];
      rtr_prefix[13] = quagga_prefix[14];
      rtr_prefix[14] = quagga_prefix[13];
      rtr_prefix[15] = quagga_prefix[12];
      break;
#endif /* HAVE_IPV6 */

    default:
      return 0;
  }
  rtr_mgr_validate(&rtr_config, asn, &ip_addr_prefix, mask_len, &result);
  RPKI_DEBUG("Validating Prefix %s from asn %u", prefix_string , asn);
  switch (result) {
    case BGP_PFXV_STATE_VALID:
      RPKI_DEBUG("    Result: VALID");
      return RPKI_VALID;
    case BGP_PFXV_STATE_NOT_FOUND:
      RPKI_DEBUG("    Result: NOTFOUND");
      return RPKI_NOTFOUND;
    case BGP_PFXV_STATE_INVALID:
      RPKI_DEBUG("    Result: INVALID");
      return RPKI_INVALID;
    default:
      RPKI_DEBUG("    Result: CANNOT VALIDATE");
      break;
  }
  return 0;
}

int get_connected_group(){
  for(unsigned int i = 0; i < rtr_config.len; i++){
    if(rtr_config.groups[i].status == RTR_MGR_ESTABLISHED
        || rtr_config.groups[i].status == RTR_MGR_CONNECTING){
      return rtr_config.groups[i].preference;
    }
  }
  return -1;
}

void print_prefix_table(struct vty *vty){
  unsigned int number_of_ipv4_prefixes = 0;
  unsigned int number_of_ipv6_prefixes = 0;
  struct pfx_table* pfx_table = rtr_config.groups[0].sockets[0]->pfx_table;
  vty_out(vty, "RPKI/RTR prefix table%s", VTY_NEWLINE);
  vty_out(vty, "%-40s %s  %s %s", "Prefix", "Prefix Length", "Origin-AS", VTY_NEWLINE);
  if(pfx_table->ipv4 != NULL){
    list_all_nodes(vty, pfx_table->ipv4, &number_of_ipv4_prefixes);
  }
  if(pfx_table->ipv6 != NULL){
    list_all_nodes(vty, pfx_table->ipv6, &number_of_ipv6_prefixes);
  }
  vty_out(vty, "Number of IPv4 Prefixes: %u %s", number_of_ipv4_prefixes, VTY_NEWLINE);
  vty_out(vty, "Number of IPv6 Prefixes: %u %s", number_of_ipv6_prefixes, VTY_NEWLINE);
}

void list_all_nodes(struct vty *vty, const lpfst_node* node, unsigned int* count){
  *count += 1;

  if(node->lchild != NULL){
    list_all_nodes(vty, node->lchild, count);
  }

  print_record(vty, node);

  if(node->rchild != NULL){
    list_all_nodes(vty, node->rchild, count);
  }
}

void print_record(struct vty *vty, const lpfst_node* node){
  char ip[INET6_ADDRSTRLEN];
  node_data* data = (node_data*) node->data;
  for(unsigned int i = 0; i < data->len; ++i){
    ip_addr_to_str(&(node->prefix), ip, sizeof(ip));
//    rtr_socket* rtr_socket = (rtr_socket*) data->ary[i].socket_id;
    vty_out(vty, "%-40s   %3u - %3u   %10u %s", ip, node->len,
        data->ary[i].max_len, data->ary[i].asn, VTY_NEWLINE);
  }
}

void do_rpki_origin_validation(struct bgp* bgp, struct bgp_info* bgp_info, struct prefix* prefix) {
  int validate_disable = bgp_flag_check(bgp, BGP_FLAG_VALIDATE_DISABLE);

  for (; bgp_info; bgp_info = bgp_info->next) {
    if(validate_disable){
      bgp_info->rpki_validation_status = 0;
    }
    else {
      bgp_info->rpki_validation_status = rpki_validate_prefix(bgp_info->peer, bgp_info->attr, prefix);
    }
  }

}

static void update_cb(struct pfx_table* p __attribute__ ((unused)),
    const pfx_record rec __attribute__ ((unused)), const bool added __attribute__ ((unused))){
  struct bgp* bgp;
  struct bgp_table *table;
  struct bgp_info* bgp_info;
  struct listnode* node;
  struct bgp_node* bgp_node;
  safi_t safi;

  if(!rpki_is_synchronized()){
    return;
  }

  for (ALL_LIST_ELEMENTS_RO (bm->bgp, node, bgp)) {
    if(bgp_flag_check(bgp, BGP_FLAG_VALIDATE_DISABLE)){
      continue;
    }
    for (safi = SAFI_UNICAST; safi < SAFI_MAX; safi++) {
      switch (rec.prefix.ver) {
        case IPV4:
          table = bgp->rib[AFI_IP][SAFI_UNICAST];
          for (bgp_node = bgp_table_top(table); bgp_node; bgp_node = bgp_route_next(bgp_node)){
            if (bgp_node->info != NULL ) {
              for (bgp_info = bgp_node->info; bgp_info; bgp_info = bgp_info->next) {
                bgp_info->rpki_validation_status = rpki_validate_prefix(bgp_info->peer, bgp_info->attr, &bgp_node->p);
                bgp_process(bgp, bgp_node, AFI_IP, safi);
              }
            }
          }
          break;
        case IPV6:
          table = bgp->rib[AFI_IP6][SAFI_UNICAST];
          for (bgp_node = bgp_table_top(table); bgp_node; bgp_node = bgp_route_next(bgp_node)){
            if (bgp_node->info != NULL ) {
              for (bgp_info = bgp_node->info; bgp_info; bgp_info = bgp_info->next) {
                bgp_info->rpki_validation_status = rpki_validate_prefix(bgp_info->peer, bgp_info->attr, &bgp_node->p);
                bgp_process(bgp, bgp_node, AFI_IP6, safi);
              }
            }
          }
          break;
        default:
          break;
      }
    }
  }
}

void rpki_test(void){
  char buf[BUFSIZ];
  struct bgp_info *ri;
  struct bgp_node *rn;
  struct bgp *bgp = bgp_get_default();
  struct bgp_table *table = bgp->rib[AFI_IP][SAFI_UNICAST];
  RPKI_DEBUG("Testoutput");
  for (rn = bgp_table_top(table); rn; rn = bgp_route_next(rn)){
    RPKI_DEBUG("bgp_node: %s",
        inet_ntop(rn->p.family,
            &rn->p.u.prefix,
            buf, BUFSIZ));
    if (rn->info != NULL ) {
      for (ri = rn->info; ri; ri = ri->next) {
        do_rpki_origin_validation(bgp, ri, &rn->p);
        RPKI_DEBUG("bgp_info: weight: %7u; rpki: %u",
            (ri->attr->extra ? ri->attr->extra->weight : 0), ri->rpki_validation_status);
      }
    }
  }
}
