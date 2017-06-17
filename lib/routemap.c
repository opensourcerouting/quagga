/* Route map function.
   Copyright (C) 1998, 1999 Kunihiro Ishiguro

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

#include "linklist.h"
#include "memory.h"
#include "vector.h"
#include "prefix.h"
#include "routemap.h"
#include "command.h"
#include "vty.h"
#include "log.h"

/* Vector for route match rules. */
static vector route_match_vec;

/* Vector for route set rules. */
static vector route_set_vec;

/* Route map rule. This rule has both `match' rule and `set' rule. */
struct route_map_rule
{
  /* Rule type. */
  struct route_map_rule_cmd *cmd;

  /* For pretty printing. */
  char *rule_str;

  /* Pre-compiled match rule. */
  void *value;

  /* Linked list. */
  struct route_map_rule *next;
  struct route_map_rule *prev;
};

/* Making route map list. */
struct route_map_list
{
  struct route_map *head;
  struct route_map *tail;

  void (*add_hook) (const char *);
  void (*delete_hook) (const char *);
  void (*event_hook) (route_map_event_t, const char *); 
};

/* Master list of route map. */
static struct route_map_list route_map_master = { NULL, NULL, NULL, NULL };

static void
route_map_rule_delete (struct route_map_rule_list *,
		       struct route_map_rule *);

static void
route_map_index_delete (struct route_map_index *, int);

/* New route map allocation. Please note route map's name must be
   specified. */
static struct route_map *
route_map_new (const char *name)
{
  struct route_map *new;

  new =  XCALLOC (MTYPE_ROUTE_MAP, sizeof (struct route_map));
  new->name = XSTRDUP (MTYPE_ROUTE_MAP_NAME, name);
  return new;
}

/* Add new name to route_map. */
static struct route_map *
route_map_add (const char *name)
{
  struct route_map *map;
  struct route_map_list *list;

  map = route_map_new (name);
  list = &route_map_master;
    
  map->next = NULL;
  map->prev = list->tail;
  if (list->tail)
    list->tail->next = map;
  else
    list->head = map;
  list->tail = map;

  /* Execute hook. */
  if (route_map_master.add_hook)
    (*route_map_master.add_hook) (name);

  return map;
}

/* Route map delete from list. */
static void
route_map_delete (struct route_map *map)
{
  struct route_map_list *list;
  struct route_map_index *index;
  char *name;
  
  while ((index = map->head) != NULL)
    route_map_index_delete (index, 0);

  name = map->name;

  list = &route_map_master;

  if (map->next)
    map->next->prev = map->prev;
  else
    list->tail = map->prev;

  if (map->prev)
    map->prev->next = map->next;
  else
    list->head = map->next;

  XFREE (MTYPE_ROUTE_MAP, map);

  /* Execute deletion hook. */
  if (route_map_master.delete_hook)
    (*route_map_master.delete_hook) (name);

  if (name)
    XFREE (MTYPE_ROUTE_MAP_NAME, name);

}

/* Lookup route map by route map name string. */
struct route_map *
route_map_lookup_by_name (const char *name)
{
  struct route_map *map;

  for (map = route_map_master.head; map; map = map->next)
    if (strcmp (map->name, name) == 0)
      return map;
  return NULL;
}

/* Lookup route map.  If there isn't route map create one and return
   it. */
static struct route_map *
route_map_get (const char *name)
{
  struct route_map *map;

  map = route_map_lookup_by_name (name);
  if (map == NULL)
    map = route_map_add (name);
  return map;
}

/* Return route map's type string. */
static const char *
route_map_type_str (enum route_map_type type)
{
  switch (type)
    {
    case RMAP_PERMIT:
      return "permit";
      break;
    case RMAP_DENY:
      return "deny";
      break;
    default:
      return "";
      break;
    }
}

static int
route_map_empty (struct route_map *map)
{
  if (map->head == NULL && map->tail == NULL)
    return 1;
  else
    return 0;
}

