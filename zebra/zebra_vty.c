/* Zebra VTY functions
 * Copyright (C) 2002 Kunihiro Ishiguro
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
 * along with GNU Zebra; see the file COPYING.  If not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330, 
 * Boston, MA 02111-1307, USA.  
 */

#include <zebra.h>

#include "memory.h"
#include "if.h"
#include "prefix.h"
#include "command.h"
#include "table.h"
#include "rib.h"
#include "vrf.h"
#include "nexthop.h"

#include "zebra/zserv.h"
#include "zebra/zebra_rnh.h"

static int do_show_ip_route(struct vty *vty, safi_t safi, vrf_id_t vrf_id);
static void vty_show_ip_route_detail (struct vty *vty, struct route_node *rn,
                                      int mcast);
static void vty_show_ip_route (struct vty *vty, struct route_node *rn,
                               struct rib *rib);

/* General function for static route. */
static int
zebra_static_ipv4_safi (struct vty *vty, safi_t safi, int add_cmd,
			const char *dest_str, const char *mask_str,
			const char *gate_str, const char *flag_str,
			const char *tag_str, const char *distance_str,
			const char *vrf_id_str)
{
  int ret;
  u_char distance;
  struct prefix p;
  struct in_addr gate;
  struct in_addr mask;
  const char *ifname;
  u_char flag = 0;
  route_tag_t tag = 0;
  vrf_id_t vrf_id = VRF_DEFAULT;
  
  ret = str2prefix (dest_str, &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Cisco like mask notation. */
  if (mask_str)
    {
      ret = inet_aton (mask_str, &mask);
      if (ret == 0)
        {
          vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
          return CMD_WARNING;
        }
      p.prefixlen = ip_masklen (mask);
    }

  /* Apply mask for given prefix. */
  apply_mask (&p);

  /* Administrative distance. */
  if (distance_str)
    distance = atoi (distance_str);
  else
    distance = ZEBRA_STATIC_DISTANCE_DEFAULT;

  /* tag */
  if (tag_str)
    tag = atoi (tag_str);

  /* VRF id */
  if (vrf_id_str)
    VTY_GET_INTEGER ("VRF ID", vrf_id, vrf_id_str);

  /* tag */
  if (tag_str)
    tag = atoi(tag_str);

  /* Null0 static route.  */
  if ((gate_str != NULL) && (strncasecmp (gate_str, "Null0", strlen (gate_str)) == 0))
    {
      if (flag_str)
        {
          vty_out (vty, "%% can not have flag %s with Null0%s", flag_str, VTY_NEWLINE);
          return CMD_WARNING;
        }
      if (add_cmd)
        static_add_ipv4_safi (safi, &p, NULL, NULL, ZEBRA_FLAG_BLACKHOLE, tag, distance, vrf_id);
      else
        static_delete_ipv4_safi (safi, &p, NULL, NULL, tag, distance, vrf_id);
      return CMD_SUCCESS;
    }

  /* Route flags */
  if (flag_str) {
    switch(flag_str[0]) {
      case 'r':
      case 'R': /* XXX */
        SET_FLAG (flag, ZEBRA_FLAG_REJECT);
        break;
      case 'b':
      case 'B': /* XXX */
        SET_FLAG (flag, ZEBRA_FLAG_BLACKHOLE);
        break;
      default:
        vty_out (vty, "%% Malformed flag %s %s", flag_str, VTY_NEWLINE);
        return CMD_WARNING;
    }
  }

  if (gate_str == NULL)
  {
    if (add_cmd)
      static_add_ipv4_safi (safi, &p, NULL, NULL, flag, tag, distance, vrf_id);
    else
      static_delete_ipv4_safi (safi, &p, NULL, NULL, tag, distance, vrf_id);

    return CMD_SUCCESS;
  }
  
  /* When gateway is A.B.C.D format, gate is treated as nexthop
     address other case gate is treated as interface name. */
  ret = inet_aton (gate_str, &gate);
  if (ret)
    ifname = NULL;
  else
    ifname = gate_str;

  if (add_cmd)
    static_add_ipv4_safi (safi, &p, ifname ? NULL : &gate, ifname, flag, tag, distance, vrf_id);
  else
    static_delete_ipv4_safi (safi, &p, ifname ? NULL : &gate, ifname, tag, distance, vrf_id);

  return CMD_SUCCESS;
}

/* Static unicast routes for multicast RPF lookup. */
DEFUN (ip_mroute_dist,
       ip_mroute_dist_cmd,
       "ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) <1-255>",
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       "Distance\n")
{
  VTY_WARN_EXPERIMENTAL();
  return zebra_static_ipv4_safi(vty, SAFI_MULTICAST, 1, argv[0], NULL, argv[1],
                                NULL, NULL, argc > 2 ? argv[2] : NULL, NULL);
}

ALIAS (ip_mroute_dist,
       ip_mroute_cmd,
       "ip mroute A.B.C.D/M (A.B.C.D|INTERFACE)",
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n")

DEFUN (ip_mroute_dist_vrf,
       ip_mroute_dist_vrf_cmd,
       "ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) <1-255> " VRF_CMD_STR,
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       "Distance\n"
       VRF_CMD_HELP_STR)
{
  VTY_WARN_EXPERIMENTAL();
  return zebra_static_ipv4_safi(vty, SAFI_MULTICAST, 1, argv[0], NULL, argv[1],
                                NULL, NULL, argc > 3 ? argv[2] : NULL,
                                argc > 3 ? argv[3] : argv[2]);
}

ALIAS (ip_mroute_dist_vrf,
       ip_mroute_vrf_cmd,
       "ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) "VRF_CMD_STR,
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       VRF_CMD_HELP_STR)

DEFUN (no_ip_mroute_dist,
       no_ip_mroute_dist_cmd,
       "no ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) <1-255>",
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       "Distance\n")
{
  VTY_WARN_EXPERIMENTAL();
  return zebra_static_ipv4_safi(vty, SAFI_MULTICAST, 0, argv[0], NULL, argv[1],
                                NULL, NULL, argc > 2 ? argv[2] : NULL, NULL);
}

ALIAS (no_ip_mroute_dist,
       no_ip_mroute_cmd,
       "no ip mroute A.B.C.D/M (A.B.C.D|INTERFACE)",
       NO_STR
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n")

DEFUN (no_ip_mroute_dist_vrf,
       no_ip_mroute_dist_vrf_cmd,
       "no ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) <1-255> " VRF_CMD_STR,
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       "Distance\n"
       VRF_CMD_HELP_STR)
{
  VTY_WARN_EXPERIMENTAL();
  return zebra_static_ipv4_safi(vty, SAFI_MULTICAST, 0, argv[0], NULL, argv[1],
                                NULL, NULL, argc > 3 ? argv[2] : NULL,
                                argc > 3 ? argv[3] : argv[2]);
}

ALIAS (no_ip_mroute_dist_vrf,
       no_ip_mroute_vrf_cmd,
       "no ip mroute A.B.C.D/M (A.B.C.D|INTERFACE) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Configure static unicast route into MRIB for multicast RPF lookup\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Nexthop address\n"
       "Nexthop interface name\n"
       VRF_CMD_HELP_STR)

DEFUN (ip_multicast_mode,
       ip_multicast_mode_cmd,
       "ip multicast rpf-lookup-mode (urib-only|mrib-only|mrib-then-urib|lower-distance|longer-prefix)",
       IP_STR
       "Multicast options\n"
       "RPF lookup behavior\n"
       "Lookup in unicast RIB only\n"
       "Lookup in multicast RIB only\n"
       "Try multicast RIB first, fall back to unicast RIB\n"
       "Lookup both, use entry with lower distance\n"
       "Lookup both, use entry with longer prefix\n")
{
  VTY_WARN_EXPERIMENTAL();

  if (!strncmp (argv[0], "u", 1))
    multicast_mode_ipv4_set (MCAST_URIB_ONLY);
  else if (!strncmp (argv[0], "mrib-o", 6))
    multicast_mode_ipv4_set (MCAST_MRIB_ONLY);
  else if (!strncmp (argv[0], "mrib-t", 6))
    multicast_mode_ipv4_set (MCAST_MIX_MRIB_FIRST);
  else if (!strncmp (argv[0], "low", 3))
    multicast_mode_ipv4_set (MCAST_MIX_DISTANCE);
  else if (!strncmp (argv[0], "lon", 3))
    multicast_mode_ipv4_set (MCAST_MIX_PFXLEN);
  else
    {
      vty_out (vty, "Invalid mode specified%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

DEFUN (no_ip_multicast_mode,
       no_ip_multicast_mode_cmd,
       "no ip multicast rpf-lookup-mode (urib-only|mrib-only|mrib-then-urib|lower-distance|longer-prefix)",
       NO_STR
       IP_STR
       "Multicast options\n"
       "RPF lookup behavior\n"
       "Lookup in unicast RIB only\n"
       "Lookup in multicast RIB only\n"
       "Try multicast RIB first, fall back to unicast RIB\n"
       "Lookup both, use entry with lower distance\n"
       "Lookup both, use entry with longer prefix\n")
{
  multicast_mode_ipv4_set (MCAST_NO_CONFIG);
  return CMD_SUCCESS;
}

ALIAS (no_ip_multicast_mode,
       no_ip_multicast_mode_noarg_cmd,
       "no ip multicast rpf-lookup-mode",
       NO_STR
       IP_STR
       "Multicast options\n"
       "RPF lookup behavior\n")

DEFUN (show_ip_rpf,
       show_ip_rpf_cmd,
       "show ip rpf",
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n")
{
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  VTY_WARN_EXPERIMENTAL();
  return do_show_ip_route(vty, SAFI_MULTICAST, vrf_id);
}

ALIAS (show_ip_rpf,
       show_ip_rpf_vrf_cmd,
       "show ip rpf " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_rpf_addr,
       show_ip_rpf_addr_cmd,
       "show ip rpf A.B.C.D",
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n"
       "IP multicast source address (e.g. 10.0.0.0)\n")
{
  struct in_addr addr;
  struct route_node *rn;
  struct rib *rib;
  vrf_id_t vrf_id = VRF_DEFAULT;
  int ret;

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  VTY_WARN_EXPERIMENTAL();

  ret = inet_aton (argv[0], &addr);
  if (ret == 0)
    {
      vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  rib = rib_match_ipv4_multicast (addr, &rn, vrf_id);

  if (rib)
    vty_show_ip_route_detail (vty, rn, 1);
  else
    vty_out (vty, "%% No match for RPF lookup%s", VTY_NEWLINE);

  return CMD_SUCCESS;
}

ALIAS (show_ip_rpf_addr,
       show_ip_rpf_addr_vrf_cmd,
       "show ip rpf A.B.C.D " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n"
       "IP multicast source address (e.g. 10.0.0.0)\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_rpf_vrf_all,
       show_ip_rpf_vrf_all_cmd,
       "show ip rpf " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct zebra_vrf *zvrf;
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  vrf_iter_t iter;
  int first = 1;

  VTY_WARN_EXPERIMENTAL();

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_MULTICAST]) == NULL)
        continue;

      /* Show all IPv4 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          {
            if (first)
              {
                vty_out (vty, SHOW_ROUTE_V4_HEADER);
                first = 0;
              }
            vty_show_ip_route (vty, rn, rib);
          }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_rpf_addr_vrf_all,
       show_ip_rpf_addr_vrf_all_cmd,
       "show ip rpf A.B.C.D " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "Display RPF information for multicast source\n"
       "IP multicast source address (e.g. 10.0.0.0)\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct in_addr addr;
  struct route_node *rn;
  vrf_iter_t iter;
  int ret;

  VTY_WARN_EXPERIMENTAL();

  ret = inet_aton (argv[0], &addr);
  if (ret == 0)
    {
      vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if (rib_match_ipv4_multicast (addr, &rn, vrf_iter2id (iter)))
        vty_show_ip_route_detail (vty, rn, 1);
    }

  return CMD_SUCCESS;
}

/* Static route configuration.  */
DEFUN (ip_route, 
       ip_route_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 NULL, NULL, NULL, NULL);
}

DEFUN (ip_route_tag,
       ip_route_tag_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, argv[2], NULL, NULL);
}

DEFUN (ip_route_tag_vrf,
       ip_route_tag_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, argv[2], NULL, argv[3]);
}

DEFUN (ip_route_flags,
       ip_route_flags_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], NULL, NULL, NULL);
}

DEFUN (ip_route_flags_tag,
       ip_route_flags_tag_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")

{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], argv[3], NULL, NULL);
}

