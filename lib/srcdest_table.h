/*
 * SRC-DEST Routing Table
 *
 * Copyright (C) 2014 by David Lamparter, Open Source Routing.
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

#ifndef _ZEBRA_SRC_DEST_TABLE_H
#define _ZEBRA_SRC_DEST_TABLE_H

/* old/IPv4/non-srcdest:
 * table -> route_node .info -> [obj]
 *
 * new/IPv6/srcdest:
 * table -...-> srcdest_rnode [prefix = dest] .info -> [obj]
 *                                            .src_table ->
 *         srcdest table -...-> route_node [prefix = src] .info -> [obj]
 *
 * non-srcdest routes (src = ::/0) are treated just like before, their
 * information being directly there in the info pointer.
 *
 * srcdest routes are found by looking up destination first, then looking
 * up the source in the "src_table".  src_table contains normal route_nodes,
 * whose prefix is the _source_ prefix.
 *
 * NB: info can be NULL on the destination rnode, if there are only srcdest
 * routes for a particular destination prefix.
 */

struct route_node;
struct route_table;
struct prefix;
struct prefix_ipv6;

/* extended route node for IPv6 srcdest routing */
struct srcdest_rnode;

extern int rnode_is_dstnode (struct route_node *rn);
extern int rnode_is_srcnode (struct route_node *rn);

extern struct route_table *srcdest_table_init(void);
extern struct route_node *srcdest_rnode_get(struct route_table *table,
                                            struct prefix_ipv6 *dst_p,
                                            struct prefix_ipv6 *src_p);
extern struct route_node *srcdest_rnode_lookup(struct route_table *table,
                                            struct prefix_ipv6 *dst_p,
                                            struct prefix_ipv6 *src_p);
extern void srcdest_rnode_prefixes (struct route_node *rn, struct prefix **p,
                                    struct prefix **src_p);
extern struct route_node *srcdest_route_next(struct route_node *rn);

static inline struct srcdest_rnode *
srcdest_rnode_from_rnode (struct route_node *rn)
{
  assert (rnode_is_dstnode (rn));
  return (struct srcdest_rnode *) rn;
}

static inline struct route_node *
srcdest_rnode_to_rnode (struct srcdest_rnode *srn)
{
  return (struct route_node *) srn;
}

#endif /* _ZEBRA_SRC_DEST_TABLE_H */
