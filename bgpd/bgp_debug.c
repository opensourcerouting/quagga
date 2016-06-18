/* BGP-4, BGP-4+ packet debug routine
   Copyright (C) 1996, 97, 99 Kunihiro Ishiguro

This file is part of GNU Zebra.

GNU Zebra is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

GNU Zebra is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Zebra; see the file COPYING.  If not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

#include <zebra.h>

#include <lib/version.h>
#include "prefix.h"
#include "linklist.h"
#include "stream.h"
#include "command.h"
#include "str.h"
#include "log.h"
#include "sockunion.h"
#include "filter.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_debug.h"
#include "bgpd/bgp_community.h"

unsigned long conf_bgp_debug_as4;
unsigned long conf_bgp_debug_fsm;
unsigned long conf_bgp_debug_events;
unsigned long conf_bgp_debug_packet;
unsigned long conf_bgp_debug_filter;
unsigned long conf_bgp_debug_keepalive;
unsigned long conf_bgp_debug_update;
unsigned long conf_bgp_debug_normal;
unsigned long conf_bgp_debug_zebra;
unsigned long conf_bgp_debug_allow_martians;

unsigned long term_bgp_debug_as4;
unsigned long term_bgp_debug_fsm;
unsigned long term_bgp_debug_events;
unsigned long term_bgp_debug_packet;
unsigned long term_bgp_debug_filter;
unsigned long term_bgp_debug_keepalive;
unsigned long term_bgp_debug_update;
unsigned long term_bgp_debug_normal;
unsigned long term_bgp_debug_zebra;
unsigned long term_bgp_debug_allow_martians;

/* messages for BGP-4 status */
const struct message bgp_status_msg[] = 
{
  { Idle, "Idle" },
  { Connect, "Connect" },
  { Active, "Active" },
  { OpenSent, "OpenSent" },
  { OpenConfirm, "OpenConfirm" },
  { Established, "Established" },
  { Clearing,    "Clearing"    },
  { Deleted,     "Deleted"     },
};
const int bgp_status_msg_max = BGP_STATUS_MAX;

/* BGP message type string. */
const char *bgp_type_str[] =
{
  NULL,
  "OPEN",
  "UPDATE",
  "NOTIFICATION",
  "KEEPALIVE",
  "ROUTE-REFRESH",
  "CAPABILITY"
};

/* message for BGP-4 Notify */
static const struct message bgp_notify_msg[] = 
{
  { BGP_NOTIFY_HEADER_ERR, "Message Header Error"},
  { BGP_NOTIFY_OPEN_ERR, "OPEN Message Error"},
  { BGP_NOTIFY_UPDATE_ERR, "UPDATE Message Error"},
  { BGP_NOTIFY_HOLD_ERR, "Hold Timer Expired"},
  { BGP_NOTIFY_FSM_ERR, "Finite State Machine Error"},
  { BGP_NOTIFY_CEASE, "Cease"},
  { BGP_NOTIFY_CAPABILITY_ERR, "CAPABILITY Message Error"},
};
static const int bgp_notify_msg_max = BGP_NOTIFY_MAX;

static const struct message bgp_notify_head_msg[] = 
{
  { BGP_NOTIFY_HEADER_NOT_SYNC, "/Connection Not Synchronized"},
  { BGP_NOTIFY_HEADER_BAD_MESLEN, "/Bad Message Length"},
  { BGP_NOTIFY_HEADER_BAD_MESTYPE, "/Bad Message Type"}
};
static const int bgp_notify_head_msg_max = BGP_NOTIFY_HEADER_MAX;