DEFUN (ip_route_flags_tag_vrf,
       ip_route_flags_tag_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], argv[3], NULL, argv[4]);
}

DEFUN (ip_route_flags2,
       ip_route_flags2_cmd,
       "ip route A.B.C.D/M (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], NULL, NULL, NULL);
}

DEFUN (ip_route_flags2_tag,
       ip_route_flags2_tag_cmd,
       "ip route A.B.C.D/M (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, NULL,
				 argv[1], argv[2], NULL, NULL);
}

DEFUN (ip_route_flags2_tag_vrf,
       ip_route_flags2_tag_vrf_cmd,
       "ip route A.B.C.D/M (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, NULL,
				 argv[1], argv[2], NULL, argv[3]);
}

/* Mask as A.B.C.D format.  */
DEFUN (ip_route_mask,
       ip_route_mask_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], NULL, NULL, NULL, NULL);
}

DEFUN (ip_route_mask_tag,
       ip_route_mask_tag_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n")

{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 NULL, argv[3], NULL, NULL);
}

DEFUN (ip_route_mask_tag_vrf,
       ip_route_mask_tag_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 NULL, argv[3], NULL, argv[4]);
}

DEFUN (ip_route_mask_flags,
       ip_route_mask_flags_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], argv[3], NULL, NULL, NULL);
}

DEFUN (ip_route_mask_flags_tag,
       ip_route_mask_flags_tag_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], argv[3], argv[4], NULL, NULL);
}

DEFUN (ip_route_mask_flags_tag_vrf,
       ip_route_mask_flags_tag_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], argv[3], argv[4], NULL, argv[5]);
}

DEFUN (ip_route_mask_flags2,
       ip_route_mask_flags2_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 NULL, argv[2], NULL, NULL, NULL);
}

DEFUN (ip_route_mask_flags2_tag,
       ip_route_mask_flags2_tag_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], NULL,
				 argv[2], argv[3], NULL, NULL);
}

DEFUN (ip_route_mask_flags2_tag_vrf,
       ip_route_mask_flags2_tag_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], NULL,
				 argv[2], argv[3], NULL, argv[4]);
}

/* Distance option value.  */
DEFUN (ip_route_distance,
       ip_route_distance_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, NULL, argv[2], NULL);
}

DEFUN (ip_route_tag_distance,
       ip_route_tag_distance_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, argv[2], argv[3], NULL);
}

DEFUN (ip_route_tag_distance_vrf,
       ip_route_tag_distance_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, argv[2], argv[3], argv[4]);
}

DEFUN (ip_route_flags_distance,
       ip_route_flags_distance_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], NULL, argv[3], NULL);
}

DEFUN (ip_route_flags_tag_distance,
       ip_route_flags_tag_distance_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], argv[3], argv[4], NULL);
}

DEFUN (ip_route_flags_tag_distance_vrf,
       ip_route_flags_tag_distance_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL, argv[1],
				 argv[2], argv[3], argv[4], argv[5]);
}

DEFUN (ip_route_flags_distance2,
       ip_route_flags_distance2_cmd,
       "ip route A.B.C.D/M (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], NULL, argv[2], NULL);
}

DEFUN (ip_route_flags_tag_distance2,
       ip_route_flags_tag_distance2_cmd,
       "ip route A.B.C.D/M (reject|blackhole) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], argv[2], argv[3], NULL);
}

DEFUN (ip_route_flags_tag_distance2_vrf,
       ip_route_flags_tag_distance2_vrf_cmd,
       "ip route A.B.C.D/M (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], argv[2], argv[3], argv[4]);
}

DEFUN (ip_route_mask_distance,
       ip_route_mask_distance_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 NULL, NULL, argv[3], NULL);
}

DEFUN (ip_route_mask_tag_distance,
       ip_route_mask_tag_distance_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], NULL, argv[3], argv[4], NULL);
}

DEFUN (ip_route_mask_tag_distance_vrf,
       ip_route_mask_tag_distance_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], NULL, argv[3], argv[4], argv[5]);
}


DEFUN (ip_route_mask_flags_tag_distance,
       ip_route_mask_flags_tag_distance_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole)  tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 argv[3], argv[4], argv[5], NULL);
}

DEFUN (ip_route_mask_flags_tag_distance_vrf,
       ip_route_mask_flags_tag_distance_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole)  tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 argv[3], argv[4], argv[5], argv[6]);
}

DEFUN (ip_route_mask_flags_distance,
       ip_route_mask_flags_distance_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], argv[2],
				 argv[3], NULL, argv[4], NULL);
}

DEFUN (ip_route_mask_flags_distance2,
       ip_route_mask_flags_distance2_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 NULL, argv[2], NULL, argv[3], NULL);
}

DEFUN (ip_route_mask_flags_tag_distance2,
       ip_route_mask_flags_tag_distance2_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], NULL,
				 argv[2], argv[3], argv[4], NULL);
}

DEFUN (ip_route_mask_flags_tag_distance2_vrf,
       ip_route_mask_flags_tag_distance2_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1], NULL,
				 argv[2], argv[3], argv[4], argv[5]);
}

DEFUN (no_ip_route, 
       no_ip_route_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL,
				 argv[1], NULL, NULL, NULL, NULL);
}

DEFUN (no_ip_route_tag,
       no_ip_route_tag_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 NULL, argv[2], NULL, NULL);
}

DEFUN (no_ip_route_tag_vrf,
       no_ip_route_tag_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 NULL, argv[2], NULL, argv[3]);
}

ALIAS (no_ip_route,
       no_ip_route_flags_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")

ALIAS (no_ip_route_tag,
       no_ip_route_flags_tag_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n")

DEFUN (no_ip_route_flags2,
       no_ip_route_flags2_cmd,
       "no ip route A.B.C.D/M (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL,
				 NULL, NULL, NULL, NULL, NULL);
}

DEFUN (no_ip_route_flags2_tag,
       no_ip_route_flags2_tag_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 NULL, argv[1], NULL, NULL);
}

DEFUN (no_ip_route_flags2_tag_vrf,
       no_ip_route_flags2_tag_vrf_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 NULL, argv[1], NULL, argv[2]);
}

DEFUN (no_ip_route_mask,
       no_ip_route_mask_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, NULL, NULL, NULL);
}

DEFUN (no_ip_route_mask_tag,
       no_ip_route_mask_tag_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], argv[2],
				 NULL, argv[3], NULL, NULL);
}

ALIAS (no_ip_route_mask,
       no_ip_route_mask_flags_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")

ALIAS (no_ip_route_mask_tag,
       no_ip_route_mask_flags_tag_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n")

DEFUN (no_ip_route_mask_flags2,
       no_ip_route_mask_flags2_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 NULL, NULL, NULL, NULL, NULL);
}

DEFUN (no_ip_route_mask_flags2_tag,
       no_ip_route_mask_flags2_tag_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 NULL, NULL, argv[2], NULL, NULL);
}

DEFUN (no_ip_route_mask_flags2_tag_vrf,
       no_ip_route_mask_flags2_tag_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 NULL, NULL, argv[2], NULL, argv[3]);
}

DEFUN (no_ip_route_distance,
       no_ip_route_distance_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL,
				 argv[1], NULL, NULL, argv[2], NULL);
}

DEFUN (no_ip_route_tag_distance,
       no_ip_route_tag_distance_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 NULL, argv[2], argv[3], NULL);
}

DEFUN (no_ip_route_tag_distance_vrf,
       no_ip_route_tag_distance_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 NULL, argv[2], argv[3], argv[4]);
}

DEFUN (no_ip_route_flags_distance,
       no_ip_route_flags_distance_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL,
				 argv[1], argv[2], NULL, argv[3], NULL);
}

DEFUN (no_ip_route_flags_tag_distance,
       no_ip_route_flags_tag_distance_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 argv[2], argv[3], argv[4], NULL);
}

DEFUN (no_ip_route_flags_tag_distance_vrf,
       no_ip_route_flags_tag_distance_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 argv[2], argv[3], argv[4], argv[5]);
}

DEFUN (no_ip_route_flags_distance2,
       no_ip_route_flags_distance2_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 argv[1], NULL, argv[2], NULL);
}

DEFUN (no_ip_route_flags_tag_distance2,
       no_ip_route_flags_tag_distance2_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 argv[1], argv[2] , argv[3], NULL);
}

DEFUN (no_ip_route_flags_tag_distance2_vrf,
       no_ip_route_flags_tag_distance2_vrf_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 argv[1], argv[2] , argv[3], argv[4]);
}

DEFUN (no_ip_route_mask_distance,
       no_ip_route_mask_distance_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, NULL, argv[3], NULL);
}

DEFUN (no_ip_route_mask_tag_distance,
       no_ip_route_mask_tag_distance_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, argv[3], argv[4], NULL);
}

DEFUN (no_ip_route_mask_tag_distance_vrf,
       no_ip_route_mask_tag_distance_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, argv[3], argv[4], argv[5]);
}

DEFUN (no_ip_route_mask_flags_distance,
       no_ip_route_mask_flags_distance_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], argv[3], NULL, argv[4], NULL);
}

DEFUN (no_ip_route_mask_flags_tag_distance,
       no_ip_route_mask_flags_tag_distance_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], argv[2], argv[3],
				 argv[4], argv[5], NULL);
}

DEFUN (no_ip_route_mask_flags_tag_distance_vrf,
       no_ip_route_mask_flags_tag_distance_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], argv[2], argv[3],
				 argv[4], argv[5], argv[6]);
}

DEFUN (no_ip_route_mask_flags_distance2,
       no_ip_route_mask_flags_distance2_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 NULL, argv[2], NULL, argv[3], NULL);
}

DEFUN (ip_route_vrf,
       ip_route_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, NULL, NULL, argv[2]);
}

DEFUN (ip_route_flags_vrf,
       ip_route_flags_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], argv[2], NULL, NULL, argv[3]);
}

DEFUN (ip_route_flags2_vrf,
       ip_route_flags2_vrf_cmd,
       "ip route A.B.C.D/M (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], NULL, NULL, argv[2]);
}

/* Mask as A.B.C.D format.  */
DEFUN (ip_route_mask_vrf,
       ip_route_mask_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], NULL, NULL, NULL, argv[3]);
}

DEFUN (ip_route_mask_flags_vrf,
       ip_route_mask_flags_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], argv[3], NULL, NULL, argv[4]);
}

DEFUN (ip_route_mask_flags2_vrf,
       ip_route_mask_flags2_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 NULL, argv[2], NULL, NULL, argv[3]);
}

/* Distance option value.  */
DEFUN (ip_route_distance_vrf,
       ip_route_distance_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], NULL, NULL, argv[2], argv[3]);
}

DEFUN (ip_route_flags_distance_vrf,
       ip_route_flags_distance_vrf_cmd,
       "ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 argv[1], argv[2], NULL, argv[3], argv[4]);
}

DEFUN (ip_route_flags_distance2_vrf,
       ip_route_flags_distance2_vrf_cmd,
       "ip route A.B.C.D/M (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], NULL,
				 NULL, argv[1], NULL, argv[2], argv[3]);
}

DEFUN (ip_route_mask_distance_vrf,
       ip_route_mask_distance_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], NULL, NULL, argv[3], argv[4]);
}

DEFUN (ip_route_mask_flags_distance_vrf,
       ip_route_mask_flags_distance_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 argv[2], argv[3], NULL, argv[4], argv[5]);
}

