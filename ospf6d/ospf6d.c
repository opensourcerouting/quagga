/*
 * Copyright (C) 2003 Yasuhiro Ohara
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

#include "thread.h"
#include "linklist.h"
#include "vty.h"
#include "command.h"

#include "ospf6_proto.h"
#include "ospf6_network.h"
#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_message.h"
#include "ospf6_route.h"
#include "ospf6_zebra.h"
#include "ospf6_spf.h"
#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_interface.h"
#include "ospf6_neighbor.h"
#include "ospf6_intra.h"
#include "ospf6_asbr.h"
#include "ospf6_abr.h"
#include "ospf6_flood.h"
#include "ospf6d.h"

#ifdef HAVE_SNMP
#include "ospf6_snmp.h"
#endif /*HAVE_SNMP*/

const char ospf6_daemon_version[] = OSPF6_DAEMON_VERSION;

extern vector ospf6_lsa_handler_vector;

struct route_node *
route_prev (struct route_node *node)
{
  struct route_node *end;
  struct route_node *prev = NULL;

  end = node;
  node = node->parent;
  if (node)
    route_lock_node (node);
  while (node)
    {
      prev = node;
      node = route_next (node);
      if (node == end)
        {
          route_unlock_node (node);
          node = NULL;
        }
    }
  route_unlock_node (end);
  if (prev)
    route_lock_node (prev);

  return prev;
}


/* show database functions */
DEFUN (show_version_ospf6,
       show_version_ospf6_cmd,
       "show version ospf6",
       SHOW_STR
       "Displays ospf6d version\n"
      )
{
  vty_out (vty, "Zebra OSPF6d Version: %s%s",
           ospf6_daemon_version, VNL);

  return CMD_SUCCESS;
}


