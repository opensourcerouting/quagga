/*
 * Interface function.
 * Copyright (C) 1997, 1999 Kunihiro Ishiguro
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

#include <zebra.h>

#include "if.h"
#include "vty.h"
#include "sockunion.h"
#include "prefix.h"
#include "command.h"
#include "memory.h"
#include "ioctl.h"
#include "connected.h"
#include "log.h"
#include "zclient.h"
#include "vrf.h"
#include "command.h"

#include "zebra/interface.h"
#include "zebra/rtadv.h"
#include "zebra/rib.h"
#include "zebra/zserv.h"
#include "zebra/redistribute.h"
#include "zebra/debug.h"
#include "zebra/irdp.h"

#if defined (HAVE_RTADV)
/* Order is intentional.  Matches RFC4191.  This array is also used for
   command matching, so only modify with care. */
const char *rtadv_pref_strs[] = { "medium", "high", "INVALID", "low", 0 };
#endif /* HAVE_RTADV */

/* We don't have a tidy top-level instance object for zebra, or interfaces */
static struct zebra_if_defaults zif_defaults = {
  .linkdetect = IF_LINKDETECT_UNSPEC,
};

/* helper only for if_zebra_linkdetect */
static void
if_zebra_linkdetect_set_val (struct interface *ifp, zebra_if_linkdetect val)
{
  switch (val)
    {
      case IF_LINKDETECT_ON:
        SET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);
        break;
      case IF_LINKDETECT_OFF:
        UNSET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);
        break;
      default: break;
    }
}

static void
if_zebra_linkdetect_set (struct interface *ifp)
{
  struct zebra_if *zif = ifp->info;
  assert (zif != NULL);
  int if_was_operative = if_is_operative(ifp);
  
  /* If user has explicitly configured for the interface, let that set */
  if (zif->linkdetect != IF_LINKDETECT_UNSPEC)
    if_zebra_linkdetect_set_val (ifp, zif->linkdetect);
  else 
    {
      /* general compiled in default is to set */
      SET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);
      /* but user can specify a default too */
      if_zebra_linkdetect_set_val (ifp, zif_defaults.linkdetect);
    }
  /* When linkdetection is enabled, interface might come down */
  if (!if_is_operative(ifp) && if_was_operative) if_down(ifp);
  /* Alternatively, it may come up after disabling link detection */
  if (if_is_operative(ifp) && !if_was_operative) if_up(ifp);
}

/* Called when new interface is added. */
static int
if_zebra_new_hook (struct interface *ifp)
{
  struct zebra_if *zebra_if;

  zebra_if = XCALLOC (MTYPE_TMP, sizeof (struct zebra_if));

  zebra_if->multicast = IF_ZEBRA_MULTICAST_UNSPEC;
  zebra_if->shutdown = IF_ZEBRA_SHUTDOWN_OFF;

  switch (zif_defaults.linkdetect)
    {
      case IF_LINKDETECT_OFF:
        UNSET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);
        break;
      case IF_LINKDETECT_UNSPEC:
      case IF_LINKDETECT_ON:
      default:
        SET_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION);
        break;
    }
  
#if defined (HAVE_RTADV)
  {
    /* Set default router advertise values. */
    struct rtadvconf *rtadv;

    rtadv = &zebra_if->rtadv;

    rtadv->AdvSendAdvertisements = 0;
    rtadv->MaxRtrAdvInterval = RTADV_MAX_RTR_ADV_INTERVAL;
    rtadv->MinRtrAdvInterval = RTADV_MIN_RTR_ADV_INTERVAL;
    rtadv->AdvIntervalTimer = 0;
    rtadv->AdvManagedFlag = 0;
    rtadv->AdvOtherConfigFlag = 0;
    rtadv->AdvHomeAgentFlag = 0;
    rtadv->AdvLinkMTU = 0;
    rtadv->AdvReachableTime = 0;
    rtadv->AdvRetransTimer = 0;
    rtadv->AdvCurHopLimit = 0;
    rtadv->AdvDefaultLifetime = -1; /* derive from MaxRtrAdvInterval */
    rtadv->HomeAgentPreference = 0;
    rtadv->HomeAgentLifetime = -1; /* derive from AdvDefaultLifetime */
    rtadv->AdvIntervalOption = 0;
    rtadv->DefaultPreference = RTADV_PREF_MEDIUM;

    rtadv->AdvPrefixList = list_new ();
  }    
#endif /* HAVE_RTADV */

  /* Initialize installed address chains tree. */
  zebra_if->ipv4_subnets = route_table_init ();

  ifp->info = zebra_if;
  return 0;
}

/* Called when interface is deleted. */
static int
if_zebra_delete_hook (struct interface *ifp)
{
  struct zebra_if *zebra_if;
  
  if (ifp->info)
    {
      zebra_if = ifp->info;

      /* Free installed address chains tree. */
      if (zebra_if->ipv4_subnets)
	route_table_finish (zebra_if->ipv4_subnets);

      XFREE (MTYPE_TMP, zebra_if);
    }

  return 0;
}

/* Tie an interface address to its derived subnet list of addresses. */
int
if_subnet_add (struct interface *ifp, struct connected *ifc)
{
  struct route_node *rn;
  struct zebra_if *zebra_if;
  struct prefix cp;
  struct list *addr_list;

  assert (ifp && ifp->info && ifc);
  zebra_if = ifp->info;

  /* Get address derived subnet node and associated address list, while marking
     address secondary attribute appropriately. */
  cp = *ifc->address;
  apply_mask (&cp);
  rn = route_node_get (zebra_if->ipv4_subnets, &cp);

  if ((addr_list = rn->info))
    SET_FLAG (ifc->flags, ZEBRA_IFA_SECONDARY);
  else
    {
      UNSET_FLAG (ifc->flags, ZEBRA_IFA_SECONDARY);
      rn->info = addr_list = list_new ();
      route_lock_node (rn);
    }

  /* Tie address at the tail of address list. */
  listnode_add (addr_list, ifc);
  
  /* Return list element count. */
  return (addr_list->count);
}

/* Untie an interface address from its derived subnet list of addresses. */
int
if_subnet_delete (struct interface *ifp, struct connected *ifc)
{
  struct route_node *rn;
  struct zebra_if *zebra_if;
  struct list *addr_list;

  assert (ifp && ifp->info && ifc);
  zebra_if = ifp->info;

  /* Get address derived subnet node. */
  rn = route_node_lookup (zebra_if->ipv4_subnets, ifc->address);
  if (! (rn && rn->info))
    {
      zlog_warn("Trying to remove an address from an unknown subnet."
                " (please report this bug)");
      return -1;
    }
  route_unlock_node (rn);
  
  /* Untie address from subnet's address list. */
  addr_list = rn->info;

  /* Deleting an address that is not registered is a bug.
   * In any case, we shouldn't decrement the lock counter if the address
   * is unknown. */
  if (!listnode_lookup(addr_list, ifc))
    {
      zlog_warn("Trying to remove an address from a subnet where it is not"
                " currently registered. (please report this bug)");
      return -1;
    }

  listnode_delete (addr_list, ifc);
  route_unlock_node (rn);

  /* Return list element count, if not empty. */
  if (addr_list->count)
    {
      /* If deleted address is primary, mark subsequent one as such and distribute. */
      if (! CHECK_FLAG (ifc->flags, ZEBRA_IFA_SECONDARY))
	{
	  ifc = listgetdata ((struct listnode *)listhead (addr_list));
	  zebra_interface_address_delete_update (ifp, ifc);
	  UNSET_FLAG (ifc->flags, ZEBRA_IFA_SECONDARY);
	  /* XXX: Linux kernel removes all the secondary addresses when the primary
	   * address is removed. We could try to work around that, though this is
	   * non-trivial. */
	  zebra_interface_address_add_update (ifp, ifc);
	}
      
      return addr_list->count;
    }
  
  /* Otherwise, free list and route node. */
  list_free (addr_list);
  rn->info = NULL;
  route_unlock_node (rn);

  return 0;
}

/* if_flags_mangle: A place for hacks that require mangling
 * or tweaking the interface flags.
 *
 * ******************** Solaris flags hacks **************************
 *
 * Solaris IFF_UP flag reflects only the primary interface as the
 * routing socket only sends IFINFO for the primary interface.  Hence  
 * ~IFF_UP does not per se imply all the logical interfaces are also   
 * down - which we only know of as addresses. Instead we must determine
 * whether the interface really is up or not according to how many   
 * addresses are still attached. (Solaris always sends RTM_DELADDR if
 * an interface, logical or not, goes ~IFF_UP).
 *
 * Ie, we mangle IFF_UP to *additionally* reflect whether or not there
 * are addresses left in struct connected, not just the actual underlying
 * IFF_UP flag.
 *
 * We must hence remember the real state of IFF_UP, which we do in
 * struct zebra_if.primary_state.
 *
 * Setting IFF_UP within zebra to administratively shutdown the
 * interface will affect only the primary interface/address on Solaris.
 ************************End Solaris flags hacks ***********************
 */
static void
if_flags_mangle (struct interface *ifp, uint64_t *newflags)
{
#ifdef SUNOS_5
  struct zebra_if *zif = ifp->info;
  
  zif->primary_state = *newflags & (IFF_UP & 0xff);
  
  if (CHECK_FLAG (zif->primary_state, IFF_UP)
      || listcount(ifp->connected) > 0)
    SET_FLAG (*newflags, IFF_UP);
  else
    UNSET_FLAG (*newflags, IFF_UP);
#endif /* SUNOS_5 */
}

/* Update the flags field of the ifp with the new flag set provided.
 * Take whatever actions are required for any changes in flags we care
 * about.
 *
 * newflags should be the raw value, as obtained from the OS.
 */
void
if_flags_update (struct interface *ifp, uint64_t newflags)
{
  if_flags_mangle (ifp, &newflags);
    
  if (if_is_operative (ifp))
    {
      /* operative -> inoperative? */
      ifp->flags = newflags;
      if (!if_is_operative (ifp))
        if_down (ifp);
    }
  else
    {
      /* inoperative -> operative? */
      ifp->flags = newflags;
      if (if_is_operative (ifp))
        if_up (ifp);
    }
}

/* Wake up configured address if it is not in current kernel
   address. */