static const struct message bgp_notify_open_msg[] = 
{
  { BGP_NOTIFY_SUBCODE_UNSPECIFIC, "/Unspecific"},
  { BGP_NOTIFY_OPEN_UNSUP_VERSION, "/Unsupported Version Number" },
  { BGP_NOTIFY_OPEN_BAD_PEER_AS, "/Bad Peer AS"},
  { BGP_NOTIFY_OPEN_BAD_BGP_IDENT, "/Bad BGP Identifier"},
  { BGP_NOTIFY_OPEN_UNSUP_PARAM, "/Unsupported Optional Parameter"},
  { BGP_NOTIFY_OPEN_AUTH_FAILURE, "/Authentication Failure"},
  { BGP_NOTIFY_OPEN_UNACEP_HOLDTIME, "/Unacceptable Hold Time"}, 
  { BGP_NOTIFY_OPEN_UNSUP_CAPBL, "/Unsupported Capability"},
};
static const int bgp_notify_open_msg_max = BGP_NOTIFY_OPEN_MAX;

static const struct message bgp_notify_update_msg[] = 
{
  { BGP_NOTIFY_SUBCODE_UNSPECIFIC, "/Unspecific"},
  { BGP_NOTIFY_UPDATE_MAL_ATTR, "/Malformed Attribute List"},
  { BGP_NOTIFY_UPDATE_UNREC_ATTR, "/Unrecognized Well-known Attribute"},
  { BGP_NOTIFY_UPDATE_MISS_ATTR, "/Missing Well-known Attribute"},
  { BGP_NOTIFY_UPDATE_ATTR_FLAG_ERR, "/Attribute Flags Error"},
  { BGP_NOTIFY_UPDATE_ATTR_LENG_ERR, "/Attribute Length Error"},
  { BGP_NOTIFY_UPDATE_INVAL_ORIGIN, "/Invalid ORIGIN Attribute"},
  { BGP_NOTIFY_UPDATE_AS_ROUTE_LOOP, "/AS Routing Loop"},
  { BGP_NOTIFY_UPDATE_INVAL_NEXT_HOP, "/Invalid NEXT_HOP Attribute"},
  { BGP_NOTIFY_UPDATE_OPT_ATTR_ERR, "/Optional Attribute Error"},
  { BGP_NOTIFY_UPDATE_INVAL_NETWORK, "/Invalid Network Field"},
  { BGP_NOTIFY_UPDATE_MAL_AS_PATH, "/Malformed AS_PATH"},
};
static const int bgp_notify_update_msg_max = BGP_NOTIFY_UPDATE_MAX;

static const struct message bgp_notify_cease_msg[] =
{
  { BGP_NOTIFY_SUBCODE_UNSPECIFIC, "/Unspecific"},
  { BGP_NOTIFY_CEASE_MAX_PREFIX, "/Maximum Number of Prefixes Reached"},
  { BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN, "/Administratively Shutdown"},
  { BGP_NOTIFY_CEASE_PEER_UNCONFIG, "/Peer Unconfigured"},
  { BGP_NOTIFY_CEASE_ADMIN_RESET, "/Administratively Reset"},
  { BGP_NOTIFY_CEASE_CONNECT_REJECT, "/Connection Rejected"},
  { BGP_NOTIFY_CEASE_CONFIG_CHANGE, "/Other Configuration Change"},
  { BGP_NOTIFY_CEASE_COLLISION_RESOLUTION, "/Connection collision resolution"},
  { BGP_NOTIFY_CEASE_OUT_OF_RESOURCE, "/Out of Resource"},
};
static const int bgp_notify_cease_msg_max = BGP_NOTIFY_CEASE_MAX;

static const struct message bgp_notify_capability_msg[] = 
{
  { BGP_NOTIFY_SUBCODE_UNSPECIFIC, "/Unspecific"},
  { BGP_NOTIFY_CAPABILITY_INVALID_ACTION, "/Invalid Action Value" },
  { BGP_NOTIFY_CAPABILITY_INVALID_LENGTH, "/Invalid Capability Length"},
  { BGP_NOTIFY_CAPABILITY_MALFORMED_CODE, "/Malformed Capability Value"},
};
static const int bgp_notify_capability_msg_max = BGP_NOTIFY_CAPABILITY_MAX;