DEFUN (ip_route_mask_flags_distance2_vrf,
       ip_route_mask_flags_distance2_vrf_cmd,
       "ip route A.B.C.D A.B.C.D (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 1, argv[0], argv[1],
				 NULL, argv[2], NULL, argv[3], argv[4]);
}

DEFUN (no_ip_route_vrf,
       no_ip_route_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0],
				 NULL, argv[1], NULL, NULL, NULL,
				 (argc > 3) ? argv[3] : argv[2]);
}

ALIAS (no_ip_route_vrf,
       no_ip_route_flags_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)

DEFUN (no_ip_route_flags2_vrf,
       no_ip_route_flags2_vrf_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0],
				 NULL, NULL, NULL, NULL, NULL, argv[2]);
}

DEFUN (no_ip_route_mask_vrf,
       no_ip_route_mask_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, NULL, NULL,
				 (argc > 4) ? argv[4] : argv[3]);
}

ALIAS (no_ip_route_mask_vrf,
       no_ip_route_mask_flags_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)

DEFUN (no_ip_route_mask_flags2_vrf,
       no_ip_route_mask_flags2_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 NULL, NULL, NULL, NULL, argv[2]);
}

DEFUN (no_ip_route_distance_vrf,
       no_ip_route_distance_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE|null0) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL,
				 argv[1], NULL, NULL, argv[2], argv[3]);
}

DEFUN (no_ip_route_flags_distance_vrf,
       no_ip_route_flags_distance_vrf_cmd,
       "no ip route A.B.C.D/M (A.B.C.D|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, argv[1],
				 argv[2], NULL, argv[3], argv[4]);
}

DEFUN (no_ip_route_flags_distance2_vrf,
       no_ip_route_flags_distance2_vrf_cmd,
       "no ip route A.B.C.D/M (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix (e.g. 10.0.0.0/8)\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], NULL, NULL,
				 argv[1], NULL, argv[2], argv[3]);
}

DEFUN (no_ip_route_mask_distance_vrf,
       no_ip_route_mask_distance_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE|null0) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Null interface\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1],
				 argv[2], NULL, NULL, argv[3], argv[4]);
}

DEFUN (no_ip_route_mask_flags_distance_vrf,
       no_ip_route_mask_flags_distance_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (A.B.C.D|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "IP gateway address\n"
       "IP gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], argv[2],
				 argv[3], NULL, argv[4], argv[5]);
}

DEFUN (no_ip_route_mask_flags_distance2_vrf,
       no_ip_route_mask_flags_distance2_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], NULL,
                                 argv[2], NULL, argv[3], argv[4]);
}

DEFUN (no_ip_route_mask_flags_tag_distance2,
       no_ip_route_mask_flags_tag_distance2_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n")
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], NULL,
				 argv[2], argv[3], argv[4], NULL);
}

DEFUN (no_ip_route_mask_flags_tag_distance2_vrf,
       no_ip_route_mask_flags_tag_distance2_vrf_cmd,
       "no ip route A.B.C.D A.B.C.D (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IP destination prefix\n"
       "IP destination prefix mask\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Tag of this route\n"
       "Tag value\n"
       "Distance value for this route\n"
       VRF_CMD_HELP_STR)
{
  return zebra_static_ipv4_safi (vty, SAFI_UNICAST, 0, argv[0], argv[1], NULL,
				 argv[2], argv[3], argv[4], argv[5]);
}

char *proto_rm[AFI_MAX][ZEBRA_ROUTE_MAX+1];	/* "any" == ZEBRA_ROUTE_MAX */

DEFUN (ip_protocol,
       ip_protocol_cmd,
       "ip protocol PROTO route-map ROUTE-MAP",
       NO_STR
       "Apply route map to PROTO\n"
       "Protocol name\n"
       "Route map name\n")
{
  int i;

  if (strcasecmp(argv[0], "any") == 0)
    i = ZEBRA_ROUTE_MAX;
  else
    i = proto_name2num(argv[0]);
  if (i < 0)
    {
      vty_out (vty, "invalid protocol name \"%s\"%s", argv[0] ? argv[0] : "",
               VTY_NEWLINE);
      return CMD_WARNING;
    }
  if (proto_rm[AFI_IP][i])
    XFREE (MTYPE_ROUTE_MAP_NAME, proto_rm[AFI_IP][i]);
  proto_rm[AFI_IP][i] = XSTRDUP (MTYPE_ROUTE_MAP_NAME, argv[1]);
  return CMD_SUCCESS;
}

DEFUN (no_ip_protocol,
       no_ip_protocol_cmd,
       "no ip protocol PROTO",
       NO_STR
       "Remove route map from PROTO\n"
       "Protocol name\n")
{
  int i;

  if (strcasecmp(argv[0], "any") == 0)
    i = ZEBRA_ROUTE_MAX;
  else
    i = proto_name2num(argv[0]);
  if (i < 0)
    {
      vty_out (vty, "invalid protocol name \"%s\"%s", argv[0] ? argv[0] : "",
               VTY_NEWLINE);
     return CMD_WARNING;
    }
  if (proto_rm[AFI_IP][i])
    XFREE (MTYPE_ROUTE_MAP_NAME, proto_rm[AFI_IP][i]);
  proto_rm[AFI_IP][i] = NULL;
  return CMD_SUCCESS;
}

/* New RIB.  Detailed information for IPv4 route. */
static void
vty_show_ip_route_detail (struct vty *vty, struct route_node *rn, int mcast)
{
  struct rib *rib;
  struct nexthop *nexthop, *tnexthop;
  int recursing;
  char buf[PREFIX_STRLEN];

  RNODE_FOREACH_RIB (rn, rib)
    {
      const char *mcast_info = "";
      if (mcast)
        {
          rib_table_info_t *info = rn->table->info;
          mcast_info = (info->safi == SAFI_MULTICAST)
                       ? " using Multicast RIB"
                       : " using Unicast RIB";
        }
      vty_out (vty, "Routing entry for %s%s%s",
               prefix2str (&rn->p, buf, sizeof(buf)), mcast_info,
              VTY_NEWLINE);
      vty_out (vty, "  Known via \"%s\"", zebra_route_string (rib->type));
      vty_out (vty, ", distance %u, metric %u", rib->distance, rib->metric);
      if (rib->mtu)
        vty_out (vty, ", mtu %u", rib->mtu);
      vty_out (vty, ", tag %d", rib->tag);
      vty_out (vty, ", vrf %u", rib->vrf_id);
      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_SELECTED))
        vty_out (vty, ", best");
      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_FIB_OVERRIDE))
        vty_out (vty, ", fib-override");
      if (CHECK_FLAG (rib->status, RIB_ENTRY_SELECTED_FIB))
        vty_out (vty, ", fib");
      if (rib->refcnt)
        vty_out (vty, ", refcnt %ld", rib->refcnt);
      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_BLACKHOLE))
       vty_out (vty, ", blackhole");
      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_REJECT))
       vty_out (vty, ", reject");
      vty_out (vty, "%s", VTY_NEWLINE);

#define ONE_DAY_SECOND 60*60*24
#define ONE_WEEK_SECOND 60*60*24*7
      if (rib->type == ZEBRA_ROUTE_RIP
          || rib->type == ZEBRA_ROUTE_RIPNG
          || rib->type == ZEBRA_ROUTE_OSPF
          || rib->type == ZEBRA_ROUTE_OSPF6
          || rib->type == ZEBRA_ROUTE_BABEL
          || rib->type == ZEBRA_ROUTE_ISIS
          || rib->type == ZEBRA_ROUTE_BGP)
	{
	  time_t uptime;
	  struct tm *tm;

	  uptime = time (NULL);
	  uptime -= rib->uptime;
	  tm = gmtime (&uptime);

	  vty_out (vty, "  Last update ");

	  if (uptime < ONE_DAY_SECOND)
	    vty_out (vty,  "%02d:%02d:%02d", 
		     tm->tm_hour, tm->tm_min, tm->tm_sec);
	  else if (uptime < ONE_WEEK_SECOND)
	    vty_out (vty, "%dd%02dh%02dm", 
		     tm->tm_yday, tm->tm_hour, tm->tm_min);
	  else
	    vty_out (vty, "%02dw%dd%02dh", 
		     tm->tm_yday/7,
		     tm->tm_yday - ((tm->tm_yday/7) * 7), tm->tm_hour);
	  vty_out (vty, " ago%s", VTY_NEWLINE);
	}

      for (ALL_NEXTHOPS_RO(rib->nexthop, nexthop, tnexthop, recursing))
        {
          vty_out (vty, "  %c%c%s",
                   CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_ACTIVE) ? '>' : ' ',
                   CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB) ? '*' : ' ',
                   recursing ? "  " : "");

          switch (nexthop->type)
            {
            case NEXTHOP_TYPE_IPV4:
            case NEXTHOP_TYPE_IPV4_IFINDEX:
              vty_out (vty, " %s", inet_ntoa (nexthop->gate.ipv4));
              if (nexthop->ifindex)
                vty_out (vty, ", via %s",
                         ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
              break;
            case NEXTHOP_TYPE_IPV6:
            case NEXTHOP_TYPE_IPV6_IFINDEX:
            case NEXTHOP_TYPE_IPV6_IFNAME:
              vty_out (vty, " %s",
                       inet_ntop (AF_INET6, &nexthop->gate.ipv6, buf, sizeof(buf)));
              if (nexthop->type == NEXTHOP_TYPE_IPV6_IFNAME)
                vty_out (vty, ", %s", nexthop->ifname);
              else if (nexthop->ifindex)
                vty_out (vty, ", via %s",
                         ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
              break;
            case NEXTHOP_TYPE_IFINDEX:
              vty_out (vty, " directly connected, %s",
                       ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
              break;
            case NEXTHOP_TYPE_IFNAME:
              vty_out (vty, " directly connected, %s", nexthop->ifname);
              break;
            case NEXTHOP_TYPE_BLACKHOLE:
              vty_out (vty, " directly connected, Null0");
              break;
            default:
              break;
            }
          if (! CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_ACTIVE))
            vty_out (vty, " inactive");

          if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_ONLINK))
            vty_out (vty, " onlink");

          if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_RECURSIVE))
            vty_out (vty, " (recursive)");

          switch (nexthop->type)
            {
            case NEXTHOP_TYPE_IPV4:
            case NEXTHOP_TYPE_IPV4_IFINDEX:
            case NEXTHOP_TYPE_IPV4_IFNAME:
              if (nexthop->src.ipv4.s_addr)
                {
                  if (inet_ntop(AF_INET, &nexthop->src.ipv4, buf, sizeof buf))
                    vty_out (vty, ", src %s", buf);
                }
              break;
#ifdef HAVE_IPV6
            case NEXTHOP_TYPE_IPV6:
            case NEXTHOP_TYPE_IPV6_IFINDEX:
            case NEXTHOP_TYPE_IPV6_IFNAME:
              if (!IPV6_ADDR_SAME(&nexthop->src.ipv6, &in6addr_any))
                {
                  if (inet_ntop(AF_INET6, &nexthop->src.ipv6, buf, sizeof buf))
                    vty_out (vty, ", src %s", buf);
                }
              break;
#endif /* HAVE_IPV6 */
            default:
               break;
            }
          vty_out (vty, "%s", VTY_NEWLINE);
        }
      vty_out (vty, "%s", VTY_NEWLINE);
    }
}

