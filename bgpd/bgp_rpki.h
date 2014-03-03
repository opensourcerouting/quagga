/* BGP RPKI
 * Copyright (C) 2013 Michael Mester (m.mester@fu-berlin.de)
 *
 * This file is part of Quagga
 *
 * Quagga is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * Quagga is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Quagga; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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
int rpki_debug;
#define RPKI_DEBUG(...) \
  if(rpki_debug){zlog_debug("RPKI: "__VA_ARGS__);}
#define TO_STRING(s) str(s)
#define str(s) #s

/**********************************/
/** Declaration of constants     **/
/**********************************/
#define CMD_POLLING_PERIOD_RANGE "<0-3600>"
#define CMD_TIMEOUT_RANGE "<1-4294967295>"
#define POLLING_PERIOD_DEFAULT 300
#define TIMEOUT_DEFAULT 600
#define INITIAL_SYNCHRONISATION_TIMEOUT_DEFAULT 30
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
unsigned int initial_synchronisation_timeout;

/**********************************/
/** Declaration of functions     **/
/**********************************/
void rpki_start(void);
void rpki_reset_session(void);
void rpki_init(void);
void rpki_finish(void);
int rpki_is_synchronized(void);
int rpki_is_running(void);
void rpki_set_validation_status(struct bgp* bgp, struct bgp_info* bgp_info, struct prefix* prefix);
int rpki_validate_prefix(struct peer* peer, struct attr* attr, struct prefix *prefix);
int rpki_is_route_map_active(void);
void rpki_set_route_map_active(int activate);
void rpki_print_prefix_table(struct vty *vty);
int rpki_get_connected_group(void);
void rpki_revalidate_all_routes(struct bgp* bgp, afi_t afi);
#endif /* BGP_RPKI_H_ */