static void
if_addr_wakeup (struct interface *ifp)
{
  struct listnode *node, *nnode;
  struct connected *ifc;
  struct prefix *p;
  int ret;

  for (ALL_LIST_ELEMENTS (ifp->connected, node, nnode, ifc))
    {
      p = ifc->address;
	
      if (CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED)
	  && ! CHECK_FLAG (ifc->conf, ZEBRA_IFC_QUEUED))
	{
	  /* Address check. */
	  if (p->family == AF_INET)
	    {
	      if (! if_is_up (ifp))
		{
		  /* Assume zebra is configured like following:
		   *
		   *   interface gre0
		   *    ip addr 192.0.2.1/24
		   *   !
		   *
		   * As soon as zebra becomes first aware that gre0 exists in the
		   * kernel, it will set gre0 up and configure its addresses.
		   *
		   * (This may happen at startup when the interface already exists
		   * or during runtime when the interface is added to the kernel)
		   *
		   * XXX: IRDP code is calling here via if_add_update - this seems
		   * somewhat weird.
		   * XXX: RUNNING is not a settable flag on any system
		   * I (paulj) am aware of.
		  */
		  if_set_flags (ifp, IFF_UP | IFF_RUNNING);
		  if_refresh (ifp);
		}

	      ret = if_set_prefix (ifp, ifc);
	      if (ret < 0)
		{
		  zlog_warn ("Can't set interface's address: %s", 
			     safe_strerror(errno));
		  continue;
		}

	      SET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
	      /* The address will be advertised to zebra clients when the notification
	       * from the kernel has been received.
	       * It will also be added to the interface's subnet list then. */
	    }
#ifdef HAVE_IPV6
	  if (p->family == AF_INET6)
	    {
	      if (! if_is_up (ifp))
		{
		  /* See long comment above */
		  if_set_flags (ifp, IFF_UP | IFF_RUNNING);
		  if_refresh (ifp);
		}

	      ret = if_prefix_add_ipv6 (ifp, ifc);
	      if (ret < 0)
		{
		  zlog_warn ("Can't set interface's address: %s", 
			     safe_strerror(errno));
		  continue;
		}

	      SET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
	      /* The address will be advertised to zebra clients when the notification
	       * from the kernel has been received. */
	    }
#endif /* HAVE_IPV6 */
	}
    }
}

static void if_count_up(struct zebra_if *zif)
{
  event_counter_inc(&zif->up_events);
}

static void if_count_down(struct zebra_if *zif)
{
  event_counter_inc(&zif->down_events);
}

void
if_startup_count_up (void)
{
  vrf_iter_t iter;
  struct interface *ifp;
  struct zebra_if *zif;
  struct listnode *node;

  for (iter = vrf_first(); iter != VRF_ITER_INVALID; iter = vrf_next(iter))
    {
      for (ALL_LIST_ELEMENTS_RO (vrf_iter2iflist(iter), node, ifp))
        {
          zif = ifp->info;
          if (!zif->up_events.count && if_is_operative(ifp))
            if_count_up(zif);
        }
    }
}

/* Handle interface addition */
void
if_add_update (struct interface *ifp)
{
  struct zebra_if *if_data;

  if_data = ifp->info;
  assert(if_data);

  if (if_data->multicast == IF_ZEBRA_MULTICAST_ON)
    if_set_flags (ifp, IFF_MULTICAST);
  else if (if_data->multicast == IF_ZEBRA_MULTICAST_OFF)
    if_unset_flags (ifp, IFF_MULTICAST);

  zebra_interface_add_update (ifp);

  if (! CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      SET_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE);

      if (if_data && if_data->shutdown == IF_ZEBRA_SHUTDOWN_ON)
	{
	  if (IS_ZEBRA_DEBUG_KERNEL)
	    zlog_debug ("interface %s vrf %u index %d is shutdown. "
			"Won't wake it up.",
			ifp->name, ifp->vrf_id, ifp->ifindex);
	  return;
	}

      if_addr_wakeup (ifp);

      if (IS_ZEBRA_DEBUG_KERNEL)
	zlog_debug ("interface %s vrf %u index %d becomes active.",
		    ifp->name, ifp->vrf_id, ifp->ifindex);
    }
  else
    {
      if (IS_ZEBRA_DEBUG_KERNEL)
	zlog_debug ("interface %s vrf %u index %d is added.",
		    ifp->name, ifp->vrf_id, ifp->ifindex);
    }

  if (host_config_get())
    {
      /* If configuration and therefore link-detect have already been
       * loaded, count an initial up event when new interfaces are added
       * in up state.
       * If configuration has not been loaded yet, this is handled by
       * if_startup_count_up which is called after reading the config. */
      if (!if_data->up_events.count && if_is_operative(ifp))
        if_count_up(if_data);
    }
}

/* Handle an interface delete event */
void 
if_delete_update (struct interface *ifp)
{
  struct connected *ifc;
  struct prefix *p;
  struct route_node *rn;
  struct zebra_if *zebra_if;

  zebra_if = ifp->info;

  if (if_is_up(ifp))
    {
      zlog_err ("interface %s vrf %u index %d is still up while being deleted.",
                ifp->name, ifp->vrf_id, ifp->ifindex);
      return;
    }

  /* Mark interface as inactive */
  UNSET_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE);
  
  if (IS_ZEBRA_DEBUG_KERNEL)
    zlog_debug ("interface %s vrf %u index %d is now inactive.",
                ifp->name, ifp->vrf_id, ifp->ifindex);

  /* Delete connected routes from the kernel. */
  if (ifp->connected)
    {
      struct listnode *node;
      struct listnode *last = NULL;

      while ((node = (last ? last->next : listhead (ifp->connected))))
	{
	  ifc = listgetdata (node);
	  p = ifc->address;
	  
	  if (p->family == AF_INET
	      && (rn = route_node_lookup (zebra_if->ipv4_subnets, p)))
	    {
	      struct listnode *anode;
	      struct listnode *next;
	      struct listnode *first;
	      struct list *addr_list;
	      
	      route_unlock_node (rn);
	      addr_list = (struct list *) rn->info;
	      
	      /* Remove addresses, secondaries first. */
	      first = listhead (addr_list);
	      for (anode = first->next; anode || first; anode = next)
		{
		  if (!anode)
		    {
		      anode = first;
		      first = NULL;
		    }
		  next = anode->next;

		  ifc = listgetdata (anode);
		  p = ifc->address;
		  connected_down_ipv4 (ifp, ifc);

		  /* XXX: We have to send notifications here explicitly, because we destroy
		   * the ifc before receiving the notification about the address being deleted.
		   */
		  zebra_interface_address_delete_update (ifp, ifc);

		  UNSET_FLAG (ifc->conf, ZEBRA_IFC_REAL);
		  UNSET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);

		  /* Remove from subnet chain. */
		  list_delete_node (addr_list, anode);
		  route_unlock_node (rn);
		  
		  /* Remove from interface address list (unconditionally). */
		  if (!CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
		    {
		      listnode_delete (ifp->connected, ifc);
		      connected_free (ifc);
                    }
                  else
                    last = node;
		}

	      /* Free chain list and respective route node. */
	      list_delete (addr_list);
	      rn->info = NULL;
	      route_unlock_node (rn);
	    }
#ifdef HAVE_IPV6
	  else if (p->family == AF_INET6)
	    {
	      connected_down_ipv6 (ifp, ifc);

	      zebra_interface_address_delete_update (ifp, ifc);

	      UNSET_FLAG (ifc->conf, ZEBRA_IFC_REAL);
	      UNSET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);

	      if (CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
		last = node;
	      else
		{
		  listnode_delete (ifp->connected, ifc);
		  connected_free (ifc);
		}
	    }
#endif /* HAVE_IPV6 */
	  else
	    {
	      last = node;
	    }
	}
    }
  zebra_interface_delete_update (ifp);

  /* Update ifindex after distributing the delete message.  This is in
     case any client needs to have the old value of ifindex available
     while processing the deletion.  Each client daemon is responsible
     for setting ifindex to IFINDEX_INTERNAL after processing the
     interface deletion message. */
  ifp->ifindex = IFINDEX_INTERNAL;
}

/* Interface is up. */
void
if_up (struct interface *ifp)
{
  struct listnode *node;
  struct listnode *next;
  struct connected *ifc;
  struct prefix *p;

  if_count_up(ifp->info);

  /* Notify the protocol daemons. */
  zebra_interface_up_update (ifp);

  /* Install connected routes to the kernel. */
  if (ifp->connected)
    {
      for (ALL_LIST_ELEMENTS (ifp->connected, node, next, ifc))
	{
	  p = ifc->address;

	  if (p->family == AF_INET)
	    connected_up_ipv4 (ifp, ifc);
#ifdef HAVE_IPV6
	  else if (p->family == AF_INET6)
	    connected_up_ipv6 (ifp, ifc);
#endif /* HAVE_IPV6 */
	}
    }

  /* Examine all static routes. */
  rib_update (ifp->vrf_id);
}

/* Interface goes down.  We have to manage different behavior of based
   OS. */
void
if_down (struct interface *ifp)
{
  struct listnode *node;
  struct listnode *next;
  struct connected *ifc;
  struct prefix *p;
  struct zebra_if *zif;

  zif = ifp->info;
  if (zif->up_events.count)
    if_count_down(zif);

  /* Notify to the protocol daemons. */
  zebra_interface_down_update (ifp);

  /* Delete connected routes from the kernel. */
  if (ifp->connected)
    {
      for (ALL_LIST_ELEMENTS (ifp->connected, node, next, ifc))
	{
	  p = ifc->address;

	  if (p->family == AF_INET)
	    connected_down_ipv4 (ifp, ifc);
#ifdef HAVE_IPV6
	  else if (p->family == AF_INET6)
	    connected_down_ipv6 (ifp, ifc);
#endif /* HAVE_IPV6 */
	}
    }

  /* Examine all static routes which direct to the interface. */
  rib_update (ifp->vrf_id);
}

void
if_refresh (struct interface *ifp)
{
  if_get_flags (ifp);
}

/* Output prefix string to vty. */
static int
prefix_vty_out (struct vty *vty, struct prefix *p)
{
  char str[INET6_ADDRSTRLEN];

  inet_ntop (p->family, &p->u.prefix, str, sizeof (str));
  vty_out (vty, "%s", str);
  return strlen (str);
}