static void
vty_show_ip_route (struct vty *vty, struct route_node *rn, struct rib *rib)
{
  struct nexthop *nexthop, *tnexthop;
  int recursing;
  int len = 0;
  char buf[BUFSIZ];

  /* Nexthop information. */
  for (ALL_NEXTHOPS_RO(rib->nexthop, nexthop, tnexthop, recursing))
    {
      if (nexthop == rib->nexthop)
	{
	  /* Prefix information. */
	  len = vty_out (vty, "%c%c%c %s",
			 zebra_route_char (rib->type),
			 CHECK_FLAG (rib->flags, ZEBRA_FLAG_SELECTED)
			 ? '>' : ' ',
			 CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB)
			 ? '*' : ' ',
			 prefix2str (&rn->p, buf, sizeof buf));
		
	  /* Distance and metric display. */
	  if (rib->type != ZEBRA_ROUTE_CONNECT 
	      && rib->type != ZEBRA_ROUTE_KERNEL)
	    len += vty_out (vty, " [%d/%d]", rib->distance,
			    rib->metric);

          if (rib->vrf_id != VRF_DEFAULT)
            len += vty_out (vty, " [vrf %u]", rib->vrf_id);
	}
      else
	vty_out (vty, "  %c%*c",
		 CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB)
		 ? '*' : ' ',
		 len - 3 + (2 * recursing), ' ');

      switch (nexthop->type)
        {
        case NEXTHOP_TYPE_IPV4:
        case NEXTHOP_TYPE_IPV4_IFINDEX:
          vty_out (vty, " via %s", inet_ntoa (nexthop->gate.ipv4));
          if (nexthop->ifindex)
            vty_out (vty, ", %s",
                     ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
          break;
        case NEXTHOP_TYPE_IPV6:
        case NEXTHOP_TYPE_IPV6_IFINDEX:
        case NEXTHOP_TYPE_IPV6_IFNAME:
          vty_out (vty, " via %s",
                   inet_ntop (AF_INET6, &nexthop->gate.ipv6, buf, BUFSIZ));
          if (nexthop->type == NEXTHOP_TYPE_IPV6_IFNAME)
            vty_out (vty, ", %s", nexthop->ifname);
          else if (nexthop->ifindex)
            vty_out (vty, ", %s",
                     ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
          break;
        case NEXTHOP_TYPE_IFINDEX:
          vty_out (vty, " is directly connected, %s",
                   ifindex2ifname_vrf (nexthop->ifindex, rib->vrf_id));
          break;
        case NEXTHOP_TYPE_IFNAME:
          vty_out (vty, " is directly connected, %s", nexthop->ifname);
          break;
        case NEXTHOP_TYPE_BLACKHOLE:
          vty_out (vty, " is directly connected, Null0");
          break;
        default:
          break;
        }
      if (! CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_ACTIVE))
        vty_out (vty, " inactive");

      if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_ONLINK))
        vty_out (vty, " onlink");

      if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_RECURSIVE))
        vty_out (vty, " (recursive)");

      switch (nexthop->type)
        {
          case NEXTHOP_TYPE_IPV4:
          case NEXTHOP_TYPE_IPV4_IFINDEX:
          case NEXTHOP_TYPE_IPV4_IFNAME:
            if (nexthop->src.ipv4.s_addr)
              {
                if (inet_ntop(AF_INET, &nexthop->src.ipv4, buf, sizeof buf))
                  vty_out (vty, ", src %s", buf);
              }
            break;
#ifdef HAVE_IPV6
          case NEXTHOP_TYPE_IPV6:
          case NEXTHOP_TYPE_IPV6_IFINDEX:
          case NEXTHOP_TYPE_IPV6_IFNAME:
            if (!IPV6_ADDR_SAME(&nexthop->src.ipv6, &in6addr_any))
              {
                if (inet_ntop(AF_INET6, &nexthop->src.ipv6, buf, sizeof buf))
                  vty_out (vty, ", src %s", buf);
              }
            break;
#endif /* HAVE_IPV6 */
          default:
            break;
        }

      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_BLACKHOLE))
               vty_out (vty, ", bh");
      if (CHECK_FLAG (rib->flags, ZEBRA_FLAG_REJECT))
               vty_out (vty, ", rej");

      if (rib->type == ZEBRA_ROUTE_RIP
          || rib->type == ZEBRA_ROUTE_RIPNG
          || rib->type == ZEBRA_ROUTE_OSPF
          || rib->type == ZEBRA_ROUTE_OSPF6
          || rib->type == ZEBRA_ROUTE_BABEL
          || rib->type == ZEBRA_ROUTE_ISIS
          || rib->type == ZEBRA_ROUTE_BGP)
	{
	  time_t uptime;
	  struct tm *tm;

	  uptime = time (NULL);
	  uptime -= rib->uptime;
	  tm = gmtime (&uptime);

#define ONE_DAY_SECOND 60*60*24
#define ONE_WEEK_SECOND 60*60*24*7

	  if (uptime < ONE_DAY_SECOND)
	    vty_out (vty,  ", %02d:%02d:%02d", 
		     tm->tm_hour, tm->tm_min, tm->tm_sec);
	  else if (uptime < ONE_WEEK_SECOND)
	    vty_out (vty, ", %dd%02dh%02dm", 
		     tm->tm_yday, tm->tm_hour, tm->tm_min);
	  else
	    vty_out (vty, ", %02dw%dd%02dh", 
		     tm->tm_yday/7,
		     tm->tm_yday - ((tm->tm_yday/7) * 7), tm->tm_hour);
	}
      vty_out (vty, "%s", VTY_NEWLINE);
    }
}

DEFUN (show_ip_route,
       show_ip_route_cmd,
       "show ip route",
       SHOW_STR
       IP_STR
       "IP routing table\n")
{
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  return do_show_ip_route(vty, SAFI_UNICAST, vrf_id);
}

static int do_show_ip_route(struct vty *vty, safi_t safi, vrf_id_t vrf_id)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;

  table = zebra_vrf_table (AFI_IP, safi, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show all IPv4 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
	if (first)
	  {
	    vty_out (vty, SHOW_ROUTE_V4_HEADER);
	    first = 0;
	  }
	vty_show_ip_route (vty, rn, rib);
      }
  return CMD_SUCCESS;
}

ALIAS (show_ip_route,
       show_ip_route_vrf_cmd,
       "show ip route " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_nht,
       show_ip_nht_cmd,
       "show ip nht",
       SHOW_STR
       IP_STR
       "IP nexthop tracking table\n")
{
  zebra_print_rnh_table(0, AF_INET, vty);
  return CMD_SUCCESS;
}

DEFUN (show_ipv6_nht,
       show_ipv6_nht_cmd,
       "show ipv6 nht",
       SHOW_STR
       IP_STR
       "IPv6 nexthop tracking table\n")
{
  zebra_print_rnh_table(0, AF_INET6, vty);
  return CMD_SUCCESS;
}

DEFUN (show_ip_route_tag,
       show_ip_route_tag_cmd,
       "show ip route tag <1-65535>",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Show only routes with tag\n"
       "Tag value\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  route_tag_t tag = 0;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argv[0])
    tag = atoi(argv[0]);

  if (argc == 2)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show all IPv4 routes with matching tag value. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
        if (rib->tag != tag)
          continue;

        if (first)
          {
            vty_out (vty, SHOW_ROUTE_V4_HEADER);
            first = 0;
          }
        vty_show_ip_route (vty, rn, rib);
      }
  return CMD_SUCCESS;
}