/* show route-map */
static void
vty_show_route_map_entry (struct vty *vty, struct route_map *map)
{
  struct route_map_index *index;
  struct route_map_rule *rule;

  /* Print the name of the protocol */
  if (zlog_default)
    vty_out (vty, "%s:%s", zlog_proto_names[zlog_default->protocol],
             VTY_NEWLINE);

  for (index = map->head; index; index = index->next)
    {
      vty_out (vty, "route-map %s, %s, sequence %d%s",
               map->name, route_map_type_str (index->type),
               index->pref, VTY_NEWLINE);

      /* Description */
      if (index->description)
	vty_out (vty, "  Description:%s    %s%s", VTY_NEWLINE,
		 index->description, VTY_NEWLINE);
      
      /* Match clauses */
      vty_out (vty, "  Match clauses:%s", VTY_NEWLINE);
      for (rule = index->match_list.head; rule; rule = rule->next)
        vty_out (vty, "    %s %s%s", 
                 rule->cmd->str, rule->rule_str, VTY_NEWLINE);
      
      vty_out (vty, "  Set clauses:%s", VTY_NEWLINE);
      for (rule = index->set_list.head; rule; rule = rule->next)
        vty_out (vty, "    %s %s%s",
                 rule->cmd->str, rule->rule_str, VTY_NEWLINE);
      
      /* Call clause */
      vty_out (vty, "  Call clause:%s", VTY_NEWLINE);
      if (index->nextrm)
        vty_out (vty, "    Call %s%s", index->nextrm, VTY_NEWLINE);
      
      /* Exit Policy */
      vty_out (vty, "  Action:%s", VTY_NEWLINE);
      if (index->exitpolicy == RMAP_GOTO)
        vty_out (vty, "    Goto %d%s", index->nextpref, VTY_NEWLINE);
      else if (index->exitpolicy == RMAP_NEXT)
        vty_out (vty, "    Continue to next entry%s", VTY_NEWLINE);
      else if (index->exitpolicy == RMAP_EXIT)
        vty_out (vty, "    Exit routemap%s", VTY_NEWLINE);
    }
}

static int
vty_show_route_map (struct vty *vty, const char *name)
{
  struct route_map *map;

  if (name)
    {
      map = route_map_lookup_by_name (name);

      if (map)
        {
          vty_show_route_map_entry (vty, map);
          return CMD_SUCCESS;
        }
      else
        {
          vty_out (vty, "%%route-map %s not found%s", name, VTY_NEWLINE);
          return CMD_WARNING;
        }
    }
  else
    {
      for (map = route_map_master.head; map; map = map->next)
	vty_show_route_map_entry (vty, map);
    }
  return CMD_SUCCESS;
}


/* New route map allocation. Please note route map's name must be
   specified. */
static struct route_map_index *
route_map_index_new (void)
{
  struct route_map_index *new;

  new =  XCALLOC (MTYPE_ROUTE_MAP_INDEX, sizeof (struct route_map_index));
  new->exitpolicy = RMAP_EXIT; /* Default to Cisco-style */
  return new;
}

/* Free route map index. */
static void
route_map_index_delete (struct route_map_index *index, int notify)
{
  struct route_map_rule *rule;

  /* Free route match. */
  while ((rule = index->match_list.head) != NULL)
    route_map_rule_delete (&index->match_list, rule);

  /* Free route set. */
  while ((rule = index->set_list.head) != NULL)
    route_map_rule_delete (&index->set_list, rule);

  /* Remove index from route map list. */
  if (index->next)
    index->next->prev = index->prev;
  else
    index->map->tail = index->prev;

  if (index->prev)
    index->prev->next = index->next;
  else
    index->map->head = index->next;

  /* Free 'char *nextrm' if not NULL */
  if (index->nextrm)
    XFREE (MTYPE_ROUTE_MAP_NAME, index->nextrm);

    /* Execute event hook. */
  if (route_map_master.event_hook && notify)
    (*route_map_master.event_hook) (RMAP_EVENT_INDEX_DELETED,
				    index->map->name);

  XFREE (MTYPE_ROUTE_MAP_INDEX, index);
}

/* Lookup index from route map. */
static struct route_map_index *
route_map_index_lookup (struct route_map *map, enum route_map_type type,
			int pref)
{
  struct route_map_index *index;

  for (index = map->head; index; index = index->next)
    if ((index->type == type || type == RMAP_ANY)
	&& index->pref == pref)
      return index;
  return NULL;
}

/* Add new index to route map. */
static struct route_map_index *
route_map_index_add (struct route_map *map, enum route_map_type type,
		     int pref)
{
  struct route_map_index *index;
  struct route_map_index *point;

  /* Allocate new route map inex. */
  index = route_map_index_new ();
  index->map = map;
  index->type = type;
  index->pref = pref;
  
  /* Compare preference. */
  for (point = map->head; point; point = point->next)
    if (point->pref >= pref)
      break;

  if (map->head == NULL)
    {
      map->head = map->tail = index;
    }
  else if (point == NULL)
    {
      index->prev = map->tail;
      map->tail->next = index;
      map->tail = index;
    }
  else if (point == map->head)
    {
      index->next = map->head;
      map->head->prev = index;
      map->head = index;
    }
  else
    {
      index->next = point;
      index->prev = point->prev;
      if (point->prev)
	point->prev->next = index;
      point->prev = index;
    }

  /* Execute event hook. */
  if (route_map_master.event_hook)
    (*route_map_master.event_hook) (RMAP_EVENT_INDEX_ADDED,
				    map->name);

  return index;
}

