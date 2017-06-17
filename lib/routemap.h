/* Route map function.
 * Copyright (C) 1998 Kunihiro Ishiguro
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

#ifndef _ZEBRA_ROUTEMAP_H
#define _ZEBRA_ROUTEMAP_H

#include "prefix.h"

/* Route map's type. */
enum route_map_type
{
  RMAP_PERMIT,
  RMAP_DENY,
  RMAP_ANY
};

typedef enum 
{
  RMAP_MATCH,
  RMAP_DENYMATCH,
  RMAP_NOMATCH,
  RMAP_ERROR,
  RMAP_OKAY
} route_map_result_t;

typedef enum
{
  RMAP_RIP,
  RMAP_RIPNG,
  RMAP_BABEL,
  RMAP_OSPF,
  RMAP_OSPF6,
  RMAP_BGP,
  RMAP_ZEBRA,
  RMAP_ISIS,
} route_map_object_t;

typedef enum
{
  RMAP_EXIT,
  RMAP_GOTO,
  RMAP_NEXT
} route_map_end_t;

typedef enum
{
  RMAP_EVENT_SET_ADDED,
  RMAP_EVENT_SET_DELETED,
  RMAP_EVENT_SET_REPLACED,
  RMAP_EVENT_MATCH_ADDED,
  RMAP_EVENT_MATCH_DELETED,
  RMAP_EVENT_MATCH_REPLACED,
  RMAP_EVENT_INDEX_ADDED,
  RMAP_EVENT_INDEX_DELETED
} route_map_event_t;

/* Depth limit in RMAP recursion using RMAP_CALL. */
#define RMAP_RECURSION_LIMIT      10

/* Route map rule structure for matching and setting. */
struct route_map_rule_cmd
{
  /* Route map rule name (e.g. as-path, metric) */
  const char *str;

  /* Function for value set or match. */
  route_map_result_t (*func_apply)(void *, struct prefix *, 
				   route_map_object_t, void *);

  /* Compile argument and return result as void *. */
  void *(*func_compile)(const char *);

  /* Free allocated value by func_compile (). */
  void (*func_free)(void *);
};

/* Route map apply error. */
enum
{
  /* Route map rule is missing. */
  RMAP_RULE_MISSING = 1,

  /* Route map rule can't compile */
  RMAP_COMPILE_ERROR
};

/* Route map rule list. */
struct route_map_rule_list
{
  struct route_map_rule *head;
  struct route_map_rule *tail;
};

/* Route map index structure. */
struct route_map_index
{
  struct route_map *map;
  char *description;

  /* Preference of this route map rule. */
  int pref;

  /* Route map type permit or deny. */
  enum route_map_type type;			

  /* Do we follow old rules, or hop forward? */
  route_map_end_t exitpolicy;

  /* If we're using "GOTO", to where do we go? */
  int nextpref;

  /* If we're using "CALL", to which route-map do ew go? */
  char *nextrm;

  /* Matching rule list. */
  struct route_map_rule_list match_list;
  struct route_map_rule_list set_list;

  /* Make linked list. */
  struct route_map_index *next;
  struct route_map_index *prev;
};

/* Route map list structure. */
struct route_map
{
  /* Name of route map. */
  char *name;

  /* Route map's rule. */
  struct route_map_index *head;
  struct route_map_index *tail;

  /* Make linked list. */
  struct route_map *next;
  struct route_map *prev;
};

/* Prototypes. */
extern void route_map_init (void);
extern void route_map_init_vty (void);
extern void route_map_finish (void);

/* Add match statement to route map. */
extern int route_map_add_match (struct route_map_index *index,
		                const char *match_name,
		                const char *match_arg);

/* Delete specified route match rule. */
extern int route_map_delete_match (struct route_map_index *index,
			           const char *match_name,
			           const char *match_arg);

/* Add route-map set statement to the route map. */
extern int route_map_add_set (struct route_map_index *index, 
		              const char *set_name,
		              const char *set_arg);

/* Delete route map set rule. */
extern int route_map_delete_set (struct route_map_index *index,
                                 const char *set_name,
                                 const char *set_arg);

/* Install rule command to the match list. */
extern void route_map_install_match (struct route_map_rule_cmd *cmd);

/* Install rule command to the set list. */
extern void route_map_install_set (struct route_map_rule_cmd *cmd);

/* Lookup route map by name. */
extern struct route_map * route_map_lookup_by_name (const char *name);

/* Apply route map to the object. */
extern route_map_result_t route_map_apply (struct route_map *map,
                                           struct prefix *,
                                           route_map_object_t object_type,
                                           void *object);

extern void route_map_add_hook (void (*func) (const char *));
extern void route_map_delete_hook (void (*func) (const char *));
extern void route_map_event_hook (void (*func) (route_map_event_t, const char *));

extern void *route_map_rule_tag_compile (const char *arg);
extern void route_map_rule_tag_free (void *rule);

#endif /* _ZEBRA_ROUTEMAP_H */