ALIAS (show_ip_route_tag,
       show_ip_route_tag_vrf_cmd,
       "show ip route tag <1-65535>" VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Show only routes with tag\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_prefix_longer,
       show_ip_route_prefix_longer_cmd,
       "show ip route A.B.C.D/M longer-prefixes",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       "Show route matching the specified Network/Mask pair only\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct prefix p;
  int ret;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix (argv[0], &p);
  if (! ret)
    {
      vty_out (vty, "%% Malformed Prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show matched type IPv4 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      if (prefix_match (&p, &rn->p))
	{
	  if (first)
	    {
	      vty_out (vty, SHOW_ROUTE_V4_HEADER);
	      first = 0;
	    }
	  vty_show_ip_route (vty, rn, rib);
	}
  return CMD_SUCCESS;
}

ALIAS (show_ip_route_prefix_longer,
       show_ip_route_prefix_longer_vrf_cmd,
       "show ip route A.B.C.D/M longer-prefixes " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       "Show route matching the specified Network/Mask pair only\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_supernets,
       show_ip_route_supernets_cmd,
       "show ip route supernets-only",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Show supernet entries only\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  u_int32_t addr;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show matched type IPv4 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
	addr = ntohl (rn->p.u.prefix4.s_addr);

	if ((IN_CLASSC (addr) && rn->p.prefixlen < 24)
	   || (IN_CLASSB (addr) && rn->p.prefixlen < 16)
	   || (IN_CLASSA (addr) && rn->p.prefixlen < 8))
	  {
	    if (first)
	      {
		vty_out (vty, SHOW_ROUTE_V4_HEADER);
		first = 0;
	      }
	    vty_show_ip_route (vty, rn, rib);
	  }
      }
  return CMD_SUCCESS;
}

ALIAS (show_ip_route_supernets,
       show_ip_route_supernets_vrf_cmd,
       "show ip route supernets-only " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Show supernet entries only\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_protocol,
       show_ip_route_protocol_cmd,
       "show ip route " QUAGGA_IP_REDIST_STR_ZEBRA,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       QUAGGA_IP_REDIST_HELP_STR_ZEBRA)
{
  int type;
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  type = proto_redistnum (AFI_IP, argv[0]);
  if (type < 0)
    {
      vty_out (vty, "Unknown route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show matched type IPv4 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      if (rib->type == type)
	{
	  if (first)
	    {
	      vty_out (vty, SHOW_ROUTE_V4_HEADER);
	      first = 0;
	    }
	  vty_show_ip_route (vty, rn, rib);
	}
  return CMD_SUCCESS;
}

ALIAS (show_ip_route_protocol,
       show_ip_route_protocol_vrf_cmd,
       "show ip route " QUAGGA_IP_REDIST_STR_ZEBRA " " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       QUAGGA_IP_REDIST_HELP_STR_ZEBRA
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_addr,
       show_ip_route_addr_cmd,
       "show ip route A.B.C.D",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Network in the IP routing table to display\n")
{
  int ret;
  struct prefix_ipv4 p;
  struct route_table *table;
  struct route_node *rn;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix_ipv4 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed IPv4 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  rn = route_node_match (table, (struct prefix *) &p);
  if (! rn)
    {
      vty_out (vty, "%% Network not in table%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  vty_show_ip_route_detail (vty, rn, 0);

  route_unlock_node (rn);

  return CMD_SUCCESS;
}

ALIAS (show_ip_route_addr,
       show_ip_route_addr_vrf_cmd,
       "show ip route A.B.C.D " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Network in the IP routing table to display\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_prefix,
       show_ip_route_prefix_cmd,
       "show ip route A.B.C.D/M",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n")
{
  int ret;
  struct prefix_ipv4 p;
  struct route_table *table;
  struct route_node *rn;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix_ipv4 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed IPv4 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  rn = route_node_match (table, (struct prefix *) &p);
  if (! rn || rn->p.prefixlen != p.prefixlen)
    {
      vty_out (vty, "%% Network not in table%s", VTY_NEWLINE);
      if (rn)
        route_unlock_node (rn);
      return CMD_WARNING;
    }

  vty_show_ip_route_detail (vty, rn, 0);

  route_unlock_node (rn);

  return CMD_SUCCESS;
}

ALIAS (show_ip_route_prefix,
       show_ip_route_prefix_vrf_cmd,
       "show ip route A.B.C.D/M " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       VRF_CMD_HELP_STR)

static void
vty_show_ip_route_summary (struct vty *vty, struct route_table *table)
{
  struct route_node *rn;
  struct rib *rib;
  struct nexthop *nexthop;
#define ZEBRA_ROUTE_IBGP  ZEBRA_ROUTE_MAX
#define ZEBRA_ROUTE_TOTAL (ZEBRA_ROUTE_IBGP + 1)
  u_int32_t rib_cnt[ZEBRA_ROUTE_TOTAL + 1];
  u_int32_t fib_cnt[ZEBRA_ROUTE_TOTAL + 1];
  u_int32_t i;

  memset (&rib_cnt, 0, sizeof(rib_cnt));
  memset (&fib_cnt, 0, sizeof(fib_cnt));
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      for (nexthop = rib->nexthop; nexthop; nexthop = nexthop->next)
        {
	  rib_cnt[ZEBRA_ROUTE_TOTAL]++;
	  rib_cnt[rib->type]++;
	  if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB)
	      || nexthop_has_fib_child(nexthop))
	    {
	      fib_cnt[ZEBRA_ROUTE_TOTAL]++;
	      fib_cnt[rib->type]++;
	    }
	  if (rib->type == ZEBRA_ROUTE_BGP && 
	      CHECK_FLAG (rib->flags, ZEBRA_FLAG_IBGP)) 
	    {
	      rib_cnt[ZEBRA_ROUTE_IBGP]++;
	      if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB)
		  || nexthop_has_fib_child(nexthop))
		fib_cnt[ZEBRA_ROUTE_IBGP]++;
	    }
	}

  vty_out (vty, "%-20s %-20s %s  (vrf %u)%s",
           "Route Source", "Routes", "FIB",
           ((rib_table_info_t *)table->info)->zvrf->vrf_id,
           VTY_NEWLINE);

  for (i = 0; i < ZEBRA_ROUTE_MAX; i++) 
    {
      if (rib_cnt[i] > 0)
	{
	  if (i == ZEBRA_ROUTE_BGP)
	    {
	      vty_out (vty, "%-20s %-20d %-20d %s", "ebgp", 
		       rib_cnt[ZEBRA_ROUTE_BGP] - rib_cnt[ZEBRA_ROUTE_IBGP],
		       fib_cnt[ZEBRA_ROUTE_BGP] - fib_cnt[ZEBRA_ROUTE_IBGP],
		       VTY_NEWLINE);
	      vty_out (vty, "%-20s %-20d %-20d %s", "ibgp", 
		       rib_cnt[ZEBRA_ROUTE_IBGP], fib_cnt[ZEBRA_ROUTE_IBGP],
		       VTY_NEWLINE);
	    }
	  else 
	    vty_out (vty, "%-20s %-20d %-20d %s", zebra_route_string(i), 
		     rib_cnt[i], fib_cnt[i], VTY_NEWLINE);
	}
    }

  vty_out (vty, "------%s", VTY_NEWLINE);
  vty_out (vty, "%-20s %-20d %-20d %s", "Totals", rib_cnt[ZEBRA_ROUTE_TOTAL], 
	   fib_cnt[ZEBRA_ROUTE_TOTAL], VTY_NEWLINE);  
  vty_out (vty, "%s", VTY_NEWLINE);
}

/*
 * Implementation of the ip route summary prefix command.
 *
 * This command prints the primary prefixes that have been installed by various
 * protocols on the box.
 *
 */
static void
vty_show_ip_route_summary_prefix (struct vty *vty, struct route_table *table)
{
  struct route_node *rn;
  struct rib *rib;
  struct nexthop *nexthop;
#define ZEBRA_ROUTE_IBGP  ZEBRA_ROUTE_MAX
#define ZEBRA_ROUTE_TOTAL (ZEBRA_ROUTE_IBGP + 1)
  u_int32_t rib_cnt[ZEBRA_ROUTE_TOTAL + 1];
  u_int32_t fib_cnt[ZEBRA_ROUTE_TOTAL + 1];
  u_int32_t i;
  int       cnt;

  memset (&rib_cnt, 0, sizeof(rib_cnt));
  memset (&fib_cnt, 0, sizeof(fib_cnt));
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {

       /*
        * In case of ECMP, count only once.
        */
       cnt = 0;
       for (nexthop = rib->nexthop; (!cnt && nexthop); nexthop = nexthop->next)
         {
          cnt++;
          rib_cnt[ZEBRA_ROUTE_TOTAL]++;
          rib_cnt[rib->type]++;
          if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB))
	        {
	         fib_cnt[ZEBRA_ROUTE_TOTAL]++;
             fib_cnt[rib->type]++;
            }
	      if (rib->type == ZEBRA_ROUTE_BGP &&
	          CHECK_FLAG (rib->flags, ZEBRA_FLAG_IBGP))
            {
	         rib_cnt[ZEBRA_ROUTE_IBGP]++;
		     if (CHECK_FLAG (nexthop->flags, NEXTHOP_FLAG_FIB))
		        fib_cnt[ZEBRA_ROUTE_IBGP]++;
            }
	     }
      }

  vty_out (vty, "%-20s %-20s %s  (vrf %u)%s",
           "Route Source", "Prefix Routes", "FIB",
           ((rib_table_info_t *)table->info)->zvrf->vrf_id,
           VTY_NEWLINE);

  for (i = 0; i < ZEBRA_ROUTE_MAX; i++)
    {
      if (rib_cnt[i] > 0)
	{
	  if (i == ZEBRA_ROUTE_BGP)
	    {
	      vty_out (vty, "%-20s %-20d %-20d %s", "ebgp",
		       rib_cnt[ZEBRA_ROUTE_BGP] - rib_cnt[ZEBRA_ROUTE_IBGP],
		       fib_cnt[ZEBRA_ROUTE_BGP] - fib_cnt[ZEBRA_ROUTE_IBGP],
		       VTY_NEWLINE);
	      vty_out (vty, "%-20s %-20d %-20d %s", "ibgp",
		       rib_cnt[ZEBRA_ROUTE_IBGP], fib_cnt[ZEBRA_ROUTE_IBGP],
		       VTY_NEWLINE);
	    }
	  else
	    vty_out (vty, "%-20s %-20d %-20d %s", zebra_route_string(i),
		     rib_cnt[i], fib_cnt[i], VTY_NEWLINE);
	}
    }

  vty_out (vty, "------%s", VTY_NEWLINE);
  vty_out (vty, "%-20s %-20d %-20d %s", "Totals", rib_cnt[ZEBRA_ROUTE_TOTAL],
	   fib_cnt[ZEBRA_ROUTE_TOTAL], VTY_NEWLINE);
  vty_out (vty, "%s", VTY_NEWLINE);
}

/* Show route summary.  */
DEFUN (show_ip_route_summary,
       show_ip_route_summary_cmd,
       "show ip route summary",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n")
{
  struct route_table *table;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  vty_show_ip_route_summary (vty, table);

  return CMD_SUCCESS;
}

ALIAS (show_ip_route_summary,
       show_ip_route_summary_vrf_cmd,
       "show ip route summary " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n"
       VRF_CMD_HELP_STR)

/* Show route summary prefix.  */
DEFUN (show_ip_route_summary_prefix,
       show_ip_route_summary_prefix_cmd,
       "show ip route summary prefix",
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n"
       "Prefix routes\n")
{
  struct route_table *table;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  vty_show_ip_route_summary_prefix (vty, table);

  return CMD_SUCCESS;
}

ALIAS (show_ip_route_summary_prefix,
       show_ip_route_summary_prefix_vrf_cmd,
       "show ip route summary prefix " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n"
       "Prefix routes\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ip_route_vrf_all,
       show_ip_route_vrf_all_cmd,
       "show ip route " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int first = 1;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      /* Show all IPv4 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          {
            if (first)
              {
                vty_out (vty, SHOW_ROUTE_V4_HEADER);
                first = 0;
              }
            vty_show_ip_route (vty, rn, rib);
          }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_prefix_longer_vrf_all,
       show_ip_route_prefix_longer_vrf_all_cmd,
       "show ip route A.B.C.D/M longer-prefixes " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       "Show route matching the specified Network/Mask pair only\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct prefix p;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int ret;
  int first = 1;

  ret = str2prefix (argv[0], &p);
  if (! ret)
    {
      vty_out (vty, "%% Malformed Prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      /* Show matched type IPv4 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          if (prefix_match (&p, &rn->p))
            {
              if (first)
                {
                  vty_out (vty, SHOW_ROUTE_V4_HEADER);
                  first = 0;
                }
              vty_show_ip_route (vty, rn, rib);
            }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_supernets_vrf_all,
       show_ip_route_supernets_vrf_all_cmd,
       "show ip route supernets-only " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Show supernet entries only\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  u_int32_t addr;
  int first = 1;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      /* Show matched type IPv4 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          {
            addr = ntohl (rn->p.u.prefix4.s_addr);

            if ((IN_CLASSC (addr) && rn->p.prefixlen < 24)
               || (IN_CLASSB (addr) && rn->p.prefixlen < 16)
               || (IN_CLASSA (addr) && rn->p.prefixlen < 8))
              {
                if (first)
                  {
                    vty_out (vty, SHOW_ROUTE_V4_HEADER);
                    first = 0;
                  }
                vty_show_ip_route (vty, rn, rib);
              }
          }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_protocol_vrf_all,
       show_ip_route_protocol_vrf_all_cmd,
       "show ip route " QUAGGA_IP_REDIST_STR_ZEBRA " " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       QUAGGA_IP_REDIST_HELP_STR_ZEBRA
       VRF_ALL_CMD_HELP_STR)
{
  int type;
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int first = 1;

  type = proto_redistnum (AFI_IP, argv[0]);
  if (type < 0)
    {
      vty_out (vty, "Unknown route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      /* Show matched type IPv4 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          if (rib->type == type)
            {
              if (first)
                {
                  vty_out (vty, SHOW_ROUTE_V4_HEADER);
                  first = 0;
                }
              vty_show_ip_route (vty, rn, rib);
            }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_addr_vrf_all,
       show_ip_route_addr_vrf_all_cmd,
       "show ip route A.B.C.D " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Network in the IP routing table to display\n"
       VRF_ALL_CMD_HELP_STR)
{
  int ret;
  struct prefix_ipv4 p;
  struct route_table *table;
  struct route_node *rn;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  ret = str2prefix_ipv4 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed IPv4 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      rn = route_node_match (table, (struct prefix *) &p);
      if (! rn)
        continue;

      vty_show_ip_route_detail (vty, rn, 0);

      route_unlock_node (rn);
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_prefix_vrf_all,
       show_ip_route_prefix_vrf_all_cmd,
       "show ip route A.B.C.D/M " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "IP prefix <network>/<length>, e.g., 35.0.0.0/8\n"
       VRF_ALL_CMD_HELP_STR)
{
  int ret;
  struct prefix_ipv4 p;
  struct route_table *table;
  struct route_node *rn;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  ret = str2prefix_ipv4 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed IPv4 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP][SAFI_UNICAST]) == NULL)
        continue;

      rn = route_node_match (table, (struct prefix *) &p);
      if (! rn)
        continue;
      if (rn->p.prefixlen != p.prefixlen)
        {
          route_unlock_node (rn);
          continue;
        }

      vty_show_ip_route_detail (vty, rn, 0);

      route_unlock_node (rn);
    }

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_summary_vrf_all,
       show_ip_route_summary_vrf_all_cmd,
       "show ip route summary " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    if ((zvrf = vrf_iter2info (iter)) != NULL)
      vty_show_ip_route_summary (vty, zvrf->table[AFI_IP][SAFI_UNICAST]);

  return CMD_SUCCESS;
}

DEFUN (show_ip_route_summary_prefix_vrf_all,
       show_ip_route_summary_prefix_vrf_all_cmd,
       "show ip route summary prefix " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       "Summary of all routes\n"
       "Prefix routes\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    if ((zvrf = vrf_iter2info (iter)) != NULL)
      vty_show_ip_route_summary_prefix (vty, zvrf->table[AFI_IP][SAFI_UNICAST]);

  return CMD_SUCCESS;
}

/* Write IPv4 static route configuration. */
static int
static_config_ipv4 (struct vty *vty, safi_t safi, const char *cmd)
{
  struct route_node *rn;
  struct static_route *si;  
  struct route_table *stable;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int write;

  write = 0;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (stable = zvrf->stable[AFI_IP][safi]) == NULL)
        continue;

      for (rn = route_top (stable); rn; rn = route_next (rn))
        for (si = rn->info; si; si = si->next)
          {
            vty_out (vty, "%s %s/%d", cmd, inet_ntoa (rn->p.u.prefix4),
                     rn->p.prefixlen);

            switch (si->type)
              {
                case STATIC_IPV4_GATEWAY:
                  vty_out (vty, " %s", inet_ntoa (si->addr.ipv4));
                  break;
                case STATIC_IPV4_IFNAME:
                  vty_out (vty, " %s", si->ifname);
                  break;
                case STATIC_IPV4_BLACKHOLE:
                  vty_out (vty, " Null0");
                  break;
              }

            /* flags are incompatible with STATIC_IPV4_BLACKHOLE */
            if (si->type != STATIC_IPV4_BLACKHOLE)
              {
                if (CHECK_FLAG(si->flags, ZEBRA_FLAG_REJECT))
                  vty_out (vty, " %s", "reject");

                if (CHECK_FLAG(si->flags, ZEBRA_FLAG_BLACKHOLE))
                  vty_out (vty, " %s", "blackhole");
              }

	    if (si->tag)
	      vty_out (vty, " tag %d", si->tag);

            if (si->distance != ZEBRA_STATIC_DISTANCE_DEFAULT)
              vty_out (vty, " %d", si->distance);

            if (si->vrf_id != VRF_DEFAULT)
              vty_out (vty, " vrf %u", si->vrf_id);

            vty_out (vty, "%s", VTY_NEWLINE);

            write = 1;
          }
    }
  return write;
}

DEFUN (show_ip_protocol,
       show_ip_protocol_cmd,
       "show ip protocol",
        SHOW_STR
        IP_STR
       "IP protocol filtering status\n")
{
    int i; 

    vty_out(vty, "Protocol    : route-map %s", VTY_NEWLINE);
    vty_out(vty, "------------------------%s", VTY_NEWLINE);
    for (i=0;i<ZEBRA_ROUTE_MAX;i++)
    {
        if (proto_rm[AFI_IP][i])
          vty_out (vty, "%-10s  : %-10s%s", zebra_route_string(i),
					proto_rm[AFI_IP][i],
					VTY_NEWLINE);
        else
          vty_out (vty, "%-10s  : none%s", zebra_route_string(i), VTY_NEWLINE);
    }
    if (proto_rm[AFI_IP][i])
      vty_out (vty, "%-10s  : %-10s%s", "any", proto_rm[AFI_IP][i],
					VTY_NEWLINE);
    else
      vty_out (vty, "%-10s  : none%s", "any", VTY_NEWLINE);

    return CMD_SUCCESS;
}

/* General fucntion for IPv6 static route. */
static int
static_ipv6_func (struct vty *vty, int add_cmd, const char *dest_str,
		  const char *gate_str, const char *ifname,
		  const char *flag_str, const char *tag_str,
		  const char *distance_str, const char *vrf_id_str)
{
  int ret;
  u_char distance;
  struct prefix p;
  struct in6_addr *gate = NULL;
  struct in6_addr gate_addr;
  u_char type = 0;
  vrf_id_t vrf_id = VRF_DEFAULT;
  u_char flag = 0;
  route_tag_t tag = 0;
  
  ret = str2prefix (dest_str, &p);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Apply mask for given prefix. */
  apply_mask (&p);

  /* Route flags */
  if (flag_str) {
    switch(flag_str[0]) {
      case 'r':
      case 'R': /* XXX */
        SET_FLAG (flag, ZEBRA_FLAG_REJECT);
        break;
      case 'b':
      case 'B': /* XXX */
        SET_FLAG (flag, ZEBRA_FLAG_BLACKHOLE);
        break;
      default:
        vty_out (vty, "%% Malformed flag %s %s", flag_str, VTY_NEWLINE);
        return CMD_WARNING;
    }
  }

  /* Administrative distance. */
  if (distance_str)
    distance = atoi (distance_str);
  else
    distance = ZEBRA_STATIC_DISTANCE_DEFAULT;

  /* tag */
  if (tag_str)
    tag = atoi (tag_str);

  /* tag */
  if (tag_str)
    tag = atoi(tag_str);

  /* When gateway is valid IPv6 addrees, then gate is treated as
     nexthop address other case gate is treated as interface name. */
  ret = inet_pton (AF_INET6, gate_str, &gate_addr);

  if (ifname)
    {
      /* When ifname is specified.  It must be come with gateway
         address. */
      if (ret != 1)
	{
	  vty_out (vty, "%% Malformed address%s", VTY_NEWLINE);
	  return CMD_WARNING;
	}
      type = STATIC_IPV6_GATEWAY_IFNAME;
      gate = &gate_addr;
    }
  else
    {
      if (ret == 1)
	{
	  type = STATIC_IPV6_GATEWAY;
	  gate = &gate_addr;
	}
      else
	{
	  type = STATIC_IPV6_IFNAME;
	  ifname = gate_str;
	}
    }

  /* VRF id */
  if (vrf_id_str)
    VTY_GET_INTEGER ("VRF ID", vrf_id, vrf_id_str);

  if (add_cmd)
    static_add_ipv6 (&p, type, gate, ifname, flag, tag, distance, vrf_id);
  else
    static_delete_ipv6 (&p, type, gate, ifname, tag, distance, vrf_id);

  return CMD_SUCCESS;
}

DEFUN (ipv6_route,
       ipv6_route_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE)",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, NULL,
                           NULL, NULL);
}