/* Dump if address information to vty. */
static void
connected_dump_vty (struct vty *vty, struct connected *connected)
{
  struct prefix *p;

  /* Print interface address. */
  p = connected->address;
  vty_out (vty, "  %s ", prefix_family_str (p));
  prefix_vty_out (vty, p);
  vty_out (vty, "/%d", p->prefixlen);

  /* If there is destination address, print it. */
  if (connected->destination)
    {
      vty_out (vty, (CONNECTED_PEER(connected) ? " peer " : " broadcast "));
      prefix_vty_out (vty, connected->destination);
    }

  if (CHECK_FLAG (connected->flags, ZEBRA_IFA_SECONDARY))
    vty_out (vty, " secondary");

  if (CHECK_FLAG (connected->flags, ZEBRA_IFA_UNNUMBERED))
    vty_out (vty, " unnumbered");

  if (connected->label)
    vty_out (vty, " %s", connected->label);

  vty_out (vty, "%s", VTY_NEWLINE);
}

#if defined (HAVE_RTADV)
/* Dump interface ND information to vty. */
static void
nd_dump_vty (struct vty *vty, struct interface *ifp)
{
  struct zebra_if *zif;
  struct rtadvconf *rtadv;
  int interval;

  zif = (struct zebra_if *) ifp->info;
  rtadv = &zif->rtadv;

  if (rtadv->AdvSendAdvertisements)
    {
      vty_out (vty, "  ND advertised reachable time is %d milliseconds%s",
	       rtadv->AdvReachableTime, VTY_NEWLINE);
      vty_out (vty, "  ND advertised retransmit interval is %d milliseconds%s",
	       rtadv->AdvRetransTimer, VTY_NEWLINE);
      interval = rtadv->MaxRtrAdvInterval;
      if (interval % 1000)
        vty_out (vty, "  ND router advertisements are sent every "
			"%d milliseconds%s", interval,
		 VTY_NEWLINE);
      else
        vty_out (vty, "  ND router advertisements are sent every "
			"%d seconds%s", interval / 1000,
		 VTY_NEWLINE);
      if (rtadv->AdvDefaultLifetime != -1)
	vty_out (vty, "  ND router advertisements live for %d seconds%s",
		 rtadv->AdvDefaultLifetime, VTY_NEWLINE);
      else
	vty_out (vty, "  ND router advertisements lifetime tracks ra-interval%s",
		 VTY_NEWLINE);
      vty_out (vty, "  ND router advertisement default router preference is "
			"%s%s", rtadv_pref_strs[rtadv->DefaultPreference],
		 VTY_NEWLINE);
      if (rtadv->AdvManagedFlag)
	vty_out (vty, "  Hosts use DHCP to obtain routable addresses.%s",
		 VTY_NEWLINE);
      else
	vty_out (vty, "  Hosts use stateless autoconfig for addresses.%s",
		 VTY_NEWLINE);
      if (rtadv->AdvHomeAgentFlag)
      {
      	vty_out (vty, "  ND router advertisements with "
				"Home Agent flag bit set.%s",
		 VTY_NEWLINE);
	if (rtadv->HomeAgentLifetime != -1)
	  vty_out (vty, "  Home Agent lifetime is %u seconds%s",
	           rtadv->HomeAgentLifetime, VTY_NEWLINE);
	else
	  vty_out (vty, "  Home Agent lifetime tracks ra-lifetime%s",
	           VTY_NEWLINE);
	vty_out (vty, "  Home Agent preference is %u%s",
	         rtadv->HomeAgentPreference, VTY_NEWLINE);
      }
      if (rtadv->AdvIntervalOption)
      	vty_out (vty, "  ND router advertisements with Adv. Interval option.%s",
		 VTY_NEWLINE);
    }
}
#endif /* HAVE_RTADV */

/* Interface's information print out to vty interface. */
static void
if_dump_vty (struct vty *vty, struct interface *ifp)
{
  struct connected *connected;
  struct listnode *node;
  struct route_node *rn;
  struct zebra_if *zebra_if;

  zebra_if = ifp->info;

  vty_out (vty, "Interface %s is ", ifp->name);
  if (if_is_up(ifp)) {
    vty_out (vty, "up, line protocol ");
    
    if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION)) {
      if (if_is_running(ifp))
       vty_out (vty, "is up%s", VTY_NEWLINE);
      else
	vty_out (vty, "is down%s", VTY_NEWLINE);
    } else {
      vty_out (vty, "detection is disabled%s", VTY_NEWLINE);
    }
  } else {
    vty_out (vty, "down%s", VTY_NEWLINE);
  }

  vty_out (vty, "  Link ups:   %s%s",
           event_counter_format(&zebra_if->up_events), VTY_NEWLINE);
  vty_out (vty, "  Link downs: %s%s",
           event_counter_format(&zebra_if->down_events), VTY_NEWLINE);

  vty_out (vty, "  vrf: %u%s", ifp->vrf_id, VTY_NEWLINE);

  if (ifp->desc)
    vty_out (vty, "  Description: %s%s", ifp->desc,
	     VTY_NEWLINE);
  if (ifp->ifindex == IFINDEX_INTERNAL)
    {
      vty_out(vty, "  pseudo interface%s", VTY_NEWLINE);
      return;
    }
  else if (! CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      vty_out(vty, "  index %d inactive interface%s", 
	      ifp->ifindex, 
	      VTY_NEWLINE);
      return;
    }

  vty_out (vty, "  index %d metric %d mtu %d ",
	   ifp->ifindex, ifp->metric, ifp->mtu);
#ifdef HAVE_IPV6
  if (ifp->mtu6 != ifp->mtu)
    vty_out (vty, "mtu6 %d ", ifp->mtu6);
#endif 
  vty_out (vty, "%s  flags: %s%s", VTY_NEWLINE,
           if_flag_dump (ifp->flags), VTY_NEWLINE);
  
  /* Hardware address. */
  vty_out (vty, "  Type: %s%s", if_link_type_str (ifp->ll_type), VTY_NEWLINE);
  if (ifp->hw_addr_len != 0)
    {
      int i;

      vty_out (vty, "  HWaddr: ");
      for (i = 0; i < ifp->hw_addr_len; i++)
	vty_out (vty, "%s%02x", i == 0 ? "" : ":", ifp->hw_addr[i]);
      vty_out (vty, "%s", VTY_NEWLINE);
    }
  
  /* Bandwidth in kbps */
  if (ifp->bandwidth != 0)
    {
      vty_out(vty, "  bandwidth %u kbps", ifp->bandwidth);
      vty_out(vty, "%s", VTY_NEWLINE);
    }

  for (rn = route_top (zebra_if->ipv4_subnets); rn; rn = route_next (rn))
    {
      if (! rn->info)
	continue;

      for (ALL_LIST_ELEMENTS_RO ((struct list *)rn->info, node, connected))
        connected_dump_vty (vty, connected);
    }

  for (ALL_LIST_ELEMENTS_RO (ifp->connected, node, connected))
    {
      if (CHECK_FLAG (connected->conf, ZEBRA_IFC_REAL) &&
	  (connected->address->family == AF_INET6))
	connected_dump_vty (vty, connected);
    }

  if (HAS_LINK_PARAMS(ifp))
    {
      int i;
      struct if_link_params *iflp = ifp->link_params;
      vty_out(vty, "  Traffic Engineering Link Parameters:%s", VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_TE))
        vty_out(vty, "    TE metric %u%s",iflp->te_metric, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_MAX_BW))
        vty_out(vty, "    Maximum Bandwidth %g (Byte/s)%s", iflp->max_bw, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_MAX_RSV_BW))
        vty_out(vty, "    Maximum Reservable Bandwidth %g (Byte/s)%s", iflp->max_rsv_bw, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_UNRSV_BW)) {
        vty_out(vty, "    Unreserved Bandwidth per Class Type in Byte/s:%s", VTY_NEWLINE);
        for (i = 0; i < MAX_CLASS_TYPE; i+=2)
          vty_out(vty, "      [%d]: %g (Bytes/sec),\t[%d]: %g (Bytes/sec)%s",
                  i, iflp->unrsv_bw[i], i+1, iflp->unrsv_bw[i+1], VTY_NEWLINE);
      }

      if (IS_PARAM_SET(iflp, LP_ADM_GRP))
        vty_out(vty, "    Administrative Group:%u%s", iflp->admin_grp, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_DELAY))
        {
          vty_out(vty, "    Link Delay Average: %u (micro-sec.)", iflp->av_delay);
          if (IS_PARAM_SET(iflp, LP_MM_DELAY))
            {
              vty_out(vty, " Min:  %u (micro-sec.)", iflp->min_delay);
              vty_out(vty, " Max:  %u (micro-sec.)", iflp->max_delay);
            }
          vty_out(vty, "%s", VTY_NEWLINE);
        }
      if (IS_PARAM_SET(iflp, LP_DELAY_VAR))
        vty_out(vty, "    Link Delay Variation %u (micro-sec.)%s", iflp->delay_var, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_PKT_LOSS))
        vty_out(vty, "    Link Packet Loss %g (in %%)%s", iflp->pkt_loss, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_AVA_BW))
        vty_out(vty, "    Available Bandwidth %g (Byte/s)%s", iflp->ava_bw, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_RES_BW))
        vty_out(vty, "    Residual Bandwidth %g (Byte/s)%s", iflp->res_bw, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_USE_BW))
        vty_out(vty, "    Utilized Bandwidth %g (Byte/s)%s", iflp->use_bw, VTY_NEWLINE);
      if (IS_PARAM_SET(iflp, LP_RMT_AS))
        vty_out(vty, "    Neighbor ASBR IP: %s AS: %u %s", inet_ntoa(iflp->rmt_ip), iflp->rmt_as, VTY_NEWLINE);
    }

 #ifdef RTADV
   nd_dump_vty (vty, ifp);
 #endif /* RTADV */
#if defined (HAVE_RTADV)
  nd_dump_vty (vty, ifp);
#endif /* HAVE_RTADV */

#ifdef HAVE_PROC_NET_DEV
  /* Statistics print out using proc file system. */
  vty_out (vty, "    %lu input packets (%lu multicast), %lu bytes, "
	   "%lu dropped%s",
	   ifp->stats.rx_packets, ifp->stats.rx_multicast,
	   ifp->stats.rx_bytes, ifp->stats.rx_dropped, VTY_NEWLINE);

  vty_out (vty, "    %lu input errors, %lu length, %lu overrun,"
	   " %lu CRC, %lu frame%s",
	   ifp->stats.rx_errors, ifp->stats.rx_length_errors,
	   ifp->stats.rx_over_errors, ifp->stats.rx_crc_errors,
	   ifp->stats.rx_frame_errors, VTY_NEWLINE);

  vty_out (vty, "    %lu fifo, %lu missed%s", ifp->stats.rx_fifo_errors,
	   ifp->stats.rx_missed_errors, VTY_NEWLINE);

  vty_out (vty, "    %lu output packets, %lu bytes, %lu dropped%s",
	   ifp->stats.tx_packets, ifp->stats.tx_bytes,
	   ifp->stats.tx_dropped, VTY_NEWLINE);

  vty_out (vty, "    %lu output errors, %lu aborted, %lu carrier,"
	   " %lu fifo, %lu heartbeat%s",
	   ifp->stats.tx_errors, ifp->stats.tx_aborted_errors,
	   ifp->stats.tx_carrier_errors, ifp->stats.tx_fifo_errors,
	   ifp->stats.tx_heartbeat_errors, VTY_NEWLINE);

  vty_out (vty, "    %lu window, %lu collisions%s",
	   ifp->stats.tx_window_errors, ifp->stats.collisions, VTY_NEWLINE);