/* Get route map index. */
static struct route_map_index *
route_map_index_get (struct route_map *map, enum route_map_type type, 
		     int pref)
{
  struct route_map_index *index;

  index = route_map_index_lookup (map, RMAP_ANY, pref);
  if (index && index->type != type)
    {
      /* Delete index from route map. */
      route_map_index_delete (index, 1);
      index = NULL;
    }
  if (index == NULL)
    index = route_map_index_add (map, type, pref);
  return index;
}

/* New route map rule */
static struct route_map_rule *
route_map_rule_new (void)
{
  struct route_map_rule *new;

  new = XCALLOC (MTYPE_ROUTE_MAP_RULE, sizeof (struct route_map_rule));
  return new;
}

/* Install rule command to the match list. */
void
route_map_install_match (struct route_map_rule_cmd *cmd)
{
  vector_set (route_match_vec, cmd);
}

/* Install rule command to the set list. */
void
route_map_install_set (struct route_map_rule_cmd *cmd)
{
  vector_set (route_set_vec, cmd);
}

/* Lookup rule command from match list. */
static struct route_map_rule_cmd *
route_map_lookup_match (const char *name)
{
  unsigned int i;
  struct route_map_rule_cmd *rule;

  for (i = 0; i < vector_active (route_match_vec); i++)
    if ((rule = vector_slot (route_match_vec, i)) != NULL)
      if (strcmp (rule->str, name) == 0)
	return rule;
  return NULL;
}

/* Lookup rule command from set list. */
static struct route_map_rule_cmd *
route_map_lookup_set (const char *name)
{
  unsigned int i;
  struct route_map_rule_cmd *rule;

  for (i = 0; i < vector_active (route_set_vec); i++)
    if ((rule = vector_slot (route_set_vec, i)) != NULL)
      if (strcmp (rule->str, name) == 0)
	return rule;
  return NULL;
}

/* Add match and set rule to rule list. */
static void
route_map_rule_add (struct route_map_rule_list *list,
		    struct route_map_rule *rule)
{
  rule->next = NULL;
  rule->prev = list->tail;
  if (list->tail)
    list->tail->next = rule;
  else
    list->head = rule;
  list->tail = rule;
}

/* Delete rule from rule list. */
static void
route_map_rule_delete (struct route_map_rule_list *list,
		       struct route_map_rule *rule)
{
  if (rule->cmd->func_free)
    (*rule->cmd->func_free) (rule->value);

  if (rule->rule_str)
    XFREE (MTYPE_ROUTE_MAP_RULE_STR, rule->rule_str);

  if (rule->next)
    rule->next->prev = rule->prev;
  else
    list->tail = rule->prev;
  if (rule->prev)
    rule->prev->next = rule->next;
  else
    list->head = rule->next;

  XFREE (MTYPE_ROUTE_MAP_RULE, rule);
}

/* strcmp wrapper function which don't crush even argument is NULL. */
static int
rulecmp (const char *dst, const char *src)
{
  if (dst == NULL)
    {
      if (src ==  NULL)
	return 0;
      else
	return 1;
    }
  else
    {
      if (src == NULL)
	return 1;
      else
	return strcmp (dst, src);
    }
  return 1;
}