DEFUN (ipv6_route_tag,
       ipv6_route_tag_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, argv[2], NULL, NULL);
}

DEFUN (ipv6_route_tag_vrf,
       ipv6_route_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, argv[2], NULL, argv[3]);
}

DEFUN (ipv6_route_flags,
       ipv6_route_flags_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], NULL,
                           NULL, NULL);
}

DEFUN (ipv6_route_flags_tag,
       ipv6_route_flags_tag_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], argv[3], NULL, NULL);
}

DEFUN (ipv6_route_flags_tag_vrf,
       ipv6_route_flags_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], argv[3], NULL, argv[4]);
}

DEFUN (ipv6_route_ifname,
       ipv6_route_ifname_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, NULL,
                           NULL, NULL);
}

DEFUN (ipv6_route_ifname_tag,
       ipv6_route_ifname_tag_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, argv[3], NULL, NULL);
}

DEFUN (ipv6_route_ifname_tag_vrf,
       ipv6_route_ifname_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, argv[3], NULL, argv[4]);
}

DEFUN (ipv6_route_ifname_flags,
       ipv6_route_ifname_flags_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole)",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], NULL,
                           NULL, NULL);
}

DEFUN (ipv6_route_ifname_flags_tag,
       ipv6_route_ifname_flags_tag_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], argv[4], NULL, NULL);
}

DEFUN (ipv6_route_ifname_flags_tag_vrf,
       ipv6_route_ifname_flags_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], argv[4], NULL, argv[5]);
}

DEFUN (ipv6_route_pref,
       ipv6_route_pref_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, NULL, argv[2],
                           NULL);
}

DEFUN (ipv6_route_pref_tag,
       ipv6_route_pref_tag_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, argv[2], argv[3], NULL);
}

DEFUN (ipv6_route_pref_tag_vrf,
       ipv6_route_pref_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, argv[2], argv[3], argv[4]);
}

DEFUN (ipv6_route_flags_pref,
       ipv6_route_flags_pref_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], NULL, argv[3],
                           NULL);
}

DEFUN (ipv6_route_flags_pref_tag,
       ipv6_route_flags_pref_tag_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], argv[3], argv[4], NULL);
}

DEFUN (ipv6_route_flags_pref_tag_vrf,
       ipv6_route_flags_pref_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], argv[3], argv[4], argv[5]);
}

DEFUN (ipv6_route_ifname_pref,
       ipv6_route_ifname_pref_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, NULL, argv[3],
                           NULL);
}

DEFUN (ipv6_route_ifname_pref_tag,
       ipv6_route_ifname_pref_tag_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, argv[3], argv[4], NULL);
}

DEFUN (ipv6_route_ifname_pref_tag_vrf,
       ipv6_route_ifname_pref_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, argv[3], argv[4], argv[5]);
}

DEFUN (ipv6_route_ifname_flags_pref,
       ipv6_route_ifname_flags_pref_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], NULL, argv[4],
                           NULL);
}

DEFUN (ipv6_route_ifname_flags_pref_tag,
       ipv6_route_ifname_flags_pref_tag_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535> <1-255>",
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], NULL);
}

DEFUN (ipv6_route_ifname_flags_pref_tag_vrf,
       ipv6_route_ifname_flags_pref_tag_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
}

DEFUN (no_ipv6_route,
       no_ipv6_route_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, NULL, NULL,
                           NULL);
}

DEFUN (no_ipv6_route_tag,
       no_ipv6_route_tag_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, argv[2], NULL, NULL);
}

DEFUN (no_ipv6_route_tag_vrf,
       no_ipv6_route_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, argv[2], NULL, argv[3]);
}

ALIAS (no_ipv6_route,
       no_ipv6_route_flags_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")

ALIAS (no_ipv6_route_tag,
       no_ipv6_route_flags_tag_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")

DEFUN (no_ipv6_route_ifname,
       no_ipv6_route_ifname_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, NULL, NULL,
                           NULL);
}

DEFUN (no_ipv6_route_ifname_tag,
       no_ipv6_route_ifname_tag_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, argv[3], NULL, NULL);
}

DEFUN (no_ipv6_route_ifname_tag_vrf,
       no_ipv6_route_ifname_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, argv[3], NULL, argv[4]);
}

ALIAS (no_ipv6_route_ifname,
       no_ipv6_route_ifname_flags_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole)",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n")

ALIAS (no_ipv6_route_ifname_tag,
       no_ipv6_route_ifname_flags_tag_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n")

DEFUN (no_ipv6_route_pref,
       no_ipv6_route_pref_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, NULL, argv[2],
                           NULL);
}

DEFUN (no_ipv6_route_pref_tag,
       no_ipv6_route_pref_tag_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, argv[2], argv[3], NULL);
}

DEFUN (no_ipv6_route_pref_tag_vrf,
       no_ipv6_route_pref_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, argv[2], argv[3], argv[4]);
}

DEFUN (no_ipv6_route_flags_pref,
       no_ipv6_route_flags_pref_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n")
{
  /* We do not care about argv[2] */
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, argv[2], NULL, argv[3],
                           NULL);
}

DEFUN (no_ipv6_route_flags_pref_tag,
       no_ipv6_route_flags_pref_tag_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  /* We do not care about argv[2] */
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, argv[2], argv[3], argv[4], NULL);
}

DEFUN (no_ipv6_route_flags_pref_tag_vrf,
       no_ipv6_route_flags_pref_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  /* We do not care about argv[2] */
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, argv[2], argv[3], argv[4], argv[5]);
}

DEFUN (no_ipv6_route_ifname_pref,
       no_ipv6_route_ifname_pref_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, NULL, argv[3],
                           NULL);
}

DEFUN (no_ipv6_route_ifname_pref_tag,
       no_ipv6_route_ifname_pref_tag_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, argv[3], argv[4], NULL);
}

DEFUN (no_ipv6_route_ifname_pref_tag_vrf,
       no_ipv6_route_ifname_pref_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, argv[3], argv[4], argv[5]);
}

DEFUN (no_ipv6_route_ifname_flags_pref,
       no_ipv6_route_ifname_flags_pref_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], argv[3], NULL, argv[4],
                           NULL);
}

DEFUN (no_ipv6_route_ifname_flags_pref_tag,
       no_ipv6_route_ifname_flags_pref_tag_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535> <1-255>",
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n")
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], NULL);
}

DEFUN (no_ipv6_route_ifname_flags_pref_tag_vrf,
       no_ipv6_route_ifname_flags_pref_tag_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) tag <1-65535> <1-255>" VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Set tag for this route\n"
       "Tag value\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);
}

DEFUN (ipv6_route_vrf,
       ipv6_route_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, NULL, NULL,
                           argv[2]);
}