#endif /* HAVE_PROC_NET_DEV */

#ifdef HAVE_NET_RT_IFLIST
#if defined (__bsdi__) || defined (__NetBSD__)
  /* Statistics print out using sysctl (). */
  vty_out (vty, "    input packets %llu, bytes %llu, dropped %llu,"
           " multicast packets %llu%s",
           (unsigned long long)ifp->stats.ifi_ipackets,
           (unsigned long long)ifp->stats.ifi_ibytes,
           (unsigned long long)ifp->stats.ifi_iqdrops,
           (unsigned long long)ifp->stats.ifi_imcasts,
           VTY_NEWLINE);

  vty_out (vty, "    input errors %llu%s",
           (unsigned long long)ifp->stats.ifi_ierrors, VTY_NEWLINE);

  vty_out (vty, "    output packets %llu, bytes %llu,"
           " multicast packets %llu%s",
           (unsigned long long)ifp->stats.ifi_opackets,
           (unsigned long long)ifp->stats.ifi_obytes,
           (unsigned long long)ifp->stats.ifi_omcasts,
           VTY_NEWLINE);

  vty_out (vty, "    output errors %llu%s",
           (unsigned long long)ifp->stats.ifi_oerrors, VTY_NEWLINE);

  vty_out (vty, "    collisions %llu%s",
           (unsigned long long)ifp->stats.ifi_collisions, VTY_NEWLINE);
#else
  /* Statistics print out using sysctl (). */
  vty_out (vty, "    input packets %lu, bytes %lu, dropped %lu,"
	   " multicast packets %lu%s",
	   ifp->stats.ifi_ipackets, ifp->stats.ifi_ibytes,
	   ifp->stats.ifi_iqdrops, ifp->stats.ifi_imcasts,
	   VTY_NEWLINE);

  vty_out (vty, "    input errors %lu%s",
	   ifp->stats.ifi_ierrors, VTY_NEWLINE);

  vty_out (vty, "    output packets %lu, bytes %lu, multicast packets %lu%s",
	   ifp->stats.ifi_opackets, ifp->stats.ifi_obytes,
	   ifp->stats.ifi_omcasts, VTY_NEWLINE);

  vty_out (vty, "    output errors %lu%s",
	   ifp->stats.ifi_oerrors, VTY_NEWLINE);

  vty_out (vty, "    collisions %lu%s",
	   ifp->stats.ifi_collisions, VTY_NEWLINE);
#endif /* __bsdi__ || __NetBSD__ */
#endif /* HAVE_NET_RT_IFLIST */
}

/* Wrapper hook point for zebra daemon so that ifindex can be set 
 * DEFUN macro not used as extract.pl HAS to ignore this
 * See also interface_cmd in lib/if.c
 */ 
DEFUN_NOSH (zebra_interface,
	    zebra_interface_cmd,
	    "interface IFNAME",
	    "Select an interface to configure\n"
	    "Interface's name\n")
{
  int ret;
  struct interface *ifp;
  
  /* Call lib interface() */
  if ((ret = interface_cmd.func (self, vty, argc, argv)) != CMD_SUCCESS)
    return ret;

  ifp = vty->index;

  if (ifp->ifindex == IFINDEX_INTERNAL)
    /* Is this really necessary?  Shouldn't status be initialized to 0
       in that case? */
    UNSET_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE);

  return ret;
}

ALIAS (zebra_interface,
       zebra_interface_vrf_cmd,
       "interface IFNAME " VRF_CMD_STR,
       "Select an interface to configure\n"
       "Interface's name\n"
       VRF_CMD_HELP_STR)

struct cmd_node interface_node =
{
  INTERFACE_NODE,
  "%s(config-if)# ",
  1
};

/* Show all interfaces to vty. */
DEFUN (show_interface, show_interface_cmd,
       "show interface",
       SHOW_STR
       "Interface status and configuration\n")
{
  struct listnode *node;
  struct interface *ifp;
  vrf_id_t vrf_id = VRF_DEFAULT;

#ifdef HAVE_PROC_NET_DEV
  /* If system has interface statistics via proc file system, update
     statistics. */
  ifstat_update_proc ();
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
  ifstat_update_sysctl ();
#endif /* HAVE_NET_RT_IFLIST */

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  /* All interface print. */
  for (ALL_LIST_ELEMENTS_RO (vrf_iflist (vrf_id), node, ifp))
    if_dump_vty (vty, ifp);

  return CMD_SUCCESS;
}

ALIAS (show_interface,
       show_interface_vrf_cmd,
       "show interface " VRF_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       VRF_CMD_HELP_STR)

/* Show all interfaces to vty. */
DEFUN (show_interface_vrf_all, show_interface_vrf_all_cmd,
       "show interface " VRF_ALL_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct listnode *node;
  struct interface *ifp;
  vrf_iter_t iter;

#ifdef HAVE_PROC_NET_DEV
  /* If system has interface statistics via proc file system, update
     statistics. */
  ifstat_update_proc ();
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
  ifstat_update_sysctl ();
#endif /* HAVE_NET_RT_IFLIST */

  /* All interface print. */
  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    for (ALL_LIST_ELEMENTS_RO (vrf_iter2iflist (iter), node, ifp))
      if_dump_vty (vty, ifp);

  return CMD_SUCCESS;
}

/* Show specified interface to vty. */
DEFUN (show_interface_name, show_interface_name_cmd,
       "show interface IFNAME",
       SHOW_STR
       "Interface status and configuration\n"
       "Inteface name\n")
{
  struct interface *ifp;
  vrf_id_t vrf_id = VRF_DEFAULT;

#ifdef HAVE_PROC_NET_DEV
  /* If system has interface statistics via proc file system, update
     statistics. */
  ifstat_update_proc ();
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
  ifstat_update_sysctl ();
#endif /* HAVE_NET_RT_IFLIST */

  if (argc > 1)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[1]);

  /* Specified interface print. */
  ifp = if_lookup_by_name_vrf (argv[0], vrf_id);
  if (ifp == NULL)
    {
      vty_out (vty, "%% Can't find interface %s%s", argv[0],
               VTY_NEWLINE);
      return CMD_WARNING;
    }
  if_dump_vty (vty, ifp);

  return CMD_SUCCESS;
}

ALIAS (show_interface_name,
       show_interface_name_vrf_cmd,
       "show interface IFNAME " VRF_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       "Inteface name\n"
       VRF_CMD_HELP_STR)

/* Show specified interface to vty. */
DEFUN (show_interface_name_vrf_all, show_interface_name_vrf_all_cmd,
       "show interface IFNAME " VRF_ALL_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       "Inteface name\n"
       VRF_ALL_CMD_HELP_STR)
{
  struct interface *ifp;
  vrf_iter_t iter;
  int found = 0;

#ifdef HAVE_PROC_NET_DEV
  /* If system has interface statistics via proc file system, update
     statistics. */
  ifstat_update_proc ();
#endif /* HAVE_PROC_NET_DEV */
#ifdef HAVE_NET_RT_IFLIST
  ifstat_update_sysctl ();
#endif /* HAVE_NET_RT_IFLIST */

  /* All interface print. */
  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    {
      /* Specified interface print. */
      ifp = if_lookup_by_name_vrf (argv[0], vrf_iter2id (iter));
      if (ifp)
        {
          if_dump_vty (vty, ifp);
          found++;
        }
    }

  if (!found)
    {
      vty_out (vty, "%% Can't find interface %s%s", argv[0], VTY_NEWLINE);
      return CMD_WARNING;
    }

  return CMD_SUCCESS;
}

static void
if_show_description (struct vty *vty, vrf_id_t vrf_id)
{
  struct listnode *node;
  struct interface *ifp;

  vty_out (vty, "Interface       Status  Protocol  Description%s", VTY_NEWLINE);
  for (ALL_LIST_ELEMENTS_RO (vrf_iflist (vrf_id), node, ifp))
    {
      int len;

      len = vty_out (vty, "%s", ifp->name);
      vty_out (vty, "%*s", (16 - len), " ");
      
      if (if_is_up(ifp))
	{
	  vty_out (vty, "up      ");
	  if (CHECK_FLAG(ifp->status, ZEBRA_INTERFACE_LINKDETECTION))
	    {
	      if (if_is_running(ifp))
		vty_out (vty, "up        ");
	      else
		vty_out (vty, "down      ");
	    }
	  else
	    {
	      vty_out (vty, "unknown   ");
	    }
	}
      else
	{
	  vty_out (vty, "down    down      ");
	}

      if (ifp->desc)
	vty_out (vty, "%s", ifp->desc);
      vty_out (vty, "%s", VTY_NEWLINE);
    }
}

DEFUN (show_interface_desc,
       show_interface_desc_cmd,
       "show interface description",
       SHOW_STR
       "Interface status and configuration\n"
       "Interface description\n")
{
  vrf_id_t vrf_id = VRF_DEFAULT;

  if (argc > 0)
    VTY_GET_INTEGER ("VRF ID", vrf_id, argv[0]);

  if_show_description (vty, vrf_id);

  return CMD_SUCCESS;
}

ALIAS (show_interface_desc,
       show_interface_desc_vrf_cmd,
       "show interface description " VRF_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       "Interface description\n"
       VRF_CMD_HELP_STR)

DEFUN (show_interface_desc_vrf_all,
       show_interface_desc_vrf_all_cmd,
       "show interface description " VRF_ALL_CMD_STR,
       SHOW_STR
       "Interface status and configuration\n"
       "Interface description\n"
       VRF_ALL_CMD_HELP_STR)
{
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
    if (!list_isempty (vrf_iter2iflist (iter)))
      {
        vty_out (vty, "%s\tVRF %u%s%s", VTY_NEWLINE,
                 vrf_iter2id (iter),
                 VTY_NEWLINE, VTY_NEWLINE);
        if_show_description (vty, vrf_iter2id (iter));
      }

  return CMD_SUCCESS;
}

