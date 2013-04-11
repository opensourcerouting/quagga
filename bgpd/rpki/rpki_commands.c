/*
 * rpki_commands.c
 *
 *  Created on: 19.03.2013
 *      Author: Michael Mester
 */

#include <zebra.h>
#include <pthread.h>
#include "log.h"
#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "routemap.h"

#include "rtrlib/rtrlib.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"
#include "rtrlib/transport/ssh/ssh_transport.h"

#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/rpki/bgp_rpki.h"

/**************************************/
/** Declaration of structs & enums   **/
/**************************************/
enum return_values{
    SUCCESS = 0,
    ERROR = -1
};

typedef struct {
  struct list* cache_config_list;
  int preference_value;
} cache_group;

typedef struct {
  enum {
    TCP,
    SSH
  } type;
  union {
    tr_tcp_config* tcp_config;
    tr_ssh_config* ssh_config;
  } tr_config;
  rtr_socket* rtr_socket;
} cache;

/*******************************************/
/** Declaration of list helper functions  **/
/*******************************************/
void delete_cache(void* node_value){
  cache* cache_p = node_value;
  if(cache_p->type == TCP){
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config->host);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config->port);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config);
  }
  else {
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->host);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->username);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->client_privkey_path);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->client_pubkey_path);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config);
  }
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->rtr_socket->tr_socket);
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->rtr_socket);
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p);
}

struct list* create_cache_list(){
  struct list* cache_list = list_new();
  cache_list->del = delete_cache;
  return cache_list;
}

cache* create_cache(tr_socket* tr_socket){
  rtr_socket* rtr_socket_p;
  cache* cache_p;
  if ((rtr_socket_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (rtr_socket))) == NULL){
      return NULL;
  }
  if ((cache_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (cache))) == NULL){
      return NULL;
  }
  rtr_socket_p->tr_socket = tr_socket;
  cache_p->rtr_socket = rtr_socket_p;
  return cache_p;
}

int add_tcp_cache(struct list* cache_list, char* host, char* port){
  tr_tcp_config* tcp_config_p;
  tr_socket* tr_socket_p;
  cache* cache_p;
  if((tr_socket_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (tr_socket))) == NULL){
      return ERROR;
  }
  if((tcp_config_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (tr_tcp_config))) == NULL){
      return ERROR;
  }
  tcp_config_p->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);
  tcp_config_p->port = XSTRDUP(MTYPE_BGP_RPKI_CACHE, port);
  tr_tcp_init(tcp_config_p, tr_socket_p);
  if((cache_p = create_cache(tr_socket_p)) == NULL){
      return ERROR;
  }
  cache_p->tr_config.tcp_config = tcp_config_p;
  cache_p->type = TCP;
  listnode_add(cache_list, cache_p);
  return SUCCESS;
}

int add_ssh_cache(struct list* cache_list, char* host, int port,
    const char* username, const char* client_privkey_path,
    const char* client_pubkey_path){

  tr_ssh_config* ssh_config_p;
  tr_socket* tr_socket_p;
  cache* cache_p;
  if((tr_socket_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (tr_socket))) == NULL){
      return ERROR;
  }
  if((ssh_config_p = XMALLOC (MTYPE_BGP_RPKI_CACHE, sizeof (tr_ssh_config))) == NULL){
      return ERROR;
  }
  ssh_config_p->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);
  memcpy(&(ssh_config_p->port),&port, sizeof(int));
  ssh_config_p->username = XSTRDUP(MTYPE_BGP_RPKI_CACHE, username);
  ssh_config_p->client_privkey_path = XSTRDUP(MTYPE_BGP_RPKI_CACHE, client_privkey_path);
  ssh_config_p->client_pubkey_path = XSTRDUP(MTYPE_BGP_RPKI_CACHE, client_pubkey_path);
  ssh_config_p->server_hostkey_path = NULL;
  tr_ssh_init(ssh_config_p, tr_socket_p);
  if((cache_p = create_cache(tr_socket_p)) == NULL){
      return ERROR;
  }
  cache_p->tr_config.ssh_config = ssh_config_p;
  cache_p->type = SSH;
  listnode_add(cache_list, cache_p);
  return SUCCESS;
}

void delete_cache_group(void* node_value){
  cache_group* group = node_value;
  list_delete(group->cache_config_list);
  XFREE(MTYPE_BGP_RPKI_CACHE_GROUP, group);
}

cache_group* create_cache_group(int preference_value){
  cache_group* group;
  if((group = XMALLOC (MTYPE_BGP_RPKI_CACHE_GROUP, sizeof (cache_group))) == NULL){
      return NULL;
  }
  group->cache_config_list = create_cache_list();
  group->preference_value = preference_value;
  return group;
}