/* Add match statement to route map. */
int
route_map_add_match (struct route_map_index *index, const char *match_name,
                     const char *match_arg)
{
  struct route_map_rule *rule;
  struct route_map_rule *next;
  struct route_map_rule_cmd *cmd;
  void *compile;
  int replaced = 0;

  /* First lookup rule for add match statement. */
  cmd = route_map_lookup_match (match_name);
  if (cmd == NULL)
    return RMAP_RULE_MISSING;

  /* Next call compile function for this match statement. */
  if (cmd->func_compile)
    {
      compile= (*cmd->func_compile)(match_arg);
      if (compile == NULL)
	return RMAP_COMPILE_ERROR;
    }
  else
    compile = NULL;

  /* If argument is completely same ignore it. */
  for (rule = index->match_list.head; rule; rule = next)
    {
      next = rule->next;
      if (rule->cmd == cmd)
	{	
	  route_map_rule_delete (&index->match_list, rule);
	  replaced = 1;
	}
    }

  /* Add new route map match rule. */
  rule = route_map_rule_new ();
  rule->cmd = cmd;
  rule->value = compile;
  if (match_arg)
    rule->rule_str = XSTRDUP (MTYPE_ROUTE_MAP_RULE_STR, match_arg);
  else
    rule->rule_str = NULL;

  /* Add new route match rule to linked list. */
  route_map_rule_add (&index->match_list, rule);

  /* Execute event hook. */
  if (route_map_master.event_hook)
    (*route_map_master.event_hook) (replaced ?
				    RMAP_EVENT_MATCH_REPLACED:
				    RMAP_EVENT_MATCH_ADDED,
				    index->map->name);

  return 0;
}

/* Delete specified route match rule. */
int
route_map_delete_match (struct route_map_index *index, const char *match_name,
                        const char *match_arg)
{
  struct route_map_rule *rule;
  struct route_map_rule_cmd *cmd;

  cmd = route_map_lookup_match (match_name);
  if (cmd == NULL)
    return 1;
  
  for (rule = index->match_list.head; rule; rule = rule->next)
    if (rule->cmd == cmd && 
	(rulecmp (rule->rule_str, match_arg) == 0 || match_arg == NULL))
      {
	route_map_rule_delete (&index->match_list, rule);
	/* Execute event hook. */
	if (route_map_master.event_hook)
	  (*route_map_master.event_hook) (RMAP_EVENT_MATCH_DELETED,
					  index->map->name);
	return 0;
      }
  /* Can't find matched rule. */
  return 1;
}

/* Add route-map set statement to the route map. */
int
route_map_add_set (struct route_map_index *index, const char *set_name,
                   const char *set_arg)
{
  struct route_map_rule *rule;
  struct route_map_rule *next;
  struct route_map_rule_cmd *cmd;
  void *compile;
  int replaced = 0;

  cmd = route_map_lookup_set (set_name);
  if (cmd == NULL)
    return RMAP_RULE_MISSING;

  /* Next call compile function for this match statement. */
  if (cmd->func_compile)
    {
      compile= (*cmd->func_compile)(set_arg);
      if (compile == NULL)
	return RMAP_COMPILE_ERROR;
    }
  else
    compile = NULL;

 /* Add by WJL. if old set command of same kind exist, delete it first
    to ensure only one set command of same kind exist under a
    route_map_index. */
  for (rule = index->set_list.head; rule; rule = next)
    {
      next = rule->next;
      if (rule->cmd == cmd)
	{
	  route_map_rule_delete (&index->set_list, rule);
	  replaced = 1;
	}
    }

  /* Add new route map match rule. */
  rule = route_map_rule_new ();
  rule->cmd = cmd;
  rule->value = compile;
  if (set_arg)
    rule->rule_str = XSTRDUP (MTYPE_ROUTE_MAP_RULE_STR, set_arg);
  else
    rule->rule_str = NULL;

  /* Add new route match rule to linked list. */
  route_map_rule_add (&index->set_list, rule);

  /* Execute event hook. */
  if (route_map_master.event_hook)
    (*route_map_master.event_hook) (replaced ?
				    RMAP_EVENT_SET_REPLACED:
				    RMAP_EVENT_SET_ADDED,
				    index->map->name);
  return 0;
}

/* Delete route map set rule. */
int
route_map_delete_set (struct route_map_index *index, const char *set_name,
                      const char *set_arg)
{
  struct route_map_rule *rule;
  struct route_map_rule_cmd *cmd;

  cmd = route_map_lookup_set (set_name);
  if (cmd == NULL)
    return 1;
  
  for (rule = index->set_list.head; rule; rule = rule->next)
    if ((rule->cmd == cmd) &&
         (rulecmp (rule->rule_str, set_arg) == 0 || set_arg == NULL))
      {
        route_map_rule_delete (&index->set_list, rule);
	/* Execute event hook. */
	if (route_map_master.event_hook)
	  (*route_map_master.event_hook) (RMAP_EVENT_SET_DELETED,
					  index->map->name);
        return 0;
      }
  /* Can't find matched rule. */
  return 1;
}