DEFUN (multicast,
       multicast_cmd,
       "multicast",
       "Set multicast flag to interface\n")
{
  int ret;
  struct interface *ifp;
  struct zebra_if *if_data;

  ifp = (struct interface *) vty->index;
  if (CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      ret = if_set_flags (ifp, IFF_MULTICAST);
      if (ret < 0)
	{
	  vty_out (vty, "Can't set multicast flag%s", VTY_NEWLINE);
	  return CMD_WARNING;
	}
      if_refresh (ifp);
    }
  if_data = ifp->info;
  if_data->multicast = IF_ZEBRA_MULTICAST_ON;

  return CMD_SUCCESS;
}

DEFUN (no_multicast,
       no_multicast_cmd,
       "no multicast",
       NO_STR
       "Unset multicast flag to interface\n")
{
  int ret;
  struct interface *ifp;
  struct zebra_if *if_data;

  ifp = (struct interface *) vty->index;
  if (CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      ret = if_unset_flags (ifp, IFF_MULTICAST);
      if (ret < 0)
	{
	  vty_out (vty, "Can't unset multicast flag%s", VTY_NEWLINE);
	  return CMD_WARNING;
	}
      if_refresh (ifp);
    }
  if_data = ifp->info;
  if_data->multicast = IF_ZEBRA_MULTICAST_OFF;

  return CMD_SUCCESS;
}

/* Hacky: create a dummy node just to hang a config-writer callback off it */
static struct cmd_node zebra_if_defaults_node = {
  ZEBRA_IF_DEFAULTS_NODE,
  "",
  1,
};

static int
config_write_zebra_if_defaults (struct vty *vty)
{
  if (zif_defaults.linkdetect != IF_LINKDETECT_UNSPEC)
    vty_out (vty, "default link-detect %s%s",
             zif_defaults.linkdetect == IF_LINKDETECT_ON ? "on" : "off",
             VTY_NEWLINE);
  return 0;
}

DEFUN(default_linkdetect,
      default_linkdetect_cmd,
      "default link-detect (on|off)",
      "Configure defaults of settings\n"
      "Interface link detection\n"
      "Interface link-detect defaults to enabled\n"
      "Interface link-detect defaults to disabled\n")
{
  zebra_if_linkdetect prev = zif_defaults.linkdetect;
  struct listnode *node;
  struct interface *ifp;
  vrf_iter_t iter;
  
  if (strcmp (argv[1], "on") == 0)
    zif_defaults.linkdetect = IF_LINKDETECT_ON;
  else
    zif_defaults.linkdetect = IF_LINKDETECT_OFF;
    
  if (zif_defaults.linkdetect != prev)
    for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
      for (ALL_LIST_ELEMENTS_RO (vrf_iter2iflist (iter), node, ifp))
        if_zebra_linkdetect_set (ifp);
  
  return CMD_SUCCESS;
}

DEFUN (linkdetect,
       linkdetect_cmd,
       "link-detect [default]",
       "Enable link detection on interface\n"
       "Leave link-detect to the default\n")
{
  struct interface *ifp;
  struct zebra_if *zif;
  
  ifp = (struct interface *) vty->index;
  zif = ifp->info;
  assert (zif != NULL);
  
  zif->linkdetect = IF_LINKDETECT_ON;
  if_zebra_linkdetect_set (ifp);
  
  /* FIXME: Will defer status change forwarding if interface
     does not come down! */

  return CMD_SUCCESS;
}


DEFUN (no_linkdetect,
       no_linkdetect_cmd,
       "no link-detect",
       NO_STR
       "Disable link detection on interface\n")
{
  struct interface *ifp;
  struct zebra_if *zif;
  
  ifp = (struct interface *) vty->index;
  zif = ifp->info;
  assert (zif != NULL);
  
  zif->linkdetect = IF_LINKDETECT_OFF;
  if_zebra_linkdetect_set (ifp);
  
  /* FIXME: see linkdetect_cmd */

  return CMD_SUCCESS;
}

DEFUN (shutdown_if,
       shutdown_if_cmd,
       "shutdown",
       "Shutdown the selected interface\n")
{
  int ret;
  struct interface *ifp;
  struct zebra_if *if_data;

  ifp = (struct interface *) vty->index;
  if (ifp->ifindex != IFINDEX_INTERNAL)
    {
        ret = if_unset_flags (ifp, IFF_UP);
        if (ret < 0)
          {
            vty_out (vty, "Can't shutdown interface%s", VTY_NEWLINE);
            return CMD_WARNING;
          }
        if_refresh (ifp);
    }
  if_data = ifp->info;
  if_data->shutdown = IF_ZEBRA_SHUTDOWN_ON;

  return CMD_SUCCESS;
}

DEFUN (no_shutdown_if,
       no_shutdown_if_cmd,
       "no shutdown",
       NO_STR
       "Shutdown the selected interface\n")
{
  int ret;
  struct interface *ifp;
  struct zebra_if *if_data;

  ifp = (struct interface *) vty->index;

  if (ifp->ifindex != IFINDEX_INTERNAL)
    {
      ret = if_set_flags (ifp, IFF_UP | IFF_RUNNING);
      if (ret < 0)
	{
	  vty_out (vty, "Can't up interface%s", VTY_NEWLINE);
	  return CMD_WARNING;
	}
      if_refresh (ifp);

      /* Some addresses (in particular, IPv6 addresses on Linux) get
       * removed when the interface goes down. They need to be readded.
       */
      if_addr_wakeup(ifp);
    }

  if_data = ifp->info;
  if_data->shutdown = IF_ZEBRA_SHUTDOWN_OFF;

  return CMD_SUCCESS;
}