int get_number_of_cache_groups(){
  return listcount(cache_group_list);
}

rtr_mgr_group* get_rtr_mgr_groups(){
  struct listnode *cache_group_node;
  cache_group* cache_group;
  rtr_mgr_group* rtr_mgr_groups;
  rtr_mgr_group* loop_group_pointer;
  int number_of_groups = listcount(cache_group_list);

  if(number_of_groups == 0){
      return NULL;
  }

  if((rtr_mgr_groups = XMALLOC (MTYPE_BGP_RPKI_CACHE_GROUP,
      number_of_groups * sizeof (rtr_mgr_group))) == NULL){
      return NULL;
  }
  loop_group_pointer = rtr_mgr_groups;

  for (ALL_LIST_ELEMENTS_RO(cache_group_list, cache_group_node, cache_group)) {
      struct list* cache_list = cache_group->cache_config_list;
      struct listnode* cache_node;
      cache* cache;
      rtr_socket** loop_cache_pointer;
      int number_of_caches = listcount(cache_list);
      if (number_of_caches == 0) {
        break;
      }
      if((loop_group_pointer->sockets = XMALLOC (MTYPE_BGP_RPKI_CACHE,
          number_of_caches * sizeof (rtr_socket*))) == NULL){
          return NULL;
      }
      loop_cache_pointer = loop_group_pointer->sockets;

      for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache)) {
          *loop_cache_pointer = cache->rtr_socket;
          loop_cache_pointer++;
      }
      loop_group_pointer->sockets_len = number_of_caches;
      loop_group_pointer->preference = cache_group->preference_value;
      loop_group_pointer++;
  }

  if (loop_group_pointer == rtr_mgr_groups) {
    // No caches were found in config file
    return NULL;
  }
  return rtr_mgr_groups;
}

void free_rtr_mgr_groups(rtr_mgr_group* group, int length){
  rtr_mgr_group* group_temp = group;
  for (int i = 0; i < length; ++i) {
    XFREE(MTYPE_BGP_RPKI_CACHE, group_temp->sockets);
    group_temp++;
  }

  XFREE(MTYPE_BGP_RPKI_CACHE_GROUP, group);
}

void delete_cache_group_list(){
  list_delete(cache_group_list);
}

/**********************************/
/** Declaration of cli commands  **/
/**********************************/

DEFUN (rpki_polling_period,
       rpki_polling_period_cmd,
       "rpki polling_period " CMD_POLLING_PERIOD_RANGE,
       "Enable rpki in this bgp deamon\n"
       "Set polling period\n"
       "Polling period value\n")
{
  RPKI_DEBUG("Setting Polling period to: %s", argv[0])
  VTY_GET_INTEGER_RANGE("polling_period", polling_period, argv[0], 0, 3600);
  return CMD_SUCCESS;
}

DEFUN (rpki_timeout,
       rpki_timeout_cmd,
       "rpki timeout TIMEOUT",
       "Enable rpki in this bgp deamon\n"
       "Set timeout\n"
       "Timeout value\n")
{
  VTY_GET_INTEGER("timeout", timeout, argv[0]);
  RPKI_DEBUG("Setting timeout to: %d", timeout)
  return CMD_SUCCESS;
}

DEFUN (rpki_group,
       rpki_group_cmd,
       "rpki group PREFERENCE",
       "Enable rpki in this bgp deamon\n"
       "Install a group of cache servers\n"
       "Preference Value for this group (lower value means higher preference)\n")
{
  int group_preference;
  cache_group* new_cache_group;
  VTY_GET_INTEGER("group preference", group_preference, argv[0]);
  if((new_cache_group = create_cache_group(group_preference)) == NULL) {
      vty_out (vty, "Could not create new rpki cache group because "
          "of memory allocation error.%s", VTY_NEWLINE);
      return CMD_WARNING;
  }
  listnode_add(cache_group_list, new_cache_group);
  RPKI_DEBUG("Installing new rpki cache group with preference: %d", group_preference)
  return CMD_SUCCESS;
}