/* Apply route map's each index to the object.

   The matrix for a route-map looks like this:
   (note, this includes the description for the "NEXT"
   and "GOTO" frobs now
  
              Match   |   No Match
                      |
    permit    action  |     cont
                      |
    ------------------+---------------
                      |
    deny      deny    |     cont
                      |
  
   action)
      -Apply Set statements, accept route
      -If Call statement is present jump to the specified route-map, if it
         denies the route we finish.
      -If NEXT is specified, goto NEXT statement
      -If GOTO is specified, goto the first clause where pref > nextpref
      -If nothing is specified, do as Cisco and finish
   deny)
      -Route is denied by route-map.
   cont)
      -Goto Next index
  
   If we get no matches after we've processed all updates, then the route
   is dropped too.
  
   Some notes on the new "CALL", "NEXT" and "GOTO"
     call WORD        - If this clause is matched, then the set statements
                        are executed and then we jump to route-map 'WORD'. If
                        this route-map denies the route, we finish, in other case we
                        do whatever the exit policy (EXIT, NEXT or GOTO) tells.
     on-match next    - If this clause is matched, then the set statements
                        are executed and then we drop through to the next clause
     on-match goto n  - If this clause is matched, then the set statments
                        are executed and then we goto the nth clause, or the
                        first clause greater than this. In order to ensure
                        route-maps *always* exit, you cannot jump backwards.
                        Sorry ;)
  
   We need to make sure our route-map processing matches the above
*/

static route_map_result_t
route_map_apply_match (struct route_map_rule_list *match_list,
                       struct prefix *prefix, route_map_object_t type,
                       void *object)
{
  route_map_result_t ret = RMAP_NOMATCH;
  struct route_map_rule *match;


  /* Check all match rule and if there is no match rule, go to the
     set statement. */
  if (!match_list->head)
    ret = RMAP_MATCH;
  else
    {
      for (match = match_list->head; match; match = match->next)
        {
          /* Try each match statement in turn, If any do not return
             RMAP_MATCH, return, otherwise continue on to next match 
             statement. All match statements must match for end-result
             to be a match. */
          ret = (*match->cmd->func_apply) (match->value, prefix,
                                           type, object);
          if (ret != RMAP_MATCH)
            return ret;
        }
    }
  return ret;
}

/* Apply route map to the object. */
route_map_result_t
route_map_apply (struct route_map *map, struct prefix *prefix,
                 route_map_object_t type, void *object)
{
  static int recursion = 0;
  int ret = 0;
  struct route_map_index *index;
  struct route_map_rule *set;

  if (recursion > RMAP_RECURSION_LIMIT)
    {
      zlog (NULL, LOG_WARNING,
            "route-map recursion limit (%d) reached, discarding route",
            RMAP_RECURSION_LIMIT);
      recursion = 0;
      return RMAP_DENYMATCH;
    }

  if (map == NULL)
    return RMAP_DENYMATCH;

  for (index = map->head; index; index = index->next)
    {
      /* Apply this index. */
      ret = route_map_apply_match (&index->match_list, prefix, type, object);

      /* Now we apply the matrix from above */
      if (ret == RMAP_NOMATCH)
        /* 'cont' from matrix - continue to next route-map sequence */
        continue;
      else if (ret == RMAP_MATCH)
        {
          if (index->type == RMAP_PERMIT)
            /* 'action' */
            {
              /* permit+match must execute sets */
              for (set = index->set_list.head; set; set = set->next)
                ret = (*set->cmd->func_apply) (set->value, prefix,
                                               type, object);

              /* Call another route-map if available */
              if (index->nextrm)
                {
                  struct route_map *nextrm =
                                    route_map_lookup_by_name (index->nextrm);

                  if (nextrm) /* Target route-map found, jump to it */
                    {
                      recursion++;
                      ret = route_map_apply (nextrm, prefix, type, object);
                      recursion--;
                    }

                  /* If nextrm returned 'deny', finish. */
                  if (ret == RMAP_DENYMATCH)
                    return ret;
                }
                
              switch (index->exitpolicy)
                {
                  case RMAP_EXIT:
                    return ret;
                  case RMAP_NEXT:
                    continue;
                  case RMAP_GOTO:
                    {
                      /* Find the next clause to jump to */
                      struct route_map_index *next = index->next;
                      int nextpref = index->nextpref;

                      while (next && next->pref < nextpref)
                        {
                          index = next;
                          next = next->next;
                        }
                      if (next == NULL)
                        {
                          /* No clauses match! */
                          return ret;
                        }
                    }
                }
            }
          else if (index->type == RMAP_DENY)
            /* 'deny' */
            {
                return RMAP_DENYMATCH;
            }
        }
    }
  /* Finally route-map does not match at all. */
  return RMAP_DENYMATCH;
}

