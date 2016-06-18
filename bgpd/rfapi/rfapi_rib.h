/* 
 *
 * Copyright 2009-2016, LabN Consulting, L.L.C.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/*
 * File:	rfapi_rib.h
 * Purpose:	per-nve rib
 */

#ifndef QUAGGA_HGP_RFAPI_RIB_H
#define QUAGGA_HGP_RFAPI_RIB_H

/*
 * Key for indexing RIB and Pending RIB skiplists. For L3 RIBs,
 * the VN address is sufficient because it represents the actual next hop.
 *
 * For L2 RIBs, it is possible to have multiple routes to a given L2
 * prefix via a given VN address, but each route having a unique aux_prefix.
 */
struct rfapi_rib_key
{
  struct prefix vn;

  /*
   * for L2 routes: optional IP addr
   * .family == 0 means "none"
   */
  struct prefix aux_prefix;
};

struct rfapi_info
{
  struct rfapi_rib_key		rk;		/* NVE VN addr + aux addr */
  struct prefix			un;
  uint8_t			cost;
  uint32_t			lifetime;
  time_t			last_sent_time;
  uint32_t			rsp_counter;	/* dedup initial responses */
  struct bgp_tea_options	*tea_options;
  struct rfapi_un_option	*un_options;
  struct rfapi_vn_option	*vn_options;
  void				*timer;
};

/*
 * Work item for updated responses queue
 */
struct rfapi_updated_responses_queue
{
  struct rfapi_descriptor	*rfd;
  afi_t				afi;
};


extern void
rfapiRibClear (struct rfapi_descriptor *rfd);

extern void
rfapiRibFree (struct rfapi_descriptor *rfd);

extern void
rfapiRibUpdatePendingNode (
  struct bgp			*bgp,
  struct rfapi_descriptor	*rfd,
  struct rfapi_import_table	*it,
  struct route_node		*it_node,
  uint32_t			lifetime);

extern void
rfapiRibUpdatePendingNodeSubtree (
  struct bgp			*bgp,
  struct rfapi_descriptor	*rfd,
  struct rfapi_import_table	*it,
  struct route_node		*it_node,
  struct route_node		*omit_subtree,
  uint32_t			lifetime);

extern struct rfapi_next_hop_entry *rfapiRibPreload (
  struct bgp			*bgp,
  struct rfapi_descriptor	*rfd,
  struct rfapi_next_hop_entry	*response,
  int				use_eth_resolution);

extern void
rfapiRibPendingDeleteRoute (
  struct bgp			*bgp,
  struct rfapi_import_table	*it,
  afi_t				afi,
  struct route_node		*it_node);

extern void
rfapiRibShowResponsesSummary (void *stream);

extern void
rfapiRibShowResponsesSummaryClear (void);

extern void
rfapiRibShowResponses (
  void		*stream,
  struct prefix	*pfx_match,
  int		show_removed);

extern struct rfapi_next_hop_entry *
rfapiRibFTDFilterRecent (
  struct bgp			*bgp,
  struct rfapi_descriptor	*rfd,
  struct prefix			*pfx_target,  /* target expressed as pfx */
  struct rfapi_next_hop_entry	*response);

extern void
rfapiFreeRfapiUnOptionChain (struct rfapi_un_option *p);

extern void
rfapiFreeRfapiVnOptionChain (struct rfapi_vn_option *p);

extern void
rfapiRibCheckCounts (
  int		checkstats,	/* validate rfd & global counts */
  unsigned int	offset);	/* number of ri's held separately */

/* enable for debugging; disable for performance */
#if 0
#define RFAPI_RIB_CHECK_COUNTS(checkstats, offset)	rfapiRibCheckCounts(checkstats, offset)
#else
#define RFAPI_RIB_CHECK_COUNTS(checkstats, offset)
#endif

#endif /* QUAGGA_HGP_RFAPI_RIB_H */