DEFUN (show_debugging_ospf6,
       show_debugging_ospf6_cmd,
       "show debugging ospf6",
       SHOW_STR
       DEBUG_STR
       OSPF6_STR)
{
  int i;
  char buf[INET6_ADDRSTRLEN];

  vty_out (vty, "OSPF6 debugging status:%s", VNL);

  if (IS_OSPF6_DEBUG_ABR)
    vty_out (vty, "  OSPF6 ABR debugging is on%s", VNL);

  if (IS_OSPF6_DEBUG_ASBR)
    vty_out (vty, "  OSPF6 ASBR debugging is on%s", VNL);

  if (IS_OSPF6_DEBUG_INTERFACE)
    vty_out (vty, "  OSPF6 Interface debugging is on%s", VNL);

  if (IS_OSPF6_DEBUG_FLOODING)
    vty_out (vty, "  OSPF6 flooding debugging is on%s", VNL);

  /* show packet debugging */
  for (i = 0; i < 6; i++)
    {
      if (IS_OSPF6_DEBUG_MESSAGE (i, SEND) && IS_OSPF6_DEBUG_MESSAGE (i, RECV))
	vty_out (vty, "  OSPF6 packet %s debugging is on%s",
		 ospf6_packet_type_str[i], VNL);

      else if (IS_OSPF6_DEBUG_MESSAGE (i, SEND))
	vty_out (vty, "  OSPF6 packet %s send debugging is on%s",
		 ospf6_packet_type_str[i], VNL);
      else if (IS_OSPF6_DEBUG_MESSAGE (i, RECV))
	vty_out (vty, "  OSPF6 packet %s receive debugging is on%s",
		 ospf6_packet_type_str[i], VNL);
    }

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    vty_out (vty, "  OSPF6 SPF process debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_SPF (TIME))
    vty_out (vty, "  OSPF6 SPF time debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_SPF (DATABASE))
    vty_out (vty, "  OSPF6 SPF database debugging is on%s", VNL);

  if (IS_OSPF6_DEBUG_ROUTE (TABLE))
    vty_out (vty, "  OSPF6 route table debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_ROUTE (INTRA))
    vty_out (vty, "  OSPF6 route intra-area debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_ROUTE (INTER))
    vty_out (vty, "  OSPF6 route inter-area debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_ROUTE (MEMORY))
    vty_out (vty, "  OSPF6 route memory debugging is on%s", VNL);
  

  /* Show neighbor debugging */
  if (IS_OSPF6_DEBUG_NEIGHBOR (STATE) && IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    vty_out (vty, "  OSPF6 neighbor debugging is on%s", VNL);
  else if (IS_OSPF6_DEBUG_NEIGHBOR (STATE))
    vty_out (vty, "  OSPF6 neighbor state debugging is on%s", VNL);
  else if (IS_OSPF6_DEBUG_NEIGHBOR (EVENT))
    vty_out (vty, "  OSPF6 neighbor event debugging is on%s", VNL);

  /* Show debug status for OSPF LSAs. */
  for (i = 0; i < vector_active (ospf6_lsa_handler_vector); i++)
    {
      const struct ospf6_lsa_handler *handler
	= vector_slot (ospf6_lsa_handler_vector, i);
      if (handler == NULL)
        continue;

      if (CHECK_FLAG (handler->debug, OSPF6_LSA_DEBUG))
        vty_out (vty, "  OSPF6 LSA %s debugging is on%s",
                 ospf6_lsa_handler_name (handler), VNL);
      if (CHECK_FLAG (handler->debug, OSPF6_LSA_DEBUG_ORIGINATE))
	vty_out (vty, "  OSPF6 LSA %s origination debugging is on%s",
                 ospf6_lsa_handler_name (handler), VNL);
      if (CHECK_FLAG (handler->debug, OSPF6_LSA_DEBUG_EXAMIN))
	vty_out (vty, "  OSPF6 LSA %s examination debugging is on%s",
		 ospf6_lsa_handler_name (handler), VNL);
      if (CHECK_FLAG (handler->debug, OSPF6_LSA_DEBUG_FLOOD))
        vty_out (vty, "  OSPF6 LSA %s flooding debugging is on%s",
                 ospf6_lsa_handler_name (handler), VNL);
    }

  /* Show debug status for brouter */
  if (IS_OSPF6_DEBUG_BROUTER)
    vty_out (vty, "  OSPF6 border-router debugging is on%s", VNL);
  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_ROUTER)
    vty_out (vty, "  OSPF6 border-routers router-id %s debugging is on%s",
	     inet_ntop (AF_INET, &conf_debug_ospf6_brouter_specific_router_id,
			buf, sizeof (buf)), VNL);
  if (IS_OSPF6_DEBUG_BROUTER_SPECIFIC_AREA)
    vty_out (vty, "  OSPF6 border-routers area-id %s debugging is on%s",
	     inet_ntop (AF_INET, &conf_debug_ospf6_brouter_specific_area_id,
			buf, sizeof (buf)), VNL);


  /* Show debug status for Zebra. */
  if (IS_OSPF6_DEBUG_ZEBRA (SEND) && IS_OSPF6_DEBUG_ZEBRA (RECV))
    vty_out (vty, "  OSPF6 Zebra debugging is on%s", VNL);
  else if (IS_OSPF6_DEBUG_ZEBRA (SEND))
    vty_out (vty, "  OSPF6 Zebra send debugging is on%s", VNL);
  else if (IS_OSPF6_DEBUG_ZEBRA (RECV))
    vty_out (vty, "  OSPF6 Zebra recv debugging is on%s", VNL);

  return CMD_SUCCESS;
}

static struct cmd_node debug_node =
{
  DEBUG_NODE,
  "",
  1 /* VTYSH */
};

static int
config_write_ospf6_debug (struct vty *vty)
{
  config_write_ospf6_debug_message (vty);
  config_write_ospf6_debug_lsa (vty);
  config_write_ospf6_debug_zebra (vty);
  config_write_ospf6_debug_interface (vty);
  config_write_ospf6_debug_neighbor (vty);
  config_write_ospf6_debug_spf (vty);
  config_write_ospf6_debug_route (vty);
  config_write_ospf6_debug_brouter (vty);
  config_write_ospf6_debug_asbr (vty);
  config_write_ospf6_debug_abr (vty);
  config_write_ospf6_debug_flood (vty);
  vty_out (vty, "!%s", VNL);
  return 0;
}

#define AREA_LSDB_TITLE_FORMAT \
  "%s        Area Scoped Link State Database (Area %s)%s%s"
#define IF_LSDB_TITLE_FORMAT \
  "%s        I/F Scoped Link State Database (I/F %s in Area %s)%s%s"
#define AS_LSDB_TITLE_FORMAT \
  "%s        AS Scoped Link State Database%s%s"

static int
parse_show_level (int argc, const char *argv[])
{
  int level = 0;
  if (argc)
    {
      if (! strncmp (argv[0], "de", 2))
        level = OSPF6_LSDB_SHOW_LEVEL_DETAIL;
      else if (! strncmp (argv[0], "du", 2))
        level = OSPF6_LSDB_SHOW_LEVEL_DUMP;
      else if (! strncmp (argv[0], "in", 2))
        level = OSPF6_LSDB_SHOW_LEVEL_INTERNAL;
    }
  else
    level = OSPF6_LSDB_SHOW_LEVEL_NORMAL;
  return level;
}

static u_int16_t
parse_type_spec (int argc, const char *argv[])
{
  u_int16_t type = 0;
  assert (argc);
  if (! strcmp (argv[0], "router"))
    type = htons (OSPF6_LSTYPE_ROUTER);
  else if (! strcmp (argv[0], "network"))
    type = htons (OSPF6_LSTYPE_NETWORK);
  else if (! strcmp (argv[0], "as-external"))
    type = htons (OSPF6_LSTYPE_AS_EXTERNAL);
  else if (! strcmp (argv[0], "intra-prefix"))
    type = htons (OSPF6_LSTYPE_INTRA_PREFIX);
  else if (! strcmp (argv[0], "inter-router"))
    type = htons (OSPF6_LSTYPE_INTER_ROUTER);
  else if (! strcmp (argv[0], "inter-prefix"))
    type = htons (OSPF6_LSTYPE_INTER_PREFIX);
  else if (! strcmp (argv[0], "link"))
    type = htons (OSPF6_LSTYPE_LINK);
  return type;
}

DEFUN (show_ipv6_ospf6_database,
       show_ipv6_ospf6_database_cmd,
       "show ipv6 ospf6 database",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;

  OSPF6_CMD_CHECK_RUNNING ();

  level = parse_show_level (argc, argv);

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, NULL, NULL, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, NULL, NULL, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, NULL, NULL, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database,
       show_ipv6_ospf6_database_detail_cmd,
       "show ipv6 ospf6 database (detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type,
       show_ipv6_ospf6_database_type_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, NULL, NULL, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, NULL, NULL, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, NULL, NULL, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type,
       show_ipv6_ospf6_database_type_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_id,
       show_ipv6_ospf6_database_id_cmd,
       "show ipv6 ospf6 database * A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Specify Link state ID as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int32_t id = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link State ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, &id, NULL, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, &id, NULL, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, &id, NULL, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_id,
       show_ipv6_ospf6_database_id_detail_cmd,
       "show ipv6 ospf6 database * A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

ALIAS (show_ipv6_ospf6_database_id,
       show_ipv6_ospf6_database_linkstate_id_cmd,
       "show ipv6 ospf6 database linkstate-id A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
      )

ALIAS (show_ipv6_ospf6_database_id,
       show_ipv6_ospf6_database_linkstate_id_detail_cmd,
       "show ipv6 ospf6 database linkstate-id A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_router,
       show_ipv6_ospf6_database_router_cmd,
       "show ipv6 ospf6 database * * A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Any Link state ID\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_router,
       show_ipv6_ospf6_database_router_detail_cmd,
       "show ipv6 ospf6 database * * A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Any Link state ID\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

ALIAS (show_ipv6_ospf6_database_router,
       show_ipv6_ospf6_database_adv_router_cmd,
       "show ipv6 ospf6 database adv-router A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )

ALIAS (show_ipv6_ospf6_database_router,
       show_ipv6_ospf6_database_adv_router_detail_cmd,
       "show ipv6 ospf6 database adv-router A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_id,
       show_ipv6_ospf6_database_type_id_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Specify Link state ID as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t id = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link state ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, &id, NULL, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, &id, NULL, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, &id, NULL, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_id,
       show_ipv6_ospf6_database_type_id_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

ALIAS (show_ipv6_ospf6_database_type_id,
       show_ipv6_ospf6_database_type_linkstate_id_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) linkstate-id A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
      )

ALIAS (show_ipv6_ospf6_database_type_id,
       show_ipv6_ospf6_database_type_linkstate_id_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) linkstate-id A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_router,
       show_ipv6_ospf6_database_type_router_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) * A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Any Link state ID\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_router,
       show_ipv6_ospf6_database_type_router_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) * A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Any Link state ID\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

ALIAS (show_ipv6_ospf6_database_type_router,
       show_ipv6_ospf6_database_type_adv_router_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) adv-router A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )

ALIAS (show_ipv6_ospf6_database_type_router,
       show_ipv6_ospf6_database_type_adv_router_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) adv-router A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_id_router,
       show_ipv6_ospf6_database_id_router_cmd,
       "show ipv6 ospf6 database * A.B.C.D A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int32_t id = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link state ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_id_router,
       show_ipv6_ospf6_database_id_router_detail_cmd,
       "show ipv6 ospf6 database * A.B.C.D A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Any Link state Type\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_adv_router_linkstate_id,
       show_ipv6_ospf6_database_adv_router_linkstate_id_cmd,
       "show ipv6 ospf6 database adv-router A.B.C.D linkstate-id A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int32_t id = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link state ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, &id, &adv_router, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_adv_router_linkstate_id,
       show_ipv6_ospf6_database_adv_router_linkstate_id_detail_cmd,
       "show ipv6 ospf6 database adv-router A.B.C.D linkstate-id A.B.C.D "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_id_router,
       show_ipv6_ospf6_database_type_id_router_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Specify Advertising Router as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t id = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link state ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, &id, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_id_router,
       show_ipv6_ospf6_database_type_id_router_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D A.B.C.D "
       "(dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_adv_router_linkstate_id,
       show_ipv6_ospf6_database_type_adv_router_linkstate_id_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) "
       "adv-router A.B.C.D linkstate-id A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t id = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
    {
      vty_out (vty, "Advertising Router is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link state ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, &id, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_adv_router_linkstate_id,
       show_ipv6_ospf6_database_type_adv_router_linkstate_id_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) "
       "adv-router A.B.C.D linkstate-id A.B.C.D "
       "(dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Search by Advertising Router\n"
       "Specify Advertising Router as IPv4 address notation\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_self_originated,
       show_ipv6_ospf6_database_self_originated_cmd,
       "show ipv6 ospf6 database self-originated",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Self-originated LSAs\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  level = parse_show_level (argc, argv);

  adv_router = o->router_id;

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
      ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, oa->lsdb);
    }

  for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
    {
      for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
        {
          vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                   oi->interface->name, oa->name, VNL, VNL);
          ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, oi->lsdb);
        }
    }

  vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
  ospf6_lsdb_show (vty, level, NULL, NULL, &adv_router, o->lsdb);

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_self_originated,
       show_ipv6_ospf6_database_self_originated_detail_cmd,
       "show ipv6 ospf6 database self-originated "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Self-originated LSAs\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_self_originated,
       show_ipv6_ospf6_database_type_self_originated_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) self-originated",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display Self-originated LSAs\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t adv_router = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  adv_router = o->router_id;

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, NULL, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_self_originated,
       show_ipv6_ospf6_database_type_self_originated_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) self-originated "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display Self-originated LSAs\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_self_originated_linkstate_id,
       show_ipv6_ospf6_database_type_self_originated_linkstate_id_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) self-originated "
       "linkstate-id A.B.C.D",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display Self-originated LSAs\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t adv_router = 0;
  u_int32_t id = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link State ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  adv_router = o->router_id;

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, &id, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_self_originated_linkstate_id,
       show_ipv6_ospf6_database_type_self_originated_linkstate_id_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) self-originated "
       "linkstate-id A.B.C.D (detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display Self-originated LSAs\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )

DEFUN (show_ipv6_ospf6_database_type_id_self_originated,
       show_ipv6_ospf6_database_type_id_self_originated_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D self-originated",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display Self-originated LSAs\n"
      )
{
  int level;
  struct listnode *i, *j;
  struct ospf6 *o = ospf6;
  struct ospf6_area *oa;
  struct ospf6_interface *oi;
  u_int16_t type = 0;
  u_int32_t adv_router = 0;
  u_int32_t id = 0;

  OSPF6_CMD_CHECK_RUNNING ();

  type = parse_type_spec (argc, argv);
  argc--;
  argv++;

  if ((inet_pton (AF_INET, argv[0], &id)) != 1)
    {
      vty_out (vty, "Link State ID is not parsable: %s%s",
               argv[0], VNL);
      return CMD_SUCCESS;
    }

  argc--;
  argv++;
  level = parse_show_level (argc, argv);

  adv_router = o->router_id;

  switch (OSPF6_LSA_SCOPE (type))
    {
      case OSPF6_SCOPE_AREA:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            vty_out (vty, AREA_LSDB_TITLE_FORMAT, VNL, oa->name, VNL, VNL);
            ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oa->lsdb);
          }
        break;

      case OSPF6_SCOPE_LINKLOCAL:
        for (ALL_LIST_ELEMENTS_RO (o->area_list, i, oa))
          {
            for (ALL_LIST_ELEMENTS_RO (oa->if_list, j, oi))
              {
                vty_out (vty, IF_LSDB_TITLE_FORMAT, VNL,
                         oi->interface->name, oa->name, VNL, VNL);
                ospf6_lsdb_show (vty, level, &type, &id, &adv_router, oi->lsdb);
              }
          }
        break;

      case OSPF6_SCOPE_AS:
        vty_out (vty, AS_LSDB_TITLE_FORMAT, VNL, VNL, VNL);
        ospf6_lsdb_show (vty, level, &type, &id, &adv_router, o->lsdb);
        break;

      default:
        assert (0);
        break;
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_database_type_id_self_originated,
       show_ipv6_ospf6_database_type_id_self_originated_detail_cmd,
       "show ipv6 ospf6 database "
       "(router|network|inter-prefix|inter-router|as-external|"
       "group-membership|type-7|link|intra-prefix) A.B.C.D self-originated "
       "(detail|dump|internal)",
       SHOW_STR
       IPV6_STR
       OSPF6_STR
       "Display Link state database\n"
       "Display Router LSAs\n"
       "Display Network LSAs\n"
       "Display Inter-Area-Prefix LSAs\n"
       "Display Inter-Area-Router LSAs\n"
       "Display As-External LSAs\n"
       "Display Group-Membership LSAs\n"
       "Display Type-7 LSAs\n"
       "Display Link LSAs\n"
       "Display Intra-Area-Prefix LSAs\n"
       "Display Self-originated LSAs\n"
       "Search by Link state ID\n"
       "Specify Link state ID as IPv4 address notation\n"
       "Display details of LSAs\n"
       "Dump LSAs\n"
       "Display LSA's internal information\n"
      )