/* Origin strings. */
const char *bgp_origin_str[] = {"i","e","?"};
const char *bgp_origin_long_str[] = {"IGP","EGP","incomplete"};

/* Dump attribute. */
int
bgp_dump_attr (struct peer *peer, struct attr *attr, char *buf, size_t size)
{
  if (! attr)
    return 0;

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_NEXT_HOP)))
    snprintf (buf, size, "nexthop %s", inet_ntoa (attr->nexthop));

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_ORIGIN)))
    snprintf (buf + strlen (buf), size - strlen (buf), ", origin %s",
	      bgp_origin_str[attr->origin]);

  if (attr->extra)
    {
      char addrbuf[BUFSIZ];

      /* Add MP case. */
      if (attr->extra->mp_nexthop_len == 16 
          || attr->extra->mp_nexthop_len == 32)
        snprintf (buf + strlen (buf), size - strlen (buf), ", mp_nexthop %s",
                  inet_ntop (AF_INET6, &attr->extra->mp_nexthop_global, 
                             addrbuf, BUFSIZ));

      if (attr->extra->mp_nexthop_len == 32)
        snprintf (buf + strlen (buf), size - strlen (buf), "(%s)",
                  inet_ntop (AF_INET6, &attr->extra->mp_nexthop_local, 
                             addrbuf, BUFSIZ));
    }

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_LOCAL_PREF)))
    snprintf (buf + strlen (buf), size - strlen (buf), ", localpref %u",
	      attr->local_pref);

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_MULTI_EXIT_DISC))) 
    snprintf (buf + strlen (buf), size - strlen (buf), ", metric %u",
	      attr->med);

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_COMMUNITIES))) 
    snprintf (buf + strlen (buf), size - strlen (buf), ", community %s",
	      community_str (attr->community));

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_ATOMIC_AGGREGATE)))
    snprintf (buf + strlen (buf), size - strlen (buf), ", atomic-aggregate");

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_AGGREGATOR)))
    snprintf (buf + strlen (buf), size - strlen (buf), ", aggregated by %u %s",
	      attr->extra->aggregator_as,
	      inet_ntoa (attr->extra->aggregator_addr));

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_ORIGINATOR_ID)))
    snprintf (buf + strlen (buf), size - strlen (buf), ", originator %s",
	      inet_ntoa (attr->extra->originator_id));

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_CLUSTER_LIST)))
    {
      int i;

      snprintf (buf + strlen (buf), size - strlen (buf), ", clusterlist");
      for (i = 0; i < attr->extra->cluster->length / 4; i++)
	snprintf (buf + strlen (buf), size - strlen (buf), " %s",
		  inet_ntoa (attr->extra->cluster->list[i]));
    }

  if (CHECK_FLAG (attr->flag, ATTR_FLAG_BIT (BGP_ATTR_AS_PATH))) 
    snprintf (buf + strlen (buf), size - strlen (buf), ", path %s",
	      aspath_print (attr->aspath));

  if (strlen (buf) > 1)
    return 1;
  else
    return 0;
}

