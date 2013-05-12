/*
 * bgp_rpki.h
 *
 *  Created on: 15.02.2013
 *      Author: Michael Mester
 */

#ifndef BGP_RPKI_H_
#define BGP_RPKI_H_

#include "prefix.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgpd.h"
#include "bgpd/bgp_route.h"

/**********************************/
/** Declaration of debug makros  **/
/**********************************/
#define debug 1
#define RPKI_DEBUG(...) \
  if(debug){zlog_debug("RPKI: "__VA_ARGS__);}
#define TO_STRING(s) str(s)
#define str(s) #s

/**********************************/
/** Declaration of constants     **/
/**********************************/
#define CMD_POLLING_PERIOD_RANGE "<0-3600>"
#define CMD_TIMEOUT_RANGE "<1-4294967295>"
#define POLLING_PERIOD_DEFAULT 300
#define TIMEOUT_DEFAULT 650
#define CALLBACK_TIMEOUT 10
#define RPKI_VALID      1
#define RPKI_NOTFOUND   2
#define RPKI_INVALID    3

/**********************************/
/** Declaration of variables     **/
/**********************************/
struct list* cache_group_list;
unsigned int polling_period;
unsigned int timeout;
/**********************************/
/** Declaration of structs       **/
/**********************************/
typedef struct data_elem_t{
    uint32_t asn;
    uint8_t max_len;
    uintptr_t socket_id;
} data_elem;

typedef struct node_data_t{
    unsigned int len;
    data_elem* ary;
} node_data;

/**********************************/
/** Declaration of functions     **/
/**********************************/
void rpki_start(void);
void rpki_reset_session(void);
void rpki_test(void);
void rpki_init(void);
void rpki_finish(void);
int rpki_is_synchronized(void);
void do_rpki_origin_validation(struct bgp* bgp, struct bgp_info* bgp_info, struct prefix* prefix);
int rpki_validate_prefix(struct peer* peer, struct attr* attr, struct prefix *prefix);

void print_prefix_table(struct vty *vty);
int get_connected_group();
#endif /* BGP_RPKI_H_ */