DEFUN (show_ipv6_ospf6_border_routers,
       show_ipv6_ospf6_border_routers_cmd,
       "show ipv6 ospf6 border-routers",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display routing table for ABR and ASBR\n"
      )
{
  u_int32_t adv_router;
  void (*showfunc) (struct vty *, struct ospf6_route *);
  struct ospf6_route *ro;
  struct prefix prefix;

  OSPF6_CMD_CHECK_RUNNING ();

  if (argc && ! strcmp ("detail", argv[0]))
    {
      showfunc = ospf6_route_show_detail;
      argc--;
      argv++;
    }
  else
    showfunc = ospf6_brouter_show;

  if (argc)
    {
      if ((inet_pton (AF_INET, argv[0], &adv_router)) != 1)
        {
          vty_out (vty, "Router ID is not parsable: %s%s", argv[0], VNL);
          return CMD_SUCCESS;
        }

      ospf6_linkstate_prefix (adv_router, 0, &prefix);
      ro = ospf6_route_lookup (&prefix, ospf6->brouter_table);
      if (!ro)
        {
          vty_out (vty, "No Route found for Router ID: %s%s", argv[0], VNL);
          return CMD_SUCCESS;
        }

      ospf6_route_show_detail (vty, ro);
      return CMD_SUCCESS;
    }

  if (showfunc == ospf6_brouter_show)
    ospf6_brouter_show_header (vty);

  for (ro = ospf6_route_head (ospf6->brouter_table); ro;
       ro = ospf6_route_next (ro))
    (*showfunc) (vty, ro);

  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_border_routers,
       show_ipv6_ospf6_border_routers_detail_cmd,
       "show ipv6 ospf6 border-routers (A.B.C.D|detail)",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display routing table for ABR and ASBR\n"
       "Specify Router-ID\n"
       "Display Detail\n"
      )