DEFUN (bandwidth_if,
       bandwidth_if_cmd,
       "bandwidth <1-10000000>",
       "Set bandwidth informational parameter\n"
       "Bandwidth in kilobits\n")
{
  struct interface *ifp;   
  unsigned int bandwidth;
  
  ifp = (struct interface *) vty->index;
  bandwidth = strtol(argv[0], NULL, 10);

  /* bandwidth range is <1-10000000> */
  if (bandwidth < 1 || bandwidth > 10000000)
    {
      vty_out (vty, "Bandwidth is invalid%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
  
  ifp->bandwidth = bandwidth;

  /* force protocols to recalculate routes due to cost change */
  if (if_is_operative (ifp))
    zebra_interface_up_update (ifp);
  
  return CMD_SUCCESS;
}

DEFUN (no_bandwidth_if,
       no_bandwidth_if_cmd,
       "no bandwidth",
       NO_STR
       "Set bandwidth informational parameter\n")
{
  struct interface *ifp;   
  
  ifp = (struct interface *) vty->index;

  ifp->bandwidth = 0;
  
  /* force protocols to recalculate routes due to cost change */
  if (if_is_operative (ifp))
    zebra_interface_up_update (ifp);

  return CMD_SUCCESS;
}

ALIAS (no_bandwidth_if,
       no_bandwidth_if_val_cmd,
       "no bandwidth <1-10000000>",
       NO_STR
       "Set bandwidth informational parameter\n"
       "Bandwidth in kilobits\n")

struct cmd_node link_params_node =
{
  LINK_PARAMS_NODE,
  "%s(config-link-params)# ",
  1,
};

static void
link_param_cmd_set_uint32 (struct interface *ifp, uint32_t *field, 
                           uint32_t type, uint32_t value)
{
  /* Update field as needed */
  if (IS_PARAM_UNSET(ifp->link_params, type) || *field != value)
    {
      *field = value;
      SET_PARAM(ifp->link_params, type);

      /* force protocols to update LINK STATE due to parameters change */
      if (if_is_operative (ifp))
        zebra_interface_parameters_update (ifp);
    }
}
static void
link_param_cmd_set_float (struct interface *ifp, float *field,
                          uint32_t type, float value)
{

  /* Update field as needed */
  if (IS_PARAM_UNSET(ifp->link_params, type) || *field != value)
    {
      *field = value;
      SET_PARAM(ifp->link_params, type);

      /* force protocols to update LINK STATE due to parameters change */
      if (if_is_operative (ifp))
        zebra_interface_parameters_update (ifp);
    }
}

static void
link_param_cmd_unset (struct interface *ifp, uint32_t type)
{

  /* Unset field */
  UNSET_PARAM(ifp->link_params, type);

  /* force protocols to update LINK STATE due to parameters change */
  if (if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);
}

DEFUN (link_params,
       link_params_cmd,
       "link-params",
       LINK_PARAMS_STR)
{
  vty->node = LINK_PARAMS_NODE;
  
  return CMD_SUCCESS;
}

/* Specific Traffic Engineering parameters commands */
DEFUN (link_params_enable,
       link_params_enable_cmd,
       "enable",
       "Activate link parameters on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
    
  /* This command could be issue at startup, when activate MPLS TE */
  /* on a new interface or after a ON / OFF / ON toggle */
  /* In all case, TE parameters are reset to their default factory */
  if (IS_ZEBRA_DEBUG_EVENT)
    zlog_debug ("Link-params: enable TE link parameters on interface %s", ifp->name);
  
  if (!if_link_params_get (ifp)) 
    {
      if (IS_ZEBRA_DEBUG_EVENT)
        zlog_debug ("Link-params: failed to init TE link parameters  %s", ifp->name);
      
      return CMD_WARNING;
    }
  
  /* force protocols to update LINK STATE due to parameters change */
  if (if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);

  return CMD_SUCCESS;
}

DEFUN (no_link_params_enable,
       no_link_params_enable_cmd,
       "no enable",
       NO_STR
       "Disable link parameters on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;

  zlog_debug ("MPLS-TE: disable TE link parameters on interface %s", ifp->name);
  
  if_link_params_free (ifp);
  
  /* force protocols to update LINK STATE due to parameters change */
  if (if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);

  return CMD_SUCCESS;
}

/* STANDARD TE metrics */
DEFUN (link_params_metric,
       link_params_metric_cmd,
       "metric <0-4294967295>",
       "Link metric for MPLS-TE purpose\n"
       "Metric value in decimal\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  u_int32_t metric;

  VTY_GET_ULONG("metric", metric, argv[0]);

  /* Update TE metric if needed */
  link_param_cmd_set_uint32 (ifp, &iflp->te_metric, LP_TE, metric);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_metric,
       no_link_params_metric_cmd,
       "no metric",
       NO_STR
       "Disbale Link Metric on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset TE Metric */
  link_param_cmd_unset(ifp, LP_TE);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_maxbw,
       link_params_maxbw_cmd,
       "max-bw BANDWIDTH",
       "Maximum bandwidth that can be used\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  
  float bw;

  if (sscanf (argv[0], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_maxbw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that Maximum bandwidth is not lower than other bandwidth parameters */
  if ((bw <= iflp->max_rsv_bw)
      || (bw <= iflp->unrsv_bw[0])
      || (bw <= iflp->unrsv_bw[1])
      || (bw <= iflp->unrsv_bw[2])
      || (bw <= iflp->unrsv_bw[3])
      || (bw <= iflp->unrsv_bw[4])
      || (bw <= iflp->unrsv_bw[5])
      || (bw <= iflp->unrsv_bw[6])
      || (bw <= iflp->unrsv_bw[7])
      || (bw <= iflp->ava_bw)
      || (bw <= iflp->res_bw)
      || (bw <= iflp->use_bw))
    {
      vty_out (vty,
               "Maximum Bandwidth could not be lower than others bandwidth%s",
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Maximum Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->max_bw, LP_MAX_BW, bw);
    
  return CMD_SUCCESS;
}

DEFUN (link_params_max_rsv_bw,
       link_params_max_rsv_bw_cmd,
       "max-rsv-bw BANDWIDTH",
       "Maximum bandwidth that may be reserved\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  float bw;

  if (sscanf (argv[0], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_max_rsv_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that bandwidth is not greater than maximum bandwidth parameter */
  if (bw > iflp->max_bw)
    {
      vty_out (vty,
               "Maximum Reservable Bandwidth could not be greater than Maximum Bandwidth (%g)%s",
               iflp->max_bw, VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Maximum Reservable Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->max_rsv_bw, LP_MAX_RSV_BW, bw);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_unrsv_bw,
       link_params_unrsv_bw_cmd,
       "unrsv-bw <0-7> BANDWIDTH",
       "Unreserved bandwidth at each priority level\n"
       "Priority\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  int priority;
  float bw;

  /* We don't have to consider about range check here. */
  if (sscanf (argv[0], "%d", &priority) != 1)
    {
      vty_out (vty, "link_params_unrsv_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (sscanf (argv[1], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_unrsv_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that bandwidth is not greater than maximum bandwidth parameter */
  if (bw > iflp->max_bw)
    {
      vty_out (vty,
               "UnReserved Bandwidth could not be greater than Maximum Bandwidth (%g)%s",
               iflp->max_bw, VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Unreserved Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->unrsv_bw[priority], LP_UNRSV_BW, bw);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_admin_grp,
       link_params_admin_grp_cmd,
       "admin-grp BITPATTERN",
       "Administrative group membership\n"
       "32-bit Hexadecimal value (e.g. 0xa1)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  unsigned long value;

  if (sscanf (argv[0], "0x%lx", &value) != 1)
    {
      vty_out (vty, "link_params_admin_grp: fscanf: %s%s",
               safe_strerror (errno), VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Administrative Group if needed */
  link_param_cmd_set_uint32 (ifp, &iflp->admin_grp, LP_ADM_GRP, value);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_admin_grp,
       no_link_params_admin_grp_cmd,
       "no admin-grp",
       NO_STR
       "Disbale Administrative group membership on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Admin Group */
  link_param_cmd_unset(ifp, LP_ADM_GRP);
  
  return CMD_SUCCESS;
}

/* RFC5392 & RFC5316: INTER-AS */
DEFUN (link_params_inter_as,
       link_params_inter_as_cmd,
       "neighbor A.B.C.D as <1-4294967295>",
       "Configure remote ASBR information (Neighbor IP address and AS number)\n"
       "Remote IP address in dot decimal A.B.C.D\n"
       "Remote AS number\n"
       "AS number in the range <1-4294967295>\n")
{

  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  struct in_addr addr;
  u_int32_t as;

  if (!inet_aton (argv[0], &addr))
    {
      vty_out (vty, "Please specify Router-Addr by A.B.C.D%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
    
  VTY_GET_ULONG("AS number", as, argv[1]);
  
  /* Update Remote IP and Remote AS fields if needed */
  if (IS_PARAM_UNSET(iflp, LP_RMT_AS)
      || iflp->rmt_as != as
      || iflp->rmt_ip.s_addr != addr.s_addr)
    {

      iflp->rmt_as = as;
      iflp->rmt_ip.s_addr = addr.s_addr;
      SET_PARAM(iflp, LP_RMT_AS);

      /* force protocols to update LINK STATE due to parameters change */
      if (if_is_operative (ifp))
        zebra_interface_parameters_update (ifp);
    }
  return CMD_SUCCESS;
}

DEFUN (no_link_params_inter_as,
       no_link_params_inter_as_cmd,
       "no neighbor",
       NO_STR
       "Remove Neighbor IP address and AS number for Inter-AS TE\n")
{

  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);

  /* Reset Remote IP and AS neighbor */
  iflp->rmt_as = 0;
  iflp->rmt_ip.s_addr = 0;
  UNSET_PARAM(iflp, LP_RMT_AS);

  /* force protocols to update LINK STATE due to parameters change */
  if (if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);

  return CMD_SUCCESS;
}

/* RFC7471: OSPF Traffic Engineering (TE) Metric extensions & draft-ietf-isis-metric-extensions-07.txt */
DEFUN (link_params_delay,
       link_params_delay_cmd,
       "delay <0-16777215>",
       "Unidirectional Average Link Delay\n"
       "Average delay in micro-second as decimal (0...16777215)\n")
{

  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  u_int32_t delay = 0, low = 0, high = 0;
  u_int8_t update = 0;

  /* Get and Check new delay values */
  VTY_GET_ULONG("delay", delay, argv[0]);
  switch (argc)
    {
    case 1:
      /* Check new delay value against old Min and Max delays if set */
      if (IS_PARAM_SET(iflp, LP_MM_DELAY)
          && (delay <= iflp->min_delay || delay >= iflp->max_delay))
        {
          vty_out (vty, "Average delay should be comprise between Min (%d) and Max (%d) delay%s",
                   iflp->min_delay, iflp->max_delay, VTY_NEWLINE);
          return CMD_WARNING;
        }
      /* Update delay if value is not set or change */
      if (IS_PARAM_UNSET(iflp, LP_DELAY)|| iflp->av_delay != delay)
        {
          iflp->av_delay = delay;
          SET_PARAM(iflp, LP_DELAY);
          update = 1;
        }
      /* Unset Min and Max delays if already set */
      if (IS_PARAM_SET(iflp, LP_MM_DELAY))
        {
          iflp->min_delay = 0;
          iflp->max_delay = 0;
          UNSET_PARAM(iflp, LP_MM_DELAY);
          update = 1;
        }
      break;
    case 2:
      vty_out (vty, "You should specify both Minimum and Maximum delay with Average delay%s",
               VTY_NEWLINE);
      return CMD_WARNING;
    break;
    case 3:
      VTY_GET_ULONG("minimum delay", low, argv[1]);
      VTY_GET_ULONG("maximum delay", high, argv[2]);
      /* Check new delays value coherency */
      if (delay <= low || delay >= high)
        {
          vty_out (vty, "Average delay should be comprise between Min (%d) and Max (%d) delay%s",
                   low, high, VTY_NEWLINE);
          return CMD_WARNING;
        }
      /* Update Delays if needed */
      if (IS_PARAM_UNSET(iflp, LP_DELAY)
          || IS_PARAM_UNSET(iflp, LP_MM_DELAY)
          || iflp->av_delay != delay
          || iflp->min_delay != low
          || iflp->max_delay != high)
        {
          iflp->av_delay = delay;
          SET_PARAM(iflp, LP_DELAY);
          iflp->min_delay = low;
          iflp->max_delay = high;
          SET_PARAM(iflp, LP_MM_DELAY);
          update = 1;
        }
      break;
    default:
      return CMD_WARNING;
      break;
    }

  /* force protocols to update LINK STATE due to parameters change */
  if (update == 1 && if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);

  return CMD_SUCCESS;
}

ALIAS (link_params_delay,
       link_params_delay_mm_cmd,
       "delay <0-16777215> min <0-16777215> max <0-16777215>",
       "Unidirectional Average Link Delay (optionally Minimum and Maximum delays)\n"
       "Average delay in micro-second as decimal (0...16777215)\n"
       "Minimum delay\n"
       "Minimum delay in micro-second as decimal (0...16777215)\n"
       "Maximum delay\n"
       "Maximum delay in micro-second as decimal (0...16777215)\n")

DEFUN (no_link_params_delay,
       no_link_params_delay_cmd,
       "no delay",
       NO_STR
       "Disbale Unidirectional Average, Min & Max Link Delay on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  
  /* Unset Delays */
  iflp->av_delay = 0;
  UNSET_PARAM(iflp, LP_DELAY);
  iflp->min_delay = 0;
  iflp->max_delay = 0;
  UNSET_PARAM(iflp, LP_MM_DELAY);
  
  /* force protocols to update LINK STATE due to parameters change */
  if (if_is_operative (ifp))
    zebra_interface_parameters_update (ifp);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_delay_var,
       link_params_delay_var_cmd,
       "delay-variation <0-16777215>",
       "Unidirectional Link Delay Variation\n"
       "delay variation in micro-second as decimal (0...16777215)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  u_int32_t value;
  
  VTY_GET_ULONG("delay variation", value, argv[0]);
  
  /* Update Delay Variation if needed */
  link_param_cmd_set_uint32 (ifp, &iflp->delay_var, LP_DELAY_VAR, value);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_delay_var,
       no_link_params_delay_var_cmd,
       "no delay-variation",
       NO_STR
       "Disbale Unidirectional Delay Variation on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Delay Variation */
  link_param_cmd_unset(ifp, LP_DELAY_VAR);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_pkt_loss,
       link_params_pkt_loss_cmd,
       "packet-loss PERCENTAGE",
       "Unidirectional Link Packet Loss\n"
       "percentage of total traffic by 0.000003% step and less than 50.331642%\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  float fval;

  if (sscanf (argv[0], "%g", &fval) != 1)
    {
      vty_out (vty, "link_params_pkt_loss: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  if (fval > MAX_PKT_LOSS)
    fval = MAX_PKT_LOSS;

  /* Update Packet Loss if needed */
  link_param_cmd_set_float (ifp, &iflp->pkt_loss, LP_PKT_LOSS, fval);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_pkt_loss,
       no_link_params_pkt_loss_cmd,
       "no packet-loss",
       NO_STR
       "Disbale Unidirectional Link Packet Loss on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Packet Loss */
  link_param_cmd_unset(ifp, LP_PKT_LOSS);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_res_bw,
       link_params_res_bw_cmd,
       "res-bw BANDWIDTH",
       "Unidirectional Residual Bandwidth\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  float bw;

  if (sscanf (argv[0], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_res_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that bandwidth is not greater than maximum bandwidth parameter */
  if (bw > iflp->max_bw)
    {
      vty_out (vty,
               "Residual Bandwidth could not be greater than Maximum Bandwidth (%g)%s",
               iflp->max_bw, VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Residual Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->res_bw, LP_RES_BW, bw);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_res_bw,
       no_link_params_res_bw_cmd,
       "no res-bw",
       NO_STR
       "Disbale Unidirectional Residual Bandwidth on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Residual Bandwidth */
  link_param_cmd_unset(ifp, LP_RES_BW);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_ava_bw,
       link_params_ava_bw_cmd,
       "ava-bw BANDWIDTH",
       "Unidirectional Available Bandwidth\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  float bw;

  if (sscanf (argv[0], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_ava_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that bandwidth is not greater than maximum bandwidth parameter */
  if (bw > iflp->max_bw)
    {
      vty_out (vty,
               "Available Bandwidth could not be greater than Maximum Bandwidth (%g)%s",
               iflp->max_bw, VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Residual Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->ava_bw, LP_AVA_BW, bw);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_ava_bw,
       no_link_params_ava_bw_cmd,
       "no ava-bw",
       NO_STR
       "Disbale Unidirectional Available Bandwidth on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Available Bandwidth */
  link_param_cmd_unset(ifp, LP_AVA_BW);
  
  return CMD_SUCCESS;
}

DEFUN (link_params_use_bw,
       link_params_use_bw_cmd,
       "use-bw BANDWIDTH",
       "Unidirectional Utilised Bandwidth\n"
       "Bytes/second (IEEE floating point format)\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  struct if_link_params *iflp = if_link_params_get (ifp);
  float bw;

  if (sscanf (argv[0], "%g", &bw) != 1)
    {
      vty_out (vty, "link_params_use_bw: fscanf: %s%s", safe_strerror (errno),
               VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check that bandwidth is not greater than maximum bandwidth parameter */
  if (bw > iflp->max_bw)
    {
      vty_out (vty,
               "Utilised Bandwidth could not be greater than Maximum Bandwidth (%g)%s",
               iflp->max_bw, VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Update Utilized Bandwidth if needed */
  link_param_cmd_set_float (ifp, &iflp->use_bw, LP_USE_BW, bw);
  
  return CMD_SUCCESS;
}

DEFUN (no_link_params_use_bw,
       no_link_params_use_bw_cmd,
       "no use-bw",
       NO_STR
       "Disbale Unidirectional Utilised Bandwidth on this interface\n")
{
  struct interface *ifp = (struct interface *) vty->index;
  
  /* Unset Utilised Bandwidth */
  link_param_cmd_unset(ifp, LP_USE_BW);
  
  return CMD_SUCCESS;
}

static int
ip_address_install (struct vty *vty, struct interface *ifp,
		    const char *addr_str, const char *peer_str,
		    const char *label)
{
  struct zebra_if *if_data;
  struct prefix_ipv4 cp;
  struct connected *ifc;
  struct prefix_ipv4 *p;
  int ret;

  if_data = ifp->info;

  ret = str2prefix_ipv4 (addr_str, &cp);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address %s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  ifc = connected_check (ifp, (struct prefix *) &cp);
  if (! ifc)
    {
      ifc = connected_new ();
      ifc->ifp = ifp;

      /* Address. */
      p = prefix_ipv4_new ();
      *p = cp;
      ifc->address = (struct prefix *) p;

      /* Broadcast. */
      if (p->prefixlen <= IPV4_MAX_PREFIXLEN-2)
	{
	  p = prefix_ipv4_new ();
	  *p = cp;
	  p->prefix.s_addr = ipv4_broadcast_addr(p->prefix.s_addr,p->prefixlen);
	  ifc->destination = (struct prefix *) p;
	}

      /* Label. */
      if (label)
	ifc->label = XSTRDUP (MTYPE_CONNECTED_LABEL, label);

      /* Add to linked list. */
      listnode_add (ifp->connected, ifc);
    }

  /* This address is configured from zebra. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
    SET_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED);

  /* In case of this route need to install kernel. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_QUEUED)
      && CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE)
      && !(if_data && if_data->shutdown == IF_ZEBRA_SHUTDOWN_ON))
    {
      /* Some system need to up the interface to set IP address. */
      if (! if_is_up (ifp))
	{
	  if_set_flags (ifp, IFF_UP | IFF_RUNNING);
	  if_refresh (ifp);
	}

      ret = if_set_prefix (ifp, ifc);
      if (ret < 0)
	{
	  vty_out (vty, "%% Can't set interface IP address: %s.%s", 
		   safe_strerror(errno), VTY_NEWLINE);
	  return CMD_WARNING;
	}

      SET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
      /* The address will be advertised to zebra clients when the notification
       * from the kernel has been received.
       * It will also be added to the subnet chain list, then. */
    }

  return CMD_SUCCESS;
}

static int
ip_address_uninstall (struct vty *vty, struct interface *ifp,
		      const char *addr_str, const char *peer_str,
		      const char *label)
{
  struct prefix_ipv4 cp;
  struct connected *ifc;
  int ret;

  /* Convert to prefix structure. */
  ret = str2prefix_ipv4 (addr_str, &cp);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address %s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check current interface address. */
  ifc = connected_check (ifp, (struct prefix *) &cp);
  if (! ifc)
    {
      vty_out (vty, "%% Can't find address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* This is not configured address. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
    return CMD_WARNING;

  UNSET_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED);
  
  /* This is not real address or interface is not active. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_QUEUED)
      || ! CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      listnode_delete (ifp->connected, ifc);
      connected_free (ifc);
      return CMD_WARNING;
    }

  /* This is real route. */
  ret = if_unset_prefix (ifp, ifc);
  if (ret < 0)
    {
      vty_out (vty, "%% Can't unset interface IP address: %s.%s", 
	       safe_strerror(errno), VTY_NEWLINE);
      return CMD_WARNING;
    }
  UNSET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
  /* we will receive a kernel notification about this route being removed.
   * this will trigger its removal from the connected list. */
  return CMD_SUCCESS;
}

DEFUN (ip_address,
       ip_address_cmd,
       "ip address A.B.C.D/M",
       "Interface Internet Protocol config commands\n"
       "Set the IP address of an interface\n"
       "IP address (e.g. 10.0.0.1/8)\n")
{
  return ip_address_install (vty, vty->index, argv[0], NULL, NULL);
}

DEFUN (no_ip_address,
       no_ip_address_cmd,
       "no ip address A.B.C.D/M",
       NO_STR
       "Interface Internet Protocol config commands\n"
       "Set the IP address of an interface\n"
       "IP Address (e.g. 10.0.0.1/8)")
{
  return ip_address_uninstall (vty, vty->index, argv[0], NULL, NULL);
}

#ifdef HAVE_NETLINK
DEFUN (ip_address_label,
       ip_address_label_cmd,
       "ip address A.B.C.D/M label LINE",
       "Interface Internet Protocol config commands\n"
       "Set the IP address of an interface\n"
       "IP address (e.g. 10.0.0.1/8)\n"
       "Label of this address\n"
       "Label\n")
{
  return ip_address_install (vty, vty->index, argv[0], NULL, argv[1]);
}

DEFUN (no_ip_address_label,
       no_ip_address_label_cmd,
       "no ip address A.B.C.D/M label LINE",
       NO_STR
       "Interface Internet Protocol config commands\n"
       "Set the IP address of an interface\n"
       "IP address (e.g. 10.0.0.1/8)\n"
       "Label of this address\n"
       "Label\n")
{
  return ip_address_uninstall (vty, vty->index, argv[0], NULL, argv[1]);
}
#endif /* HAVE_NETLINK */

#ifdef HAVE_IPV6
static int
ipv6_address_install (struct vty *vty, struct interface *ifp,
		      const char *addr_str, const char *peer_str,
		      const char *label, int secondary)
{
  struct zebra_if *if_data;
  struct prefix_ipv6 cp;
  struct connected *ifc;
  struct prefix_ipv6 *p;
  int ret;

  if_data = ifp->info;

  ret = str2prefix_ipv6 (addr_str, &cp);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address %s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  ifc = connected_check (ifp, (struct prefix *) &cp);
  if (! ifc)
    {
      ifc = connected_new ();
      ifc->ifp = ifp;

      /* Address. */
      p = prefix_ipv6_new ();
      *p = cp;
      ifc->address = (struct prefix *) p;

      /* Secondary. */
      if (secondary)
	SET_FLAG (ifc->flags, ZEBRA_IFA_SECONDARY);

      /* Label. */
      if (label)
	ifc->label = XSTRDUP (MTYPE_CONNECTED_LABEL, label);

      /* Add to linked list. */
      listnode_add (ifp->connected, ifc);
    }

  /* This address is configured from zebra. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
    SET_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED);

  /* In case of this route need to install kernel. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_QUEUED)
      && CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE)
      && !(if_data && if_data->shutdown == IF_ZEBRA_SHUTDOWN_ON))
    {
      /* Some system need to up the interface to set IP address. */
      if (! if_is_up (ifp))
	{
	  if_set_flags (ifp, IFF_UP | IFF_RUNNING);
	  if_refresh (ifp);
	}

      ret = if_prefix_add_ipv6 (ifp, ifc);

      if (ret < 0)
	{
	  vty_out (vty, "%% Can't set interface IP address: %s.%s", 
		   safe_strerror(errno), VTY_NEWLINE);
	  return CMD_WARNING;
	}

      SET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
      /* The address will be advertised to zebra clients when the notification
       * from the kernel has been received. */
    }

  return CMD_SUCCESS;
}

static int
ipv6_address_uninstall (struct vty *vty, struct interface *ifp,
			const char *addr_str, const char *peer_str,
			const char *label, int secondry)
{
  struct prefix_ipv6 cp;
  struct connected *ifc;
  int ret;

  /* Convert to prefix structure. */
  ret = str2prefix_ipv6 (addr_str, &cp);
  if (ret <= 0)
    {
      vty_out (vty, "%% Malformed address %s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Check current interface address. */
  ifc = connected_check (ifp, (struct prefix *) &cp);
  if (! ifc)
    {
      vty_out (vty, "%% Can't find address%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* This is not configured address. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
    return CMD_WARNING;

  UNSET_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED);

  /* This is not real address or interface is not active. */
  if (! CHECK_FLAG (ifc->conf, ZEBRA_IFC_QUEUED)
      || ! CHECK_FLAG (ifp->status, ZEBRA_INTERFACE_ACTIVE))
    {
      listnode_delete (ifp->connected, ifc);
      connected_free (ifc);
      return CMD_WARNING;
    }

  /* This is real route. */
  ret = if_prefix_delete_ipv6 (ifp, ifc);
  if (ret < 0)
    {
      vty_out (vty, "%% Can't unset interface IP address: %s.%s", 
	       safe_strerror(errno), VTY_NEWLINE);
      return CMD_WARNING;
    }

  UNSET_FLAG (ifc->conf, ZEBRA_IFC_QUEUED);
  /* This information will be propagated to the zclients when the
   * kernel notification is received. */
  return CMD_SUCCESS;
}

DEFUN (ipv6_address,
       ipv6_address_cmd,
       "ipv6 address X:X::X:X/M",
       "Interface IPv6 config commands\n"
       "Set the IP address of an interface\n"
       "IPv6 address (e.g. 3ffe:506::1/48)\n")
{
  return ipv6_address_install (vty, vty->index, argv[0], NULL, NULL, 0);
}

DEFUN (no_ipv6_address,
       no_ipv6_address_cmd,
       "no ipv6 address X:X::X:X/M",
       NO_STR
       "Interface IPv6 config commands\n"
       "Set the IP address of an interface\n"
       "IPv6 address (e.g. 3ffe:506::1/48)\n")
{
  return ipv6_address_uninstall (vty, vty->index, argv[0], NULL, NULL, 0);
}
#endif /* HAVE_IPV6 */

static int
link_params_config_write (struct vty *vty, struct interface *ifp)
{
  int i;
  
  if ((ifp == NULL) || !HAS_LINK_PARAMS(ifp))
    return -1;
  
  struct if_link_params *iflp = ifp->link_params;

  vty_out (vty, " link-params%s", VTY_NEWLINE);
  vty_out(vty, "  enable%s", VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_TE))
    vty_out(vty, "  metric %u%s",iflp->te_metric, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_MAX_BW))
    vty_out(vty, "  max-bw %g%s", iflp->max_bw, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_MAX_RSV_BW))
    vty_out(vty, "  max-rsv-bw %g%s", iflp->max_rsv_bw, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_UNRSV_BW))
    {
      for (i = 0; i < 8; i++)
        vty_out(vty, "  unrsv-bw %d %g%s",
            i, iflp->unrsv_bw[i], VTY_NEWLINE);
    }
  if (IS_PARAM_SET(iflp, LP_ADM_GRP))
    vty_out(vty, "  admin-grp %u%s", iflp->admin_grp, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_DELAY))
    {
      vty_out(vty, "  delay %u", iflp->av_delay);
      if (IS_PARAM_SET(iflp, LP_MM_DELAY))
        {
          vty_out(vty, " min %u", iflp->min_delay);
          vty_out(vty, " max %u", iflp->max_delay);
        }
      vty_out(vty, "%s", VTY_NEWLINE);
    }
  if (IS_PARAM_SET(iflp, LP_DELAY_VAR))
    vty_out(vty, "  delay-variation %u%s", iflp->delay_var, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_PKT_LOSS))
    vty_out(vty, "  packet-loss %g%s", iflp->pkt_loss, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_AVA_BW))
    vty_out(vty, "  ava-bw %g%s", iflp->ava_bw, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_RES_BW))
    vty_out(vty, "  res-bw %g%s", iflp->res_bw, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_USE_BW))
    vty_out(vty, "  use-bw %g%s", iflp->use_bw, VTY_NEWLINE);
  if (IS_PARAM_SET(iflp, LP_RMT_AS))
    vty_out(vty, "  neighbor %s as %u%s", inet_ntoa(iflp->rmt_ip),
        iflp->rmt_as, VTY_NEWLINE);
  return 0;
}

static int
if_config_write (struct vty *vty)
{
  struct listnode *node;
  struct interface *ifp;
  vrf_iter_t iter;

  for (iter = vrf_first (); iter != VRF_ITER_INVALID; iter = vrf_next (iter))
  for (ALL_LIST_ELEMENTS_RO (vrf_iter2iflist (iter), node, ifp))
    {
      struct zebra_if *if_data;
      struct listnode *addrnode;
      struct connected *ifc;
      struct prefix *p;

      if_data = ifp->info;

      if (ifp->vrf_id == VRF_DEFAULT)
        vty_out (vty, "interface %s%s", ifp->name, VTY_NEWLINE);
      else
        vty_out (vty, "interface %s vrf %u%s", ifp->name, ifp->vrf_id,
                 VTY_NEWLINE);

      if (if_data)
	{
	  if (if_data->shutdown == IF_ZEBRA_SHUTDOWN_ON)
	    vty_out (vty, " shutdown%s", VTY_NEWLINE);
	}

      if (ifp->desc)
	vty_out (vty, " description %s%s", ifp->desc,
		 VTY_NEWLINE);

      /* Assign bandwidth here to avoid unnecessary interface flap
	 while processing config script */
      if (ifp->bandwidth != 0)
	vty_out(vty, " bandwidth %u%s", ifp->bandwidth, VTY_NEWLINE); 
      
      switch (if_data->linkdetect)
        {
          case IF_LINKDETECT_ON:
            vty_out(vty, " link-detect%s", VTY_NEWLINE);
            break;
          case IF_LINKDETECT_OFF:
            vty_out(vty, " no link-detect%s", VTY_NEWLINE);
            break;
          default: break;
        }
      
      for (ALL_LIST_ELEMENTS_RO (ifp->connected, addrnode, ifc))
	  {
	    if (CHECK_FLAG (ifc->conf, ZEBRA_IFC_CONFIGURED))
	      {
		char buf[INET6_ADDRSTRLEN];
		p = ifc->address;
		vty_out (vty, " ip%s address %s",
			 p->family == AF_INET ? "" : "v6",
			 prefix2str (p, buf, sizeof(buf)));

		if (ifc->label)
		  vty_out (vty, " label %s", ifc->label);

		vty_out (vty, "%s", VTY_NEWLINE);
	      }
	  }

      if (if_data)
	{
	  if (if_data->multicast != IF_ZEBRA_MULTICAST_UNSPEC)
	    vty_out (vty, " %smulticast%s",
		     if_data->multicast == IF_ZEBRA_MULTICAST_ON ? "" : "no ",
		     VTY_NEWLINE);
	}

#if defined (HAVE_RTADV)
      rtadv_config_write (vty, ifp);
#endif /* HAVE_RTADV */

#ifdef HAVE_IRDP
      irdp_config_write (vty, ifp);
#endif /* IRDP */

      link_params_config_write (vty, ifp);

      vty_out (vty, "!%s", VTY_NEWLINE);
    }
  return 0;
}


/* Allocate and initialize interface vector. */
void
zebra_if_init (void)
{
  /* Initialize interface and new hook. */
  if_add_hook (IF_NEW_HOOK, if_zebra_new_hook);
  if_add_hook (IF_DELETE_HOOK, if_zebra_delete_hook);
  
  /* Install configuration write function. */
  install_node (&interface_node, if_config_write);
  
  install_node (&zebra_if_defaults_node, config_write_zebra_if_defaults);

  install_node (&link_params_node, NULL);
  
  install_element (VIEW_NODE, &show_interface_cmd);
  install_element (VIEW_NODE, &show_interface_vrf_cmd);
  install_element (VIEW_NODE, &show_interface_vrf_all_cmd);
  install_element (VIEW_NODE, &show_interface_name_cmd);
  install_element (VIEW_NODE, &show_interface_name_vrf_cmd);
  install_element (VIEW_NODE, &show_interface_name_vrf_all_cmd);
  install_element (CONFIG_NODE, &zebra_interface_cmd);
  install_element (CONFIG_NODE, &zebra_interface_vrf_cmd);
  install_element (CONFIG_NODE, &no_interface_cmd);
  install_element (CONFIG_NODE, &no_interface_vrf_cmd);
  install_element (CONFIG_NODE, &default_linkdetect_cmd);
  install_default (INTERFACE_NODE);
  install_element (INTERFACE_NODE, &interface_desc_cmd);
  install_element (INTERFACE_NODE, &no_interface_desc_cmd);
  install_element (INTERFACE_NODE, &multicast_cmd);
  install_element (INTERFACE_NODE, &no_multicast_cmd);
  install_element (INTERFACE_NODE, &linkdetect_cmd);
  install_element (INTERFACE_NODE, &no_linkdetect_cmd);
  install_element (INTERFACE_NODE, &shutdown_if_cmd);
  install_element (INTERFACE_NODE, &no_shutdown_if_cmd);
  install_element (INTERFACE_NODE, &bandwidth_if_cmd);
  install_element (INTERFACE_NODE, &no_bandwidth_if_cmd);
  install_element (INTERFACE_NODE, &no_bandwidth_if_val_cmd);
  install_element (INTERFACE_NODE, &ip_address_cmd);
  install_element (INTERFACE_NODE, &no_ip_address_cmd);
#ifdef HAVE_IPV6
  install_element (INTERFACE_NODE, &ipv6_address_cmd);
  install_element (INTERFACE_NODE, &no_ipv6_address_cmd);
#endif /* HAVE_IPV6 */
#ifdef HAVE_NETLINK
  install_element (INTERFACE_NODE, &ip_address_label_cmd);
  install_element (INTERFACE_NODE, &no_ip_address_label_cmd);
#endif /* HAVE_NETLINK */
  install_element(INTERFACE_NODE, &link_params_cmd);
  install_default(LINK_PARAMS_NODE);
  install_element(LINK_PARAMS_NODE, &link_params_enable_cmd);
  install_element(LINK_PARAMS_NODE, &no_link_params_enable_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_metric_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_maxbw_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_max_rsv_bw_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_unrsv_bw_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_admin_grp_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_inter_as_cmd);
  install_element(LINK_PARAMS_NODE, &no_link_params_inter_as_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_delay_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_delay_mm_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_delay_var_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_pkt_loss_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_ava_bw_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_res_bw_cmd);
  install_element(LINK_PARAMS_NODE, &link_params_use_bw_cmd);
}