/* dump notify packet */
void
bgp_notify_print(struct peer *peer, struct bgp_notify *bgp_notify, 
                 const char *direct)
{
  const char *subcode_str;
  const char *code_str;

  subcode_str = "";
  code_str = LOOKUP_DEF (bgp_notify_msg, bgp_notify->code,
                         "Unrecognized Error Code");

  switch (bgp_notify->code)
    {
    case BGP_NOTIFY_HEADER_ERR:
      subcode_str = LOOKUP_DEF (bgp_notify_head_msg, bgp_notify->subcode,
                                "Unrecognized Error Subcode");
      break;
    case BGP_NOTIFY_OPEN_ERR:
      subcode_str = LOOKUP_DEF (bgp_notify_open_msg, bgp_notify->subcode,
                                "Unrecognized Error Subcode");
      break;
    case BGP_NOTIFY_UPDATE_ERR:
      subcode_str = LOOKUP_DEF (bgp_notify_update_msg, bgp_notify->subcode,
                                "Unrecognized Error Subcode");
      break;
    case BGP_NOTIFY_HOLD_ERR:
      break;
    case BGP_NOTIFY_FSM_ERR:
      break;
    case BGP_NOTIFY_CEASE:
      subcode_str = LOOKUP_DEF (bgp_notify_cease_msg, bgp_notify->subcode,
                                "Unrecognized Error Subcode");
      break;
    case BGP_NOTIFY_CAPABILITY_ERR:
      subcode_str = LOOKUP_DEF (bgp_notify_capability_msg, bgp_notify->subcode,
                                "Unrecognized Error Subcode");
      break;
    }

  if (bgp_flag_check (peer->bgp, BGP_FLAG_LOG_NEIGHBOR_CHANGES))
    zlog_info ("%%NOTIFICATION: %s neighbor %s %d/%d (%s%s) %d bytes %s",
              strcmp (direct, "received") == 0 ? "received from" : "sent to",
              peer->host, bgp_notify->code, bgp_notify->subcode,
              code_str, subcode_str, bgp_notify->length,
              bgp_notify->data ? bgp_notify->data : "");
  else if (BGP_DEBUG (normal, NORMAL))
    plog_debug (peer->log, "%s %s NOTIFICATION %d/%d (%s%s) %d bytes %s",
	       peer ? peer->host : "",
	       direct, bgp_notify->code, bgp_notify->subcode,
	       code_str, subcode_str, bgp_notify->length,
	       bgp_notify->data ? bgp_notify->data : "");
}

/* Debug option setting interface. */
unsigned long bgp_debug_option = 0;

int  
debug (unsigned int option)
{
  return bgp_debug_option & option; 
}