DEFUN (ipv6_route_flags_vrf,
       ipv6_route_flags_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], NULL, NULL,
                           argv[3]);
}

DEFUN (ipv6_route_ifname_vrf,
       ipv6_route_ifname_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, NULL, NULL,
                           argv[3]);
}

DEFUN (ipv6_route_ifname_flags_vrf,
       ipv6_route_ifname_flags_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], NULL, NULL,
                           argv[4]);
}

DEFUN (ipv6_route_pref_vrf,
       ipv6_route_pref_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, NULL, NULL, argv[2],
                           argv[3]);
}

DEFUN (ipv6_route_flags_pref_vrf,
       ipv6_route_flags_pref_vrf_cmd,
       "ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], NULL, argv[2], NULL, argv[3],
                           argv[4]);
}

DEFUN (ipv6_route_ifname_pref_vrf,
       ipv6_route_ifname_pref_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], NULL, NULL, argv[3],
                           argv[4]);
}

DEFUN (ipv6_route_ifname_flags_pref_vrf,
       ipv6_route_ifname_flags_pref_vrf_cmd,
       "ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) <1-255> " VRF_CMD_STR,
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 1, argv[0], argv[1], argv[2], argv[3], NULL, argv[4],
                           argv[5]);
}

DEFUN (no_ipv6_route_vrf,
       no_ipv6_route_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, NULL, NULL,
                           (argc > 3) ? argv[3] : argv[2]);
}

ALIAS (no_ipv6_route_vrf,
       no_ipv6_route_flags_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)

DEFUN (no_ipv6_route_ifname_vrf,
       no_ipv6_route_ifname_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, NULL, NULL,
                           (argc > 4) ? argv[4] : argv[3]);
}

ALIAS (no_ipv6_route_ifname_vrf,
       no_ipv6_route_ifname_flags_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       VRF_CMD_HELP_STR)

DEFUN (no_ipv6_route_pref_vrf,
       no_ipv6_route_pref_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, NULL, NULL, argv[2],
                           argv[3]);
}

DEFUN (no_ipv6_route_flags_pref_vrf,
       no_ipv6_route_flags_pref_vrf_cmd,
       "no ipv6 route X:X::X:X/M (X:X::X:X|INTERFACE) (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  /* We do not care about argv[2] */
  return static_ipv6_func (vty, 0, argv[0], argv[1], NULL, argv[2], NULL, argv[3],
                           argv[4]);
}

DEFUN (no_ipv6_route_ifname_pref_vrf,
       no_ipv6_route_ifname_pref_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], NULL, NULL, argv[3],
                           argv[4]);
}

DEFUN (no_ipv6_route_ifname_flags_pref_vrf,
       no_ipv6_route_ifname_flags_pref_vrf_cmd,
       "no ipv6 route X:X::X:X/M X:X::X:X INTERFACE (reject|blackhole) <1-255> " VRF_CMD_STR,
       NO_STR
       IP_STR
       "Establish static routes\n"
       "IPv6 destination prefix (e.g. 3ffe:506::/32)\n"
       "IPv6 gateway address\n"
       "IPv6 gateway interface name\n"
       "Emit an ICMP unreachable when matched\n"
       "Silently discard pkts when matched\n"
       "Distance value for this prefix\n"
       VRF_CMD_HELP_STR)
{
  return static_ipv6_func (vty, 0, argv[0], argv[1], argv[2], argv[3], NULL, argv[4],
                           argv[5]);
}