void
route_map_add_hook (void (*func) (const char *))
{
  route_map_master.add_hook = func;
}

void
route_map_delete_hook (void (*func) (const char *))
{
  route_map_master.delete_hook = func;
}

void
route_map_event_hook (void (*func) (route_map_event_t, const char *))
{
  route_map_master.event_hook = func;
}

void
route_map_init (void)
{
  /* Make vector for match and set. */
  route_match_vec = vector_init (1);
  route_set_vec = vector_init (1);
}

void
route_map_finish (void)
{
  vector_free (route_match_vec);
  route_match_vec = NULL;
  vector_free (route_set_vec);
  route_set_vec = NULL;
  /* cleanup route_map */                                                    
  while (route_map_master.head)                                              
    route_map_delete (route_map_master.head); 
}

/* VTY related functions. */
DEFUN (route_map,
       route_map_cmd,
       "route-map WORD (deny|permit) <1-65535>",
       "Create route-map or enter route-map command mode\n"
       "Route map tag\n"
       "Route map denies set operations\n"
       "Route map permits set operations\n"
       "Sequence to insert to/delete from existing route-map entry\n")
{
  int permit;
  unsigned long pref;
  struct route_map *map;
  struct route_map_index *index;
  char *endptr = NULL;

  /* Permit check. */
  if (strncmp (argv[1], "permit", strlen (argv[1])) == 0)
    permit = RMAP_PERMIT;
  else if (strncmp (argv[1], "deny", strlen (argv[1])) == 0)
    permit = RMAP_DENY;
  else
    {
      vty_out (vty, "the third field must be [permit|deny]%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Preference check. */
  pref = strtoul (argv[2], &endptr, 10);
  if (pref == ULONG_MAX || *endptr != '\0')
    {
      vty_out (vty, "the fourth field must be positive integer%s",
	       VTY_NEWLINE);
      return CMD_WARNING;
    }
  if (pref == 0 || pref > 65535)
    {
      vty_out (vty, "the fourth field must be <1-65535>%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Get route map. */
  map = route_map_get (argv[0]);
  index = route_map_index_get (map, permit, pref);

  vty->index = index;
  vty->node = RMAP_NODE;
  return CMD_SUCCESS;
}

DEFUN (no_route_map_all,
       no_route_map_all_cmd,
       "no route-map WORD",
       NO_STR
       "Create route-map or enter route-map command mode\n"
       "Route map tag\n")
{
  struct route_map *map;

  map = route_map_lookup_by_name (argv[0]);
  if (map == NULL)
    {
      vty_out (vty, "%% Could not find route-map %s%s",
	       argv[0], VTY_NEWLINE);
      return CMD_WARNING;
    }

  route_map_delete (map);

  return CMD_SUCCESS;
}

DEFUN (no_route_map,
       no_route_map_cmd,
       "no route-map WORD (deny|permit) <1-65535>",
       NO_STR
       "Create route-map or enter route-map command mode\n"
       "Route map tag\n"
       "Route map denies set operations\n"
       "Route map permits set operations\n"
       "Sequence to insert to/delete from existing route-map entry\n")
{
  int permit;
  unsigned long pref;
  struct route_map *map;
  struct route_map_index *index;
  char *endptr = NULL;

  /* Permit check. */
  if (strncmp (argv[1], "permit", strlen (argv[1])) == 0)
    permit = RMAP_PERMIT;
  else if (strncmp (argv[1], "deny", strlen (argv[1])) == 0)
    permit = RMAP_DENY;
  else
    {
      vty_out (vty, "the third field must be [permit|deny]%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Preference. */
  pref = strtoul (argv[2], &endptr, 10);
  if (pref == ULONG_MAX || *endptr != '\0')
    {
      vty_out (vty, "the fourth field must be positive integer%s",
	       VTY_NEWLINE);
      return CMD_WARNING;
    }
  if (pref == 0 || pref > 65535)
    {
      vty_out (vty, "the fourth field must be <1-65535>%s", VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Existence check. */
  map = route_map_lookup_by_name (argv[0]);
  if (map == NULL)
    {
      vty_out (vty, "%% Could not find route-map %s%s",
	       argv[0], VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Lookup route map index. */
  index = route_map_index_lookup (map, permit, pref);
  if (index == NULL)
    {
      vty_out (vty, "%% Could not find route-map entry %s %s%s", 
	       argv[0], argv[2], VTY_NEWLINE);
      return CMD_WARNING;
    }

  /* Delete index from route map. */
  route_map_index_delete (index, 1);

  /* If this route rule is the last one, delete route map itself. */
  if (route_map_empty (map))
    route_map_delete (map);

  return CMD_SUCCESS;
}

DEFUN (rmap_onmatch_next,
       rmap_onmatch_next_cmd,
       "on-match next",
       "Exit policy on matches\n"
       "Next clause\n")
{
  struct route_map_index *index;

  index = vty->index;

  if (index)
    index->exitpolicy = RMAP_NEXT;

  return CMD_SUCCESS;
}

DEFUN (no_rmap_onmatch_next,
       no_rmap_onmatch_next_cmd,
       "no on-match next",
       NO_STR
       "Exit policy on matches\n"
       "Next clause\n")
{
  struct route_map_index *index;

  index = vty->index;
  
  if (index)
    index->exitpolicy = RMAP_EXIT;

  return CMD_SUCCESS;
}

DEFUN (rmap_onmatch_goto,
       rmap_onmatch_goto_cmd,
       "on-match goto <1-65535>",
       "Exit policy on matches\n"
       "Goto Clause number\n"
       "Number\n")
{
  struct route_map_index *index = vty->index;
  int d = 0;

  if (index)
    {
      if (argc == 1 && argv[0])
        VTY_GET_INTEGER_RANGE("route-map index", d, argv[0], 1, 65536);
      else
        d = index->pref + 1;
      
      if (d <= index->pref)
	{
	  /* Can't allow you to do that, Dave */
	  vty_out (vty, "can't jump backwards in route-maps%s", 
		   VTY_NEWLINE);
	  return CMD_WARNING;
	}
      else
	{
	  index->exitpolicy = RMAP_GOTO;
	  index->nextpref = d;
	}
    }
  return CMD_SUCCESS;
}

DEFUN (no_rmap_onmatch_goto,
       no_rmap_onmatch_goto_cmd,
       "no on-match goto",
       NO_STR
       "Exit policy on matches\n"
       "Goto Clause number\n")
{
  struct route_map_index *index;

  index = vty->index;

  if (index)
    index->exitpolicy = RMAP_EXIT;
  
  return CMD_SUCCESS;
}

/* Cisco/GNU Zebra compatible ALIASes for on-match next */
ALIAS (rmap_onmatch_goto,
       rmap_continue_cmd,
       "continue",
       "Continue on a different entry within the route-map\n")

ALIAS (no_rmap_onmatch_goto,
       no_rmap_continue_cmd,
       "no continue",
       NO_STR
       "Continue on a different entry within the route-map\n")

/* GNU Zebra compatible */
ALIAS (rmap_onmatch_goto,
       rmap_continue_seq_cmd,
       "continue <1-65535>",
       "Continue on a different entry within the route-map\n"
       "Route-map entry sequence number\n")

ALIAS (no_rmap_onmatch_goto,
       no_rmap_continue_seq,
       "no continue <1-65535>",
       NO_STR
       "Continue on a different entry within the route-map\n"
       "Route-map entry sequence number\n")

DEFUN (rmap_show_name,
       rmap_show_name_cmd,
       "show route-map [WORD]",
       SHOW_STR
       "route-map information\n"
       "route-map name\n")
{
    const char *name = NULL;
    if (argc)
      name = argv[0];
    return vty_show_route_map (vty, name);
}

ALIAS (rmap_onmatch_goto,
      rmap_continue_index_cmd,
      "continue <1-65536>",
      "Exit policy on matches\n"
      "Goto Clause number\n")

DEFUN (rmap_call,
       rmap_call_cmd,
       "call WORD",
       "Jump to another Route-Map after match+set\n"
       "Target route-map name\n")
{
  struct route_map_index *index;

  index = vty->index;
  if (index)
    {
      if (index->nextrm)
          XFREE (MTYPE_ROUTE_MAP_NAME, index->nextrm);
      index->nextrm = XSTRDUP (MTYPE_ROUTE_MAP_NAME, argv[0]);
    }
  return CMD_SUCCESS;
}

DEFUN (no_rmap_call,
       no_rmap_call_cmd,
       "no call",
       NO_STR
       "Jump to another Route-Map after match+set\n")
{
  struct route_map_index *index;

  index = vty->index;

  if (index->nextrm)
    {
      XFREE (MTYPE_ROUTE_MAP_NAME, index->nextrm);
      index->nextrm = NULL;
    }

  return CMD_SUCCESS;
}

DEFUN (rmap_description,
       rmap_description_cmd,
       "description .LINE",
       "Route-map comment\n"
       "Comment describing this route-map rule\n")
{
  struct route_map_index *index;

  index = vty->index;
  if (index)
    {
      if (index->description)
	XFREE (MTYPE_TMP, index->description);
      index->description = argv_concat (argv, argc, 0);
    }
  return CMD_SUCCESS;
}

DEFUN (no_rmap_description,
       no_rmap_description_cmd,
       "no description",
       NO_STR
       "Route-map comment\n")
{
  struct route_map_index *index;

  index = vty->index;
  if (index)
    {
      if (index->description)
	XFREE (MTYPE_TMP, index->description);
      index->description = NULL;
    }
  return CMD_SUCCESS;
}

/* Configuration write function. */
static int
route_map_config_write (struct vty *vty)
{
  struct route_map *map;
  struct route_map_index *index;
  struct route_map_rule *rule;
  int first = 1;
  int write = 0;

  for (map = route_map_master.head; map; map = map->next)
    for (index = map->head; index; index = index->next)
      {
	if (!first)
	  vty_out (vty, "!%s", VTY_NEWLINE);
	else
	  first = 0;

	vty_out (vty, "route-map %s %s %d%s", 
		 map->name,
		 route_map_type_str (index->type),
		 index->pref, VTY_NEWLINE);

	if (index->description)
	  vty_out (vty, " description %s%s", index->description, VTY_NEWLINE);

	for (rule = index->match_list.head; rule; rule = rule->next)
	  vty_out (vty, " match %s %s%s", rule->cmd->str, 
		   rule->rule_str ? rule->rule_str : "",
		   VTY_NEWLINE);

	for (rule = index->set_list.head; rule; rule = rule->next)
	  vty_out (vty, " set %s %s%s", rule->cmd->str,
		   rule->rule_str ? rule->rule_str : "",
		   VTY_NEWLINE);
   if (index->nextrm)
     vty_out (vty, " call %s%s", index->nextrm, VTY_NEWLINE);
	if (index->exitpolicy == RMAP_GOTO)
      vty_out (vty, " on-match goto %d%s", index->nextpref, VTY_NEWLINE);
	if (index->exitpolicy == RMAP_NEXT)
	  vty_out (vty," on-match next%s", VTY_NEWLINE);
	
	write++;
      }
  return write;
}

/* Route map node structure. */
static struct cmd_node rmap_node =
{
  RMAP_NODE,
  "%s(config-route-map)# ",
  1
};

/* Common route map rules */

void *
route_map_rule_tag_compile (const char *arg)
{
  unsigned long int tmp;
  char *endptr;
  route_tag_t *tag;

  errno = 0;
  tmp = strtoul(arg, &endptr, 0);
  if (arg[0] == '\0' || *endptr != '\0' || errno || tmp > ROUTE_TAG_MAX)
    return NULL;

  tag = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(*tag));
  *tag = tmp;

  return tag;
}

void
route_map_rule_tag_free (void *rule)
{
  XFREE (MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* Initialization of route map vector. */
void
route_map_init_vty (void)
{
  /* Install route map top node. */
  install_node (&rmap_node, route_map_config_write);

  /* Install route map commands. */
  install_default (RMAP_NODE);
  install_element (CONFIG_NODE, &route_map_cmd);
  install_element (CONFIG_NODE, &no_route_map_cmd);
  install_element (CONFIG_NODE, &no_route_map_all_cmd);

  /* Install the on-match stuff */
  install_element (RMAP_NODE, &route_map_cmd);
  install_element (RMAP_NODE, &rmap_onmatch_next_cmd);
  install_element (RMAP_NODE, &no_rmap_onmatch_next_cmd);
  install_element (RMAP_NODE, &rmap_onmatch_goto_cmd);
  install_element (RMAP_NODE, &no_rmap_onmatch_goto_cmd);
  
  /* Install the continue stuff (ALIAS of on-match). */
  install_element (RMAP_NODE, &rmap_continue_cmd);
  install_element (RMAP_NODE, &no_rmap_continue_cmd);
  install_element (RMAP_NODE, &rmap_continue_index_cmd);
  
  /* Install the call stuff. */
  install_element (RMAP_NODE, &rmap_call_cmd);
  install_element (RMAP_NODE, &no_rmap_call_cmd);

  /* Install description commands. */
  install_element (RMAP_NODE, &rmap_description_cmd);
  install_element (RMAP_NODE, &no_rmap_description_cmd);
   
  /* Install show command */
  install_element (ENABLE_NODE, &rmap_show_name_cmd);
}