DEFUN (debug_bgp_as4,
       debug_bgp_as4_cmd,
       "debug bgp as4",
       DEBUG_STR
       BGP_STR
       "BGP AS4 actions\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (as4, AS4);
  else
    {
      TERM_DEBUG_ON (as4, AS4);
      vty_out (vty, "BGP as4 debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_as4,
       no_debug_bgp_as4_cmd,
       "no debug bgp as4",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP AS4 actions\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (as4, AS4);
  else
    {
      TERM_DEBUG_OFF (as4, AS4);
      vty_out (vty, "BGP as4 debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_as4,
       undebug_bgp_as4_cmd,
       "undebug bgp as4",
       UNDEBUG_STR
       BGP_STR
       "BGP AS4 actions\n")

DEFUN (debug_bgp_as4_segment,
       debug_bgp_as4_segment_cmd,
       "debug bgp as4 segment",
       DEBUG_STR
       BGP_STR
       "BGP AS4 actions\n"
       "BGP AS4 aspath segment handling\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (as4, AS4_SEGMENT);
  else
    {
      TERM_DEBUG_ON (as4, AS4_SEGMENT);
      vty_out (vty, "BGP as4 segment debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_as4_segment,
       no_debug_bgp_as4_segment_cmd,
       "no debug bgp as4 segment",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP AS4 actions\n"
       "BGP AS4 aspath segment handling\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (as4, AS4_SEGMENT);
  else
    {
      TERM_DEBUG_OFF (as4, AS4_SEGMENT);
      vty_out (vty, "BGP as4 segment debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_as4_segment,
       undebug_bgp_as4_segment_cmd,
       "undebug bgp as4 segment",
       UNDEBUG_STR
       BGP_STR
       "BGP AS4 actions\n"
       "BGP AS4 aspath segment handling\n")

DEFUN (debug_bgp_fsm,
       debug_bgp_fsm_cmd,
       "debug bgp fsm",
       DEBUG_STR
       BGP_STR
       "BGP Finite State Machine\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (fsm, FSM);
  else
    {
      TERM_DEBUG_ON (fsm, FSM);
      vty_out (vty, "BGP fsm debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_fsm,
       no_debug_bgp_fsm_cmd,
       "no debug bgp fsm",
       NO_STR
       DEBUG_STR
       BGP_STR
       "Finite State Machine\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (fsm, FSM);
  else
    {
      TERM_DEBUG_OFF (fsm, FSM);
      vty_out (vty, "BGP fsm debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_fsm,
       undebug_bgp_fsm_cmd,
       "undebug bgp fsm",
       UNDEBUG_STR
       BGP_STR
       "Finite State Machine\n")

DEFUN (debug_bgp_events,
       debug_bgp_events_cmd,
       "debug bgp events",
       DEBUG_STR
       BGP_STR
       "BGP events\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (events, EVENTS);
  else
    {
      TERM_DEBUG_ON (events, EVENTS);
      vty_out (vty, "BGP events debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_events,
       no_debug_bgp_events_cmd,
       "no debug bgp events",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP events\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (events, EVENTS);
  else
    {
      TERM_DEBUG_OFF (events, EVENTS);
      vty_out (vty, "BGP events debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_events,
       undebug_bgp_events_cmd,
       "undebug bgp events",
       UNDEBUG_STR
       BGP_STR
       "BGP events\n")

DEFUN (debug_bgp_filter,
       debug_bgp_filter_cmd,
       "debug bgp filters",
       DEBUG_STR
       BGP_STR
       "BGP filters\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (filter, FILTER);
  else
    {
      TERM_DEBUG_ON (filter, FILTER);
      vty_out (vty, "BGP filters debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_filter,
       no_debug_bgp_filter_cmd,
       "no debug bgp filters",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP filters\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (filter, FILTER);
  else
    {
      TERM_DEBUG_OFF (filter, FILTER);
      vty_out (vty, "BGP filters debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_filter,
       undebug_bgp_filter_cmd,
       "undebug bgp filters",
       UNDEBUG_STR
       BGP_STR
       "BGP filters\n")

DEFUN (debug_bgp_keepalive,
       debug_bgp_keepalive_cmd,
       "debug bgp keepalives",
       DEBUG_STR
       BGP_STR
       "BGP keepalives\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (keepalive, KEEPALIVE);
  else
    {
      TERM_DEBUG_ON (keepalive, KEEPALIVE);
      vty_out (vty, "BGP keepalives debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_keepalive,
       no_debug_bgp_keepalive_cmd,
       "no debug bgp keepalives",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP keepalives\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (keepalive, KEEPALIVE);
  else
    {
      TERM_DEBUG_OFF (keepalive, KEEPALIVE);
      vty_out (vty, "BGP keepalives debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_keepalive,
       undebug_bgp_keepalive_cmd,
       "undebug bgp keepalives",
       UNDEBUG_STR
       BGP_STR
       "BGP keepalives\n")

DEFUN (debug_bgp_update,
       debug_bgp_update_cmd,
       "debug bgp updates",
       DEBUG_STR
       BGP_STR
       "BGP updates\n")
{
  if (vty->node == CONFIG_NODE)
    {
      DEBUG_ON (update, UPDATE_IN);
      DEBUG_ON (update, UPDATE_OUT);
    }
  else
    {
      TERM_DEBUG_ON (update, UPDATE_IN);
      TERM_DEBUG_ON (update, UPDATE_OUT);
      vty_out (vty, "BGP updates debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (debug_bgp_update_direct,
       debug_bgp_update_direct_cmd,
       "debug bgp updates (in|out)",
       DEBUG_STR
       BGP_STR
       "BGP updates\n"
       "Inbound updates\n"
       "Outbound updates\n")
{
  if (vty->node == CONFIG_NODE)
    {
      if (strncmp ("i", argv[0], 1) == 0)
	{
	  DEBUG_OFF (update, UPDATE_OUT);
	  DEBUG_ON (update, UPDATE_IN);
	}
      else
	{	
	  DEBUG_OFF (update, UPDATE_IN);
	  DEBUG_ON (update, UPDATE_OUT);
	}
    }
  else
    {
      if (strncmp ("i", argv[0], 1) == 0)
	{
	  TERM_DEBUG_OFF (update, UPDATE_OUT);
	  TERM_DEBUG_ON (update, UPDATE_IN);
	  vty_out (vty, "BGP updates debugging is on (inbound)%s", VTY_NEWLINE);
	}
      else
	{
	  TERM_DEBUG_OFF (update, UPDATE_IN);
	  TERM_DEBUG_ON (update, UPDATE_OUT);
	  vty_out (vty, "BGP updates debugging is on (outbound)%s", VTY_NEWLINE);
	}
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_update,
       no_debug_bgp_update_cmd,
       "no debug bgp updates",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP updates\n")
{
  if (vty->node == CONFIG_NODE)
    {
      DEBUG_OFF (update, UPDATE_IN);
      DEBUG_OFF (update, UPDATE_OUT);
    }
  else
    {
      TERM_DEBUG_OFF (update, UPDATE_IN);
      TERM_DEBUG_OFF (update, UPDATE_OUT);
      vty_out (vty, "BGP updates debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_update,
       undebug_bgp_update_cmd,
       "undebug bgp updates",
       UNDEBUG_STR
       BGP_STR
       "BGP updates\n")

DEFUN (debug_bgp_normal,
       debug_bgp_normal_cmd,
       "debug bgp",
       DEBUG_STR
       BGP_STR)
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (normal, NORMAL);
  else
    {
      TERM_DEBUG_ON (normal, NORMAL);
      vty_out (vty, "BGP debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_normal,
       no_debug_bgp_normal_cmd,
       "no debug bgp",
       NO_STR
       DEBUG_STR
       BGP_STR)
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (normal, NORMAL);
  else
    {
      TERM_DEBUG_OFF (normal, NORMAL);
      vty_out (vty, "BGP debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_normal,
       undebug_bgp_normal_cmd,
       "undebug bgp",
       UNDEBUG_STR
       BGP_STR)

DEFUN (debug_bgp_zebra,
       debug_bgp_zebra_cmd,
       "debug bgp zebra",
       DEBUG_STR
       BGP_STR
       "BGP Zebra messages\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (zebra, ZEBRA);
  else
    {
      TERM_DEBUG_ON (zebra, ZEBRA);
      vty_out (vty, "BGP zebra debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_zebra,
       no_debug_bgp_zebra_cmd,
       "no debug bgp zebra",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP Zebra messages\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (zebra, ZEBRA);
  else
    {
      TERM_DEBUG_OFF (zebra, ZEBRA);
      vty_out (vty, "BGP zebra debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_zebra,
       undebug_bgp_zebra_cmd,
       "undebug bgp zebra",
       UNDEBUG_STR
       BGP_STR
       "BGP Zebra messages\n")

DEFUN (debug_bgp_allow_martians,
       debug_bgp_allow_martians_cmd,
       "debug bgp allow-martians",
       DEBUG_STR
       BGP_STR
       "BGP allow martian next hops\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_ON (allow_martians, ALLOW_MARTIANS);
  else
    {
      TERM_DEBUG_ON (allow_martians, ALLOW_MARTIANS);
      vty_out (vty, "BGP allow_martian next hop debugging is on%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (no_debug_bgp_allow_martians,
       no_debug_bgp_allow_martians_cmd,
       "no debug bgp allow-martians",
       NO_STR
       DEBUG_STR
       BGP_STR
       "BGP allow martian next hops\n")
{
  if (vty->node == CONFIG_NODE)
    DEBUG_OFF (allow_martians, ALLOW_MARTIANS);
  else
    {
      TERM_DEBUG_OFF (allow_martians, ALLOW_MARTIANS);
      vty_out (vty, "BGP allow martian next hop debugging is off%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_allow_martians,
       undebug_bgp_allow_martians_cmd,
       "undebug bgp allow-martians",
       UNDEBUG_STR
       BGP_STR
       "BGP allow martian next hops\n")

DEFUN (no_debug_bgp_all,
       no_debug_bgp_all_cmd,
       "no debug all bgp",
       NO_STR
       DEBUG_STR
       "Enable all debugging\n"
       BGP_STR)
{
  TERM_DEBUG_OFF (normal, NORMAL);
  TERM_DEBUG_OFF (events, EVENTS);
  TERM_DEBUG_OFF (keepalive, KEEPALIVE);
  TERM_DEBUG_OFF (update, UPDATE_IN);
  TERM_DEBUG_OFF (update, UPDATE_OUT);
  TERM_DEBUG_OFF (as4, AS4);
  TERM_DEBUG_OFF (as4, AS4_SEGMENT);
  TERM_DEBUG_OFF (fsm, FSM);
  TERM_DEBUG_OFF (filter, FILTER);
  TERM_DEBUG_OFF (zebra, ZEBRA);
  TERM_DEBUG_OFF (allow_martians, ALLOW_MARTIANS);
  vty_out (vty, "All possible debugging has been turned off%s", VTY_NEWLINE);
      
  return CMD_SUCCESS;
}

ALIAS (no_debug_bgp_all,
       undebug_bgp_all_cmd,
       "undebug all bgp",
       UNDEBUG_STR
       "Enable all debugging\n"
       BGP_STR)

DEFUN (show_debugging_bgp,
       show_debugging_bgp_cmd,
       "show debugging bgp",
       SHOW_STR
       DEBUG_STR
       BGP_STR)
{
  vty_out (vty, "BGP debugging status:%s", VTY_NEWLINE);

  if (BGP_DEBUG (normal, NORMAL))
    vty_out (vty, "  BGP debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (events, EVENTS))
    vty_out (vty, "  BGP events debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (keepalive, KEEPALIVE))
    vty_out (vty, "  BGP keepalives debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (update, UPDATE_IN) && BGP_DEBUG (update, UPDATE_OUT))
    vty_out (vty, "  BGP updates debugging is on%s", VTY_NEWLINE);
  else if (BGP_DEBUG (update, UPDATE_IN))
    vty_out (vty, "  BGP updates debugging is on (inbound)%s", VTY_NEWLINE);
  else if (BGP_DEBUG (update, UPDATE_OUT))
    vty_out (vty, "  BGP updates debugging is on (outbound)%s", VTY_NEWLINE);
  if (BGP_DEBUG (fsm, FSM))
    vty_out (vty, "  BGP fsm debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (filter, FILTER))
    vty_out (vty, "  BGP filter debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (zebra, ZEBRA))
    vty_out (vty, "  BGP zebra debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (as4, AS4))
    vty_out (vty, "  BGP as4 debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (as4, AS4_SEGMENT))
    vty_out (vty, "  BGP as4 aspath segment debugging is on%s", VTY_NEWLINE);
  if (BGP_DEBUG (allow_martians, ALLOW_MARTIANS))
    vty_out (vty, "  BGP allow martian next hop debugging is on%s", VTY_NEWLINE);
  vty_out (vty, "%s", VTY_NEWLINE);
  return CMD_SUCCESS;
}

static int
bgp_config_write_debug (struct vty *vty)
{
  int write = 0;

  if (CONF_BGP_DEBUG (normal, NORMAL))
    {
      vty_out (vty, "debug bgp%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (as4, AS4))
    {
      vty_out (vty, "debug bgp as4%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (as4, AS4_SEGMENT))
    {
      vty_out (vty, "debug bgp as4 segment%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (events, EVENTS))
    {
      vty_out (vty, "debug bgp events%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (keepalive, KEEPALIVE))
    {
      vty_out (vty, "debug bgp keepalives%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (update, UPDATE_IN) && CONF_BGP_DEBUG (update, UPDATE_OUT))
    {
      vty_out (vty, "debug bgp updates%s", VTY_NEWLINE);
      write++;
    }
  else if (CONF_BGP_DEBUG (update, UPDATE_IN))
    {
      vty_out (vty, "debug bgp updates in%s", VTY_NEWLINE);
      write++;
    }
  else if (CONF_BGP_DEBUG (update, UPDATE_OUT))
    {
      vty_out (vty, "debug bgp updates out%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (fsm, FSM))
    {
      vty_out (vty, "debug bgp fsm%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (filter, FILTER))
    {
      vty_out (vty, "debug bgp filters%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (zebra, ZEBRA))
    {
      vty_out (vty, "debug bgp zebra%s", VTY_NEWLINE);
      write++;
    }

  if (CONF_BGP_DEBUG (allow_martians, ALLOW_MARTIANS))
    {
      vty_out (vty, "debug bgp allow-martians%s", VTY_NEWLINE);
      write++;
    }

  return write;
}

static struct cmd_node debug_node =
{
  DEBUG_NODE,
  "",
  1
};

void
bgp_debug_init (void)
{
  install_node (&debug_node, bgp_config_write_debug);

  install_element (ENABLE_NODE, &show_debugging_bgp_cmd);

  install_element (ENABLE_NODE, &debug_bgp_as4_cmd);
  install_element (CONFIG_NODE, &debug_bgp_as4_cmd);
  install_element (ENABLE_NODE, &debug_bgp_as4_segment_cmd);
  install_element (CONFIG_NODE, &debug_bgp_as4_segment_cmd);

  install_element (ENABLE_NODE, &debug_bgp_fsm_cmd);
  install_element (CONFIG_NODE, &debug_bgp_fsm_cmd);
  install_element (ENABLE_NODE, &debug_bgp_events_cmd);
  install_element (CONFIG_NODE, &debug_bgp_events_cmd);
  install_element (ENABLE_NODE, &debug_bgp_filter_cmd);
  install_element (CONFIG_NODE, &debug_bgp_filter_cmd);
  install_element (ENABLE_NODE, &debug_bgp_keepalive_cmd);
  install_element (CONFIG_NODE, &debug_bgp_keepalive_cmd);
  install_element (ENABLE_NODE, &debug_bgp_update_cmd);
  install_element (CONFIG_NODE, &debug_bgp_update_cmd);
  install_element (ENABLE_NODE, &debug_bgp_update_direct_cmd);
  install_element (CONFIG_NODE, &debug_bgp_update_direct_cmd);
  install_element (ENABLE_NODE, &debug_bgp_normal_cmd);
  install_element (CONFIG_NODE, &debug_bgp_normal_cmd);
  install_element (ENABLE_NODE, &debug_bgp_zebra_cmd);
  install_element (CONFIG_NODE, &debug_bgp_zebra_cmd);
  install_element (ENABLE_NODE, &debug_bgp_allow_martians_cmd);
  install_element (CONFIG_NODE, &debug_bgp_allow_martians_cmd);

  install_element (ENABLE_NODE, &no_debug_bgp_as4_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_as4_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_as4_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_as4_segment_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_as4_segment_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_as4_segment_cmd);

  install_element (ENABLE_NODE, &no_debug_bgp_fsm_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_fsm_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_fsm_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_events_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_events_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_events_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_filter_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_filter_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_filter_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_keepalive_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_keepalive_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_keepalive_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_update_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_update_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_update_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_normal_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_normal_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_normal_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_zebra_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_zebra_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_zebra_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_allow_martians_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_allow_martians_cmd);
  install_element (CONFIG_NODE, &no_debug_bgp_allow_martians_cmd);
  install_element (ENABLE_NODE, &no_debug_bgp_all_cmd);
  install_element (ENABLE_NODE, &undebug_bgp_all_cmd);
}