DEFUN (show_ipv6_route,
       show_ipv6_route_cmd,
       "show ipv6 route",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show all IPv6 route. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
	if (first)
	  {
	    vty_out (vty, SHOW_ROUTE_V6_HEADER);
	    first = 0;
	  }
	vty_show_ip_route (vty, rn, rib);
      }
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route,
       show_ipv6_route_vrf_cmd,
       "show ipv6 route " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_tag,
       show_ipv6_route_tag_cmd,
       "show ipv6 route tag <1-65535>",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Show only routes with tag\n"
       "Tag value\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  route_tag_t tag = 0;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argv[0])
    tag = atoi(argv[0]);

  if (argc == 2)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show all IPv6 routes with matching tag value. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
        if (rib->tag != tag)
          continue;

	if (first)
	  {
	    vty_out (vty, SHOW_ROUTE_V6_HEADER);
	    first = 0;
	  }
	vty_show_ip_route (vty, rn, rib);
      }
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_tag,
       show_ipv6_route_tag_vrf_cmd,
       "show ipv6 route tag <1-65535>" VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Show only routes with tag\n"
       "Tag value\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_prefix_longer,
       show_ipv6_route_prefix_longer_cmd,
       "show ipv6 route X:X::X:X/M longer-prefixes",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n"
       "Show route matching the specified Network/Mask pair only\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct prefix p;
  int ret;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix (argv[0], &p);
  if (! ret)
    {
      vty_out (vty, "%% Malformed Prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show matched type IPv6 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      if (prefix_match (&p, &rn->p))
	{
	  if (first)
	    {
	      vty_out (vty, SHOW_ROUTE_V6_HEADER);
	      first = 0;
	    }
	  vty_show_ip_route (vty, rn, rib);
	}
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_prefix_longer,
       show_ipv6_route_prefix_longer_vrf_cmd,
       "show ipv6 route X:X::X:X/M longer-prefixes " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n"
       "Show route matching the specified Network/Mask pair only\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_protocol,
       show_ipv6_route_protocol_cmd,
       "show ipv6 route " QUAGGA_IP6_REDIST_STR_ZEBRA,
       SHOW_STR
       IP_STR
       "IP routing table\n"
	QUAGGA_IP6_REDIST_HELP_STR_ZEBRA)
{
  int type;
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  type = proto_redistnum (AFI_IP6, argv[0]);
  if (type < 0)
    {
      vty_out (vty, "Unknown route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show matched type IPv6 routes. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      if (rib->type == type)
	{
	  if (first)
	    {
	      vty_out (vty, SHOW_ROUTE_V6_HEADER);
	      first = 0;
	    }
	  vty_show_ip_route (vty, rn, rib);
	}
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_protocol,
       show_ipv6_route_protocol_vrf_cmd,
       "show ipv6 route " QUAGGA_IP6_REDIST_STR_ZEBRA " " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       QUAGGA_IP6_REDIST_HELP_STR_ZEBRA
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_addr,
       show_ipv6_route_addr_cmd,
       "show ipv6 route X:X::X:X",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 Address\n")
{
  int ret;
  struct prefix_ipv6 p;
  struct route_table *table;
  struct route_node *rn;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix_ipv6 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "Malformed IPv6 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  rn = route_node_match (table, (struct prefix *) &p);
  if (! rn)
    {
      vty_out (vty, "%% Network not in table%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  vty_show_ip_route_detail (vty, rn, 0);

  route_unlock_node (rn);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_addr,
       show_ipv6_route_addr_vrf_cmd,
       "show ipv6 route X:X::X:X " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 Address\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_prefix,
       show_ipv6_route_prefix_cmd,
       "show ipv6 route X:X::X:X/M",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n")
{
  int ret;
  struct prefix_ipv6 p;
  struct route_table *table;
  struct route_node *rn;
  vrf_id_t vrf_id = VRF_DEFAULT;

  ret = str2prefix_ipv6 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "Malformed IPv6 prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  rn = route_node_match (table, (struct prefix *) &p);
  if (! rn || rn->p.prefixlen != p.prefixlen)
    {
      vty_out (vty, "%% Network not in table%s", VTY_NEWLINE);
      if (rn)
        route_unlock_node (rn);
      return CMD_WARNING;
    }

  vty_show_ip_route_detail (vty, rn, 0);

  route_unlock_node (rn);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_prefix,
       show_ipv6_route_prefix_vrf_cmd,
       "show ipv6 route X:X::X:X/M " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n"
       VRF_CMD_HELP_STR)

/* Show route summary.  */
DEFUN (show_ipv6_route_summary,
       show_ipv6_route_summary_cmd,
       "show ipv6 route summary",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n")
{
  struct route_table *table;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  vty_show_ip_route_summary (vty, table);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_summary,
       show_ipv6_route_summary_vrf_cmd,
       "show ipv6 route summary " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n"
       VRF_CMD_HELP_STR)

/* Show ipv6 route summary prefix.  */
DEFUN (show_ipv6_route_summary_prefix,
       show_ipv6_route_summary_prefix_cmd,
       "show ipv6 route summary prefix",
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n"
       "Prefix routes\n")
{
  struct route_table *table;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP6, SAFI_UNICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  vty_show_ip_route_summary_prefix (vty, table);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_route_summary_prefix,
       show_ipv6_route_summary_prefix_vrf_cmd,
       "show ipv6 route summary prefix " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n"
       "Prefix routes\n"
       VRF_CMD_HELP_STR)

/*
 * Show IPv6 mroute command.Used to dump
 * the Multicast routing table.
 */

DEFUN (show_ipv6_mroute,
       show_ipv6_mroute_cmd,
       "show ipv6 mroute",
       SHOW_STR
       IP_STR
       "IPv6 Multicast routing table\n")
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  int first = 1;
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  table = zebra_vrf_table (AFI_IP6, SAFI_MULTICAST, vrf_id);
  if (! table)
    return CMD_SUCCESS;

  /* Show all IPv6 route. */
  for (rn = route_top (table); rn; rn = route_next (rn))
    RNODE_FOREACH_RIB (rn, rib)
      {
       if (first)
         {
	   vty_out (vty, SHOW_ROUTE_V6_HEADER);
           first = 0;
         }
       vty_show_ip_route (vty, rn, rib);
      }
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_mroute,
       show_ipv6_mroute_vrf_cmd,
       "show ipv6 mroute " VRF_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 Multicast routing table\n"
       VRF_CMD_HELP_STR)

DEFUN (show_ipv6_route_vrf_all,
       show_ipv6_route_vrf_all_cmd,
       "show ipv6 route " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int first = 1;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      /* Show all IPv6 route. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          {
            if (first)
              {
                vty_out (vty, SHOW_ROUTE_V6_HEADER);
                first = 0;
              }
            vty_show_ip_route (vty, rn, rib);
          }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_route_prefix_longer_vrf_all,
       show_ipv6_route_prefix_longer_vrf_all_cmd,
       "show ipv6 route X:X::X:X/M longer-prefixes " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n"
       "Show route matching the specified Network/Mask pair only\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct prefix p;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int ret;
  int first = 1;

  ret = str2prefix (argv[0], &p);
  if (! ret)
    {
      vty_out (vty, "%% Malformed Prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      /* Show matched type IPv6 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          if (prefix_match (&p, &rn->p))
            {
              if (first)
                {
                  vty_out (vty, SHOW_ROUTE_V6_HEADER);
                  first = 0;
                }
              vty_show_ip_route (vty, rn, rib);
            }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_route_protocol_vrf_all,
       show_ipv6_route_protocol_vrf_all_cmd,
       "show ipv6 route " QUAGGA_IP6_REDIST_STR_ZEBRA " " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IP routing table\n"
       QUAGGA_IP6_REDIST_HELP_STR_ZEBRA
       VRF_ALL_CMD_HELP_STR)
{
  int type;
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int first = 1;

  type = proto_redistnum (AFI_IP6, argv[0]);
  if (type < 0)
    {
      vty_out (vty, "Unknown route type%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      /* Show matched type IPv6 routes. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          if (rib->type == type)
            {
              if (first)
                {
                  vty_out (vty, SHOW_ROUTE_V6_HEADER);
                  first = 0;
                }
              vty_show_ip_route (vty, rn, rib);
            }
    }

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_route_addr_vrf_all,
       show_ipv6_route_addr_vrf_all_cmd,
       "show ipv6 route X:X::X:X " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 Address\n"
       VRF_ALL_CMD_HELP_STR)
{
  int ret;
  struct prefix_ipv6 p;
  struct route_table *table;
  struct route_node *rn;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  ret = str2prefix_ipv6 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "Malformed IPv6 address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      rn = route_node_match (table, (struct prefix *) &p);
      if (! rn)
        continue;

      vty_show_ip_route_detail (vty, rn, 0);

      route_unlock_node (rn);
    }

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_route_prefix_vrf_all,
       show_ipv6_route_prefix_vrf_all_cmd,
       "show ipv6 route X:X::X:X/M " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "IPv6 prefix\n"
       VRF_ALL_CMD_HELP_STR)
{
  int ret;
  struct prefix_ipv6 p;
  struct route_table *table;
  struct route_node *rn;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  ret = str2prefix_ipv6 (argv[0], &p);
  if (ret <= 0)
    {
      vty_out (vty, "Malformed IPv6 prefix%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      rn = route_node_match (table, (struct prefix *) &p);
      if (! rn)
        continue;
      if (rn->p.prefixlen != p.prefixlen)
        {
          route_unlock_node (rn);
          continue;
        }

      vty_show_ip_route_detail (vty, rn, 0);

      route_unlock_node (rn);
    }

  return CMD_SUCCESS;
}

/* Show route summary.  */
DEFUN (show_ipv6_route_summary_vrf_all,
       show_ipv6_route_summary_vrf_all_cmd,
       "show ipv6 route summary " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    if ((zvrf = vrf_iter2info (iter)) != NULL)
      vty_show_ip_route_summary (vty, zvrf->table[AFI_IP6][SAFI_UNICAST]);

  return CMD_SUCCESS;
}

DEFUN (show_ipv6_mroute_vrf_all,
       show_ipv6_mroute_vrf_all_cmd,
       "show ipv6 mroute " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 Multicast routing table\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct route_table *table;
  struct route_node *rn;
  struct rib *rib;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;
  int first = 1;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (table = zvrf->table[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      /* Show all IPv6 route. */
      for (rn = route_top (table); rn; rn = route_next (rn))
        RNODE_FOREACH_RIB (rn, rib)
          {
           if (first)
             {
               vty_out (vty, SHOW_ROUTE_V6_HEADER);
               first = 0;
             }
           vty_show_ip_route (vty, rn, rib);
          }
    }
  return CMD_SUCCESS;
}

DEFUN (show_ipv6_route_summary_prefix_vrf_all,
       show_ipv6_route_summary_prefix_vrf_all_cmd,
       "show ipv6 route summary prefix " VRF_ALL_CMD_STR,
       SHOW_STR
       IP_STR
       "IPv6 routing table\n"
       "Summary of all IPv6 routes\n"
       "Prefix routes\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    if ((zvrf = vrf_iter2info (iter)) != NULL)
      vty_show_ip_route_summary_prefix (vty, zvrf->table[AFI_IP6][SAFI_UNICAST]);

  return CMD_SUCCESS;
}

/* Write IPv6 static route configuration. */
static int
static_config_ipv6 (struct vty *vty)
{
  struct route_node *rn;
  struct static_route *si;  
  int write;
  char buf[BUFSIZ];
  struct route_table *stable;
  struct zebra_vrf *zvrf;
  vrf_iter_t iter;

  write = 0;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      if ((zvrf = vrf_iter2info (iter)) == NULL ||
          (stable = zvrf->stable[AFI_IP6][SAFI_UNICAST]) == NULL)
        continue;

      for (rn = route_top (stable); rn; rn = route_next (rn))
        for (si = rn->info; si; si = si->next)
          {
            vty_out (vty, "ipv6 route %s", prefix2str (&rn->p, buf, sizeof buf));

            switch (si->type)
              {
              case STATIC_IPV6_GATEWAY:
                vty_out (vty, " %s",
                         inet_ntop (AF_INET6, &si->addr.ipv6, buf, BUFSIZ));
                break;
              case STATIC_IPV6_IFNAME:
                vty_out (vty, " %s", si->ifname);
                break;
              case STATIC_IPV6_GATEWAY_IFNAME:
                vty_out (vty, " %s %s",
                         inet_ntop (AF_INET6, &si->addr.ipv6, buf, BUFSIZ),
                         si->ifname);
                break;
              }

            if (CHECK_FLAG(si->flags, ZEBRA_FLAG_REJECT))
              vty_out (vty, " %s", "reject");

            if (CHECK_FLAG(si->flags, ZEBRA_FLAG_BLACKHOLE))
              vty_out (vty, " %s", "blackhole");

	    if (si->tag)
	      vty_out (vty, " tag %d", si->tag);

            if (si->distance != ZEBRA_STATIC_DISTANCE_DEFAULT)
              vty_out (vty, " %d", si->distance);

            if (si->vrf_id != VRF_DEFAULT)
              vty_out (vty, " vrf %u", si->vrf_id);

            vty_out (vty, "%s", VTY_NEWLINE);

            write = 1;
          }
    }
  return write;
}

/* Static ip route configuration write function. */
static int
zebra_ip_config (struct vty *vty)
{
  int write = 0;

  write += static_config_ipv4 (vty, SAFI_UNICAST, "ip route");
  write += static_config_ipv4 (vty, SAFI_MULTICAST, "ip mroute");
#ifdef HAVE_IPV6
  write += static_config_ipv6 (vty);
#endif /* HAVE_IPV6 */

  return write;
}

static int config_write_vty(struct vty *vty)
{
  int i;
  enum multicast_mode ipv4_multicast_mode = multicast_mode_ipv4_get ();

  if (ipv4_multicast_mode != MCAST_NO_CONFIG)
    vty_out (vty, "ip multicast rpf-lookup-mode %s%s",
             ipv4_multicast_mode == MCAST_URIB_ONLY ? "urib-only" :
             ipv4_multicast_mode == MCAST_MRIB_ONLY ? "mrib-only" :
             ipv4_multicast_mode == MCAST_MIX_MRIB_FIRST ? "mrib-then-urib" :
             ipv4_multicast_mode == MCAST_MIX_DISTANCE ? "lower-distance" :
             "longer-prefix",
             VTY_NEWLINE);

  for (i=0;i<ZEBRA_ROUTE_MAX;i++)
    {
      if (proto_rm[AFI_IP][i])
        vty_out (vty, "ip protocol %s route-map %s%s", zebra_route_string(i),
                 proto_rm[AFI_IP][i], VTY_NEWLINE);
    }
  if (proto_rm[AFI_IP][ZEBRA_ROUTE_MAX])
      vty_out (vty, "ip protocol %s route-map %s%s", "any",
               proto_rm[AFI_IP][ZEBRA_ROUTE_MAX], VTY_NEWLINE);

  return 1;
}   

/* table node for protocol filtering */
static struct cmd_node protocol_node = { PROTOCOL_NODE, "", 1 };

/* IP node for static routes. */
static struct cmd_node ip_node = { IP_NODE,  "",  1 };

/* Route VTY.  */
void
zebra_vty_init (void)
{
  install_node (&ip_node, zebra_ip_config);
  install_node (&protocol_node, config_write_vty);

  install_element (CONFIG_NODE, &ip_mroute_cmd);
  install_element (CONFIG_NODE, &ip_mroute_dist_cmd);
  install_element (CONFIG_NODE, &no_ip_mroute_cmd);
  install_element (CONFIG_NODE, &no_ip_mroute_dist_cmd);
  install_element (CONFIG_NODE, &ip_multicast_mode_cmd);
  install_element (CONFIG_NODE, &no_ip_multicast_mode_cmd);
  install_element (CONFIG_NODE, &no_ip_multicast_mode_noarg_cmd);
  install_element (CONFIG_NODE, &ip_protocol_cmd);
  install_element (CONFIG_NODE, &no_ip_protocol_cmd);
  install_element (VIEW_NODE, &show_ip_protocol_cmd);
  install_element (CONFIG_NODE, &ip_route_cmd);
  install_element (CONFIG_NODE, &ip_route_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags2_cmd);
  install_element (CONFIG_NODE, &ip_route_flags2_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_flags2_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags2_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags2_tag_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags2_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_cmd);
  install_element (CONFIG_NODE, &no_ip_route_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags2_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags2_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags2_tag_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags2_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_tag_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_distance2_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_distance2_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_tag_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_tag_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_distance_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_distance2_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_distance2_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_tag_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_tag_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_tag_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_distance2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_tag_distance2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_tag_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_tag_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_tag_distance_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_tag_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_distance2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_tag_distance2_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_tag_distance2_vrf_cmd);

  install_element (VIEW_NODE, &show_ip_route_cmd);
  install_element (VIEW_NODE, &show_ip_route_tag_cmd);
  install_element (VIEW_NODE, &show_ip_route_tag_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_nht_cmd);
  install_element (VIEW_NODE, &show_ipv6_nht_cmd);
  install_element (VIEW_NODE, &show_ip_route_addr_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_longer_cmd);
  install_element (VIEW_NODE, &show_ip_route_protocol_cmd);
  install_element (VIEW_NODE, &show_ip_route_supernets_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_prefix_cmd);

  install_element (VIEW_NODE, &show_ip_rpf_cmd);
  install_element (VIEW_NODE, &show_ip_rpf_addr_cmd);

  /* Commands for VRF */

  install_element (CONFIG_NODE, &ip_mroute_vrf_cmd);
  install_element (CONFIG_NODE, &ip_mroute_dist_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_mroute_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_mroute_dist_vrf_cmd);

  install_element (CONFIG_NODE, &ip_route_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags2_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags2_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_flags_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_distance_vrf_cmd);
  install_element (CONFIG_NODE, &ip_route_mask_flags_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_flags_distance2_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_distance_vrf_cmd);
  install_element (CONFIG_NODE, &no_ip_route_mask_flags_distance2_vrf_cmd);

  install_element (VIEW_NODE, &show_ip_route_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_addr_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_longer_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_protocol_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_supernets_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_prefix_vrf_cmd);

  install_element (VIEW_NODE, &show_ip_route_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_addr_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_prefix_longer_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_protocol_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_supernets_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_route_summary_prefix_vrf_all_cmd);

  install_element (VIEW_NODE, &show_ip_rpf_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_rpf_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ip_rpf_addr_vrf_cmd);
  install_element (VIEW_NODE, &show_ip_rpf_addr_vrf_all_cmd);

  install_element (CONFIG_NODE, &ipv6_route_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_cmd);
  install_element (CONFIG_NODE, &ipv6_route_pref_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_pref_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_pref_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_pref_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_pref_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_pref_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_pref_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_pref_cmd);
  install_element (CONFIG_NODE, &ipv6_route_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_pref_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_pref_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_pref_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_pref_tag_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_pref_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_pref_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_pref_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_pref_tag_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_pref_tag_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_pref_tag_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_tag_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_tag_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_prefix_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_protocol_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_addr_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_longer_cmd);

  install_element (VIEW_NODE, &show_ipv6_mroute_cmd);

  /* Commands for VRF */

  install_element (CONFIG_NODE, &ipv6_route_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_pref_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_flags_pref_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_pref_vrf_cmd);
  install_element (CONFIG_NODE, &ipv6_route_ifname_flags_pref_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_pref_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_flags_pref_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_pref_vrf_cmd);
  install_element (CONFIG_NODE, &no_ipv6_route_ifname_flags_pref_vrf_cmd);

  install_element (VIEW_NODE, &show_ipv6_route_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_prefix_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_protocol_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_addr_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_vrf_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_longer_vrf_cmd);

  install_element (VIEW_NODE, &show_ipv6_route_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_summary_prefix_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_protocol_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_addr_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_vrf_all_cmd);
  install_element (VIEW_NODE, &show_ipv6_route_prefix_longer_vrf_all_cmd);

  install_element (VIEW_NODE, &show_ipv6_mroute_vrf_cmd);

  install_element (VIEW_NODE, &show_ipv6_mroute_vrf_all_cmd);
  install_element (ENABLE_NODE, &show_ipv6_mroute_vrf_all_cmd);
}
