/*
 * Zebra next hop tracking header
 * Copyright (C) 2013 Cumulus Networks, Inc.
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _ZEBRA_RNH_H
#define _ZEBRA_RNH_H

#include "prefix.h"
#include "vty.h"

/* Nexthop structure. */
struct rnh
{
  u_char flags;
#define ZEBRA_NHT_CONNECTED  	0x1
  struct rib *state;
  struct list *client_list;
  struct route_node *node;
};

extern struct rnh *zebra_add_rnh(struct prefix *p, vrf_id_t vrfid);
extern struct rnh *zebra_lookup_rnh(struct prefix *p, vrf_id_t vrfid);
extern void zebra_delete_rnh(struct rnh *rnh);
extern void zebra_add_rnh_client(struct rnh *rnh, struct zserv *client, vrf_id_t vrf_id_t);
extern void zebra_remove_rnh_client(struct rnh *rnh, struct zserv *client);
extern int zebra_evaluate_rnh_table(vrf_id_t vrfid, int family);
extern int zebra_dispatch_rnh_table(vrf_id_t vrfid, int family, struct zserv *cl);
extern void zebra_print_rnh_table(vrf_id_t vrfid, int family, struct vty *vty);
extern char *rnh_str(struct rnh *rnh, char *buf, int size);
extern int zebra_cleanup_rnh_client(vrf_id_t vrf, int family, struct zserv *client);
#endif /*_ZEBRA_RNH_H */