DEFUN (rpki_cache,
       rpki_cache_cmd,
       "rpki cache (A.B.C.D|WORD) PORT [SSH_UNAME] [SSH_PRIVKEY] [SSH_PUBKEY]",
       "Enable rpki in this bgp deamon\n"
       "Install a cache server to current group\n"
       "IP address of cache server\n"
       "Tcp port number \n"
       "SSH_UNAME \n"
       "SSH_PRIVKEY \n"
       "SSH_PUBKEY \n")
{
  int return_value = SUCCESS;
  if(list_isempty(cache_group_list)){
    vty_out (vty, "Could not create new rpki cache because "
        "no cache group is defined.%s", VTY_NEWLINE);
    return CMD_WARNING;
  }
  // use ssh connection
  if(argc == 5){
      int port;
      VTY_GET_INTEGER("rpki cache ssh port", port, argv[1]);
      struct listnode* list_tail = listtail(cache_group_list);
      cache_group* current_group = listgetdata(list_tail);
      return_value = add_ssh_cache(
        current_group->cache_config_list,
        argv[0], port, argv[2], argv[3], argv[4]);
  }
  // use tcp connection
  else {
      struct listnode* list_tail = listtail(cache_group_list);
      cache_group* current_group = listgetdata(list_tail);
      return_value = add_tcp_cache(current_group->cache_config_list, argv[0], argv[1]);
  }
  if(return_value == ERROR){
      vty_out (vty, "Could not create new rpki cache because "
          "of memory allocation error.%s", VTY_NEWLINE);
      return CMD_WARNING;
  }
  RPKI_DEBUG("Adding new cache: %s:%s", argv[0], argv[1])
  return CMD_SUCCESS;
}

DEFUN (neighbor_rpki_off,
       neighbor_rpki_off_cmd,
       NEIGHBOR_CMD2 "rpki-off",
       NEIGHBOR_STR
       NEIGHBOR_ADDR_STR2
       "Don't validate prefixes coming from this neighbor\n")
{
//  return peer_flag_set_vty (vty, argv[0], PEER_FLAG_PASSIVE);
  return CMD_SUCCESS;
}

DEFUN (show_bgp_rpki,
       show_bgp_rpki_cmd,
       "show bgp rpki",
       SHOW_STR
       BGP_STR
       "Show RPKI/RTR info\n")
{
  vty_out (vty, "RPKI/RTR Information %s", VTY_NEWLINE);

//  return CMD_WARNING;
  return CMD_SUCCESS;
}



/**************************************************/
/** Declaration of route-map match rpki command  **/
/**************************************************/

static route_map_result_t route_match_rpki (void *rule, struct prefix *prefix,
			route_map_object_t type, void *object) {

  int* rpki_status = rule;
  struct bgp_info* bgp_info;
  struct assegment* as_segment;
  as_t as_number = 0;

  if (type == RMAP_BGP) {
    bgp_info = object;
    /* No aspath means route comes from iBGP*/
    if(!bgp_info->attr->aspath){
      // Set own as number
      as_number = bgp_info->peer->bgp->as;
    }
    else {
      as_segment = bgp_info->attr->aspath->segments;
      /* Find last AsSegment */
      while (as_segment->next){
        as_segment = as_segment->next;
      }
      if(as_segment->type == AS_SEQUENCE){
        // Get rightmost asn
        as_number = as_segment->as[as_segment->length - 1];
      }
      else if (as_segment->type == AS_CONFED_SEQUENCE
            || as_segment->type == AS_CONFED_SET) {
        // Set own as number
        as_number = bgp_info->peer->bgp->as;
      } else {
        // Take distinguished value NONE as asn
        // we just leave as_number as zero
      }
    }
//    switch (prefix->family) {
//      case :
//
//        break;
//      default:
//        break;
//    }
    char* prefix_string = prefix->u.val;
    /* Lookup rpki status of prefix. */
    if(validate_prefix(prefix, as_number, prefix->prefixlen) == *rpki_status){
      return RMAP_MATCH;
    }
  }
  return RMAP_NOMATCH;
}


static void* route_match_rpki_compile (const char *arg) {
  int* rpki_status;

  rpki_status = XMALLOC (MTYPE_ROUTE_MAP_COMPILED, sizeof (u_char));

  if (strcmp (arg, "valid") == 0)
    *rpki_status = RPKI_VALID;
  else if (strcmp (arg, "invalid") == 0)
    *rpki_status = RPKI_INVALID;
  else
    *rpki_status = RPKI_UNKNOWN;

  return rpki_status;
}

static void route_match_rpki_free (void *rule) {
  XFREE (MTYPE_ROUTE_MAP_COMPILED, rule);
}

struct route_map_rule_cmd route_match_rpki_cmd =
{
  "rpki",
  route_match_rpki,
  route_match_rpki_compile,
  route_match_rpki_free
};

void install_cli_commands(){
  cache_group_list = list_new();
  cache_group_list->del = delete_cache_group;
  install_element (CONFIG_NODE, &rpki_polling_period_cmd);
  install_element (CONFIG_NODE, &rpki_timeout_cmd);
  install_element (CONFIG_NODE, &rpki_group_cmd);
  install_element (CONFIG_NODE, &rpki_cache_cmd);
  install_element (BGP_NODE, &neighbor_rpki_off_cmd);
  install_element (VIEW_NODE, &show_bgp_rpki_cmd);

  route_map_install_match (&route_match_rpki_cmd);

}