DEFUN (show_ipv6_ospf6_linkstate,
       show_ipv6_ospf6_linkstate_cmd,
       "show ipv6 ospf6 linkstate",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display linkstate routing table\n"
      )
{
  struct listnode *node;
  struct ospf6_area *oa;

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, node, oa))
    {
      vty_out (vty, "%s        SPF Result in Area %s%s%s",
               VNL, oa->name, VNL, VNL);
      ospf6_linkstate_table_show (vty, argc, argv, oa->spf_table);
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

ALIAS (show_ipv6_ospf6_linkstate,
       show_ipv6_ospf6_linkstate_router_cmd,
       "show ipv6 ospf6 linkstate router A.B.C.D",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display linkstate routing table\n"
       "Display Router Entry\n"
       "Specify Router ID as IPv4 address notation\n"
      )

ALIAS (show_ipv6_ospf6_linkstate,
       show_ipv6_ospf6_linkstate_network_cmd,
       "show ipv6 ospf6 linkstate network A.B.C.D A.B.C.D",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display linkstate routing table\n"
       "Display Network Entry\n"
       "Specify Router ID as IPv4 address notation\n"
       "Specify Link state ID as IPv4 address notation\n"
      )

DEFUN (show_ipv6_ospf6_linkstate_detail,
       show_ipv6_ospf6_linkstate_detail_cmd,
       "show ipv6 ospf6 linkstate detail",
       SHOW_STR
       IP6_STR
       OSPF6_STR
       "Display linkstate routing table\n"
      )
{
  const char *sargv[CMD_ARGC_MAX];
  int i, sargc;
  struct listnode *node;
  struct ospf6_area *oa;

  /* copy argv to sargv and then append "detail" */
  for (i = 0; i < argc; i++)
    sargv[i] = argv[i];
  sargc = argc;
  sargv[sargc++] = "detail";
  sargv[sargc] = NULL;

  for (ALL_LIST_ELEMENTS_RO (ospf6->area_list, node, oa))
    {
      vty_out (vty, "%s        SPF Result in Area %s%s%s",
               VNL, oa->name, VNL, VNL);
      ospf6_linkstate_table_show (vty, sargc, sargv, oa->spf_table);
    }

  vty_out (vty, "%s", VNL);
  return CMD_SUCCESS;
}

/* Install ospf related commands. */
void
ospf6_init (void)
{
  ospf6_top_init ();
  ospf6_area_init ();
  ospf6_interface_init ();
  ospf6_neighbor_init ();
  ospf6_zebra_init ();

  ospf6_lsa_init ();
  ospf6_spf_init ();
  ospf6_intra_init ();
  ospf6_asbr_init ();
  ospf6_abr_init ();

#ifdef HAVE_SNMP
  ospf6_snmp_init (master);
#endif /*HAVE_SNMP*/

  install_node (&debug_node, config_write_ospf6_debug);

  install_element (ENABLE_NODE, &show_debugging_ospf6_cmd);
  install_element_ospf6_debug_message ();
  install_element_ospf6_debug_lsa ();
  install_element_ospf6_debug_interface ();
  install_element_ospf6_debug_neighbor ();
  install_element_ospf6_debug_zebra ();
  install_element_ospf6_debug_spf ();
  install_element_ospf6_debug_route ();
  install_element_ospf6_debug_brouter ();
  install_element_ospf6_debug_asbr ();
  install_element_ospf6_debug_abr ();
  install_element_ospf6_debug_flood ();

  install_element (VIEW_NODE, &show_version_ospf6_cmd);
  install_element (ENABLE_NODE, &show_version_ospf6_cmd);

  install_element (VIEW_NODE, &show_ipv6_ospf6_border_routers_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_border_routers_detail_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_border_routers_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_border_routers_detail_cmd);

  install_element (VIEW_NODE, &show_ipv6_ospf6_linkstate_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_linkstate_router_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_linkstate_network_cmd);
  install_element (VIEW_NODE, &show_ipv6_ospf6_linkstate_detail_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_linkstate_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_linkstate_router_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_linkstate_network_cmd);
  install_element (ENABLE_NODE, &show_ipv6_ospf6_linkstate_detail_cmd);

#define INSTALL(n,c) \
  install_element (n ## _NODE, &show_ipv6_ospf6_ ## c)

  INSTALL (VIEW, database_cmd);
  INSTALL (VIEW, database_detail_cmd);
  INSTALL (VIEW, database_type_cmd);
  INSTALL (VIEW, database_type_detail_cmd);
  INSTALL (VIEW, database_id_cmd);
  INSTALL (VIEW, database_id_detail_cmd);
  INSTALL (VIEW, database_linkstate_id_cmd);
  INSTALL (VIEW, database_linkstate_id_detail_cmd);
  INSTALL (VIEW, database_router_cmd);
  INSTALL (VIEW, database_router_detail_cmd);
  INSTALL (VIEW, database_adv_router_cmd);
  INSTALL (VIEW, database_adv_router_detail_cmd);
  INSTALL (VIEW, database_type_id_cmd);
  INSTALL (VIEW, database_type_id_detail_cmd);
  INSTALL (VIEW, database_type_linkstate_id_cmd);
  INSTALL (VIEW, database_type_linkstate_id_detail_cmd);
  INSTALL (VIEW, database_type_router_cmd);
  INSTALL (VIEW, database_type_router_detail_cmd);
  INSTALL (VIEW, database_type_adv_router_cmd);
  INSTALL (VIEW, database_type_adv_router_detail_cmd);
  INSTALL (VIEW, database_adv_router_linkstate_id_cmd);
  INSTALL (VIEW, database_adv_router_linkstate_id_detail_cmd);
  INSTALL (VIEW, database_id_router_cmd);
  INSTALL (VIEW, database_id_router_detail_cmd);
  INSTALL (VIEW, database_type_id_router_cmd);
  INSTALL (VIEW, database_type_id_router_detail_cmd);
  INSTALL (VIEW, database_type_adv_router_linkstate_id_cmd);
  INSTALL (VIEW, database_type_adv_router_linkstate_id_detail_cmd);
  INSTALL (VIEW, database_self_originated_cmd);
  INSTALL (VIEW, database_self_originated_detail_cmd);
  INSTALL (VIEW, database_type_self_originated_cmd);
  INSTALL (VIEW, database_type_self_originated_detail_cmd);
  INSTALL (VIEW, database_type_id_self_originated_cmd);
  INSTALL (VIEW, database_type_id_self_originated_detail_cmd);
  INSTALL (VIEW, database_type_self_originated_linkstate_id_cmd);
  INSTALL (VIEW, database_type_self_originated_linkstate_id_detail_cmd);

  INSTALL (ENABLE, database_cmd);
  INSTALL (ENABLE, database_detail_cmd);
  INSTALL (ENABLE, database_type_cmd);
  INSTALL (ENABLE, database_type_detail_cmd);
  INSTALL (ENABLE, database_id_cmd);
  INSTALL (ENABLE, database_id_detail_cmd);
  INSTALL (ENABLE, database_linkstate_id_cmd);
  INSTALL (ENABLE, database_linkstate_id_detail_cmd);
  INSTALL (ENABLE, database_router_cmd);
  INSTALL (ENABLE, database_router_detail_cmd);
  INSTALL (ENABLE, database_adv_router_cmd);
  INSTALL (ENABLE, database_adv_router_detail_cmd);
  INSTALL (ENABLE, database_type_id_cmd);
  INSTALL (ENABLE, database_type_id_detail_cmd);
  INSTALL (ENABLE, database_type_linkstate_id_cmd);
  INSTALL (ENABLE, database_type_linkstate_id_detail_cmd);
  INSTALL (ENABLE, database_type_router_cmd);
  INSTALL (ENABLE, database_type_router_detail_cmd);
  INSTALL (ENABLE, database_type_adv_router_cmd);
  INSTALL (ENABLE, database_type_adv_router_detail_cmd);
  INSTALL (ENABLE, database_adv_router_linkstate_id_cmd);
  INSTALL (ENABLE, database_adv_router_linkstate_id_detail_cmd);
  INSTALL (ENABLE, database_id_router_cmd);
  INSTALL (ENABLE, database_id_router_detail_cmd);
  INSTALL (ENABLE, database_type_id_router_cmd);
  INSTALL (ENABLE, database_type_id_router_detail_cmd);
  INSTALL (ENABLE, database_type_adv_router_linkstate_id_cmd);
  INSTALL (ENABLE, database_type_adv_router_linkstate_id_detail_cmd);
  INSTALL (ENABLE, database_self_originated_cmd);
  INSTALL (ENABLE, database_self_originated_detail_cmd);
  INSTALL (ENABLE, database_type_self_originated_cmd);
  INSTALL (ENABLE, database_type_self_originated_detail_cmd);
  INSTALL (ENABLE, database_type_id_self_originated_cmd);
  INSTALL (ENABLE, database_type_id_self_originated_detail_cmd);
  INSTALL (ENABLE, database_type_self_originated_linkstate_id_cmd);
  INSTALL (ENABLE, database_type_self_originated_linkstate_id_detail_cmd);

  /* Make ospf protocol socket. */
  ospf6_serv_sock ();
  thread_add_read (master, ospf6_receive, NULL, ospf6_sock);
}


