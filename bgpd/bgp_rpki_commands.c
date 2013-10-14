/*
 * rpki_commands.c
 *
 * Author: Michael Mester (m.mester@fu-berlin.de)
 */

#include <zebra.h>
#include <pthread.h>
#include "log.h"
#include "command.h"
#include "linklist.h"
#include "memory.h"
#include "prefix.h"
#include "routemap.h"
#include "vector.h"
#include "vty.h"

#include "rtrlib/rtrlib.h"
#include "rtrlib/lib/ip.h"
#include "rtrlib/transport/tcp/tcp_transport.h"
#include "rtrlib/transport/ssh/ssh_transport.h"

#include "bgpd/bgpd.h"
#include "bgpd/bgp_table.h"
#include "bgpd/bgp_attr.h"
#include "bgpd/bgp_aspath.h"
#include "bgpd/bgp_route.h"
#include "bgpd/bgp_rpki.h"
#include "bgpd/bgp_rpki_commands.h"

#define RPKI_OUTPUT_STRING "Control rpki specific settings\n"
/**************************************/
/** Declaration of structs & enums   **/
/**************************************/
enum return_values
{
  SUCCESS = 0, ERROR = -1
};

typedef struct
{
  struct list* cache_config_list;
  int preference_value;
  char delete_flag;
} cache_group;

typedef struct
{
  enum
  {
    TCP, SSH
  } type;
  union
  {
    tr_tcp_config* tcp_config;
    tr_ssh_config* ssh_config;
  } tr_config;
  rtr_socket* rtr_socket;
  char delete_flag;
} cache;

/**********************************/
/** Declaration of variables     **/
/**********************************/
cache_group* currently_selected_cache_group;

/* RPKI command node structure */
struct cmd_node rpki_node = { RPKI_NODE, "%s(config-rpki)# ", 1, NULL, NULL };

/**************************************/
/** Declaration of static functions  **/
/**************************************/
static void delete_cache(void* value);
static struct list* create_cache_list(void);
static cache* create_cache(tr_socket* tr_socket);
static cache* find_cache(struct list* cache_list, const char* host,
    const char* port_string, const unsigned int port);
static int add_tcp_cache(struct list* cache_list, const char* host,
    const char* port);
static int add_ssh_cache(struct list* cache_list, const char* host,
    const unsigned int port, const char* username, const char* client_privkey_path,
    const char* client_pubkey_path, const char* server_pubkey_path);
static void delete_cache_group(void* _cache_group);
static void delete_marked_cache_groups(void);
static cache_group* find_cache_group(int preference_value);
static cache_group* create_cache_group(int preference_value);
static int rpki_config_write (struct vty * vty);
static void route_match_rpki_free(void *rule);
static void* route_match_rpki_compile(const char *arg);
static route_map_result_t route_match_rpki(void *rule, struct prefix *prefix,
    route_map_object_t type, void *object);

static void
delete_cache(void* value)
{
  cache* cache_p = (cache*) value;
  if (cache_p->type == TCP)
    {
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config->host);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config->port);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.tcp_config);
    }
  else
    {
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->host);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config->username);
      XFREE(MTYPE_BGP_RPKI_CACHE,
          cache_p->tr_config.ssh_config->client_privkey_path);
      XFREE(MTYPE_BGP_RPKI_CACHE,
          cache_p->tr_config.ssh_config->client_pubkey_path);
      XFREE(MTYPE_BGP_RPKI_CACHE,
          cache_p->tr_config.ssh_config->server_hostkey_path);
      XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->tr_config.ssh_config);
    }
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->rtr_socket->tr_socket);
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p->rtr_socket);
  XFREE(MTYPE_BGP_RPKI_CACHE, cache_p);
}

static struct list*
create_cache_list()
{
  struct list* cache_list = list_new();
  cache_list->del = delete_cache;
  cache_list->count = 0;
  return cache_list;
}

static cache*
create_cache(tr_socket* tr_socket)
{
  rtr_socket* rtr_socket_p;
  cache* cache_p;
  if ((rtr_socket_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(rtr_socket)))
      == NULL )
    {
      return NULL ;
    }
  if ((cache_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(cache))) == NULL )
    {
      return NULL ;
    }
  rtr_socket_p->tr_socket = tr_socket;
  cache_p->rtr_socket = rtr_socket_p;
  cache_p->delete_flag = 0;
  return cache_p;
}

static cache*
find_cache(struct list* cache_list, const char* host, const char* port_string,
    const unsigned int port)
{
  struct listnode* cache_node;
  cache* cache;
  for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache))
    {
      if (cache->delete_flag)
        {
          continue;
        }
      if (cache->type == TCP)
        {
          if (strcmp(cache->tr_config.tcp_config->host, host) == 0)
            {
              if (port_string != NULL
                  && strcmp(cache->tr_config.tcp_config->port, port_string)
                      != 0)
                {
                  break;
                }
              return cache;
            }
        }
      else
        {
          if (strcmp(cache->tr_config.ssh_config->host, host) == 0)
            {
              if (port != 0)
                {
                  if (cache->tr_config.ssh_config->port != port)
                    {
                      break;
                    }
                }
              return cache;
            }
        }
    }
  return NULL ;
}

static int
add_tcp_cache(struct list* cache_list, const char* host, const char* port)
{
  tr_tcp_config* tcp_config_p;
  tr_socket* tr_socket_p;
  cache* cache_p;
  if ((tr_socket_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(tr_socket))) == NULL )
    {
      return ERROR;
    }
  if ((tcp_config_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(tr_tcp_config)))
      == NULL )
    {
      return ERROR;
    }
  tcp_config_p->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);
  tcp_config_p->port = XSTRDUP(MTYPE_BGP_RPKI_CACHE, port);
  tr_tcp_init(tcp_config_p, tr_socket_p);
  if ((cache_p = create_cache(tr_socket_p)) == NULL )
    {
      return ERROR;
    }
  cache_p->tr_config.tcp_config = tcp_config_p;
  cache_p->type = TCP;
  listnode_add(cache_list, cache_p);
  return SUCCESS;
}

static int
add_ssh_cache(struct list* cache_list, const char* host,
    const unsigned int port, const char* username,
    const char* client_privkey_path, const char* client_pubkey_path,
    const char* server_pubkey_path)
{

  tr_ssh_config* ssh_config_p;
  tr_socket* tr_socket_p;
  cache* cache_p;
  if ((tr_socket_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(tr_socket))) == NULL )
    {
      return ERROR;
    }
  if ((ssh_config_p = XMALLOC(MTYPE_BGP_RPKI_CACHE, sizeof(tr_ssh_config)))
      == NULL )
    {
      return ERROR;
    }

  memcpy(&(ssh_config_p->port), &port, sizeof(int));
  ssh_config_p->host = XSTRDUP(MTYPE_BGP_RPKI_CACHE, host);

  ssh_config_p->username = XSTRDUP(MTYPE_BGP_RPKI_CACHE, username);
  ssh_config_p->client_privkey_path = XSTRDUP(MTYPE_BGP_RPKI_CACHE,
      client_privkey_path);
  ssh_config_p->client_pubkey_path = XSTRDUP(MTYPE_BGP_RPKI_CACHE,
      client_pubkey_path);

  if (server_pubkey_path != NULL )
    {
      ssh_config_p->server_hostkey_path = XSTRDUP(MTYPE_BGP_RPKI_CACHE,
          server_pubkey_path);
      ;
    }
  else
    {
      ssh_config_p->server_hostkey_path = NULL;
    }

  tr_ssh_init(ssh_config_p, tr_socket_p);
  if ((cache_p = create_cache(tr_socket_p)) == NULL )
    {
      return ERROR;
    }
  cache_p->tr_config.ssh_config = ssh_config_p;
  cache_p->type = SSH;
  listnode_add(cache_list, cache_p);
  return SUCCESS;
}

static void
delete_cache_group(void* _cache_group)
{
  cache_group* group = _cache_group;
  list_delete(group->cache_config_list);
  XFREE(MTYPE_BGP_RPKI_CACHE_GROUP, group);
}

static void
delete_marked_cache_groups()
{
  cache_group* cache_group;
  cache* cache;
  struct listnode *cache_group_node, *cache_node;
  struct listnode *next_node, *next_cache_node;
  for (ALL_LIST_ELEMENTS(cache_group_list, cache_group_node, next_node,
      cache_group))
    {
      for (ALL_LIST_ELEMENTS(cache_group->cache_config_list, cache_node,
          next_cache_node, cache))
        {
          if (cache->delete_flag)
            {
              listnode_delete(cache_group->cache_config_list, cache);
              delete_cache(cache);
            }
        }
      if (listcount(cache_group->cache_config_list) == 0
          || cache_group->delete_flag)
        {
          listnode_delete(cache_group_list, cache_group);
          delete_cache_group(cache_group);
        }
    }
}

static cache_group*
find_cache_group(int preference_value)
{
  cache_group* cache_group;
  struct listnode *cache_group_node;
  for (ALL_LIST_ELEMENTS_RO(cache_group_list, cache_group_node, cache_group))
    {
      if (cache_group->preference_value == preference_value
          && !cache_group->delete_flag)
        {
          return cache_group;
        }
    }
  return NULL ;
}

static cache_group*
create_cache_group(int preference_value)
{
  cache_group* group;
  if ((group = XMALLOC(MTYPE_BGP_RPKI_CACHE_GROUP, sizeof(cache_group)))
      == NULL )
    {
      return NULL ;
    }
  group->cache_config_list = create_cache_list();
  group->preference_value = preference_value;
  group->delete_flag = 0;
  return group;
}

static void
reprocess_routes(struct bgp* bgp)
{
  afi_t afi;
  for (afi = AFI_IP; afi < AFI_MAX; ++afi)
    {
      rpki_revalidate_all_routes(bgp, afi);
    }
}

static int
rpki_config_write(struct vty * vty)
{
  struct listnode *cache_group_node;
  cache_group* cache_group;
  if (listcount(cache_group_list))
    {
      if (rpki_debug)
        {
          vty_out(vty, "debug rpki%s", VTY_NEWLINE);
        }
      vty_out(vty, "! %s", VTY_NEWLINE);
      vty_out(vty, "enable-rpki%s", VTY_NEWLINE);
      vty_out(vty, "  rpki polling_period %d %s", polling_period, VTY_NEWLINE);
      vty_out(vty, "  rpki timeout %d %s", timeout, VTY_NEWLINE);
      vty_out(vty, "  rpki initial-synchronisation-timeout %d %s",
          initial_synchronisation_timeout, VTY_NEWLINE);
      vty_out(vty, "! %s", VTY_NEWLINE);
      for (ALL_LIST_ELEMENTS_RO(cache_group_list, cache_group_node, cache_group)
          )
        {
          struct list* cache_list = cache_group->cache_config_list;
          struct listnode* cache_node;
          cache* cache;
          vty_out(vty, "  rpki group %d %s", cache_group->preference_value,
              VTY_NEWLINE);
          if (listcount(cache_list) == 0)
            {
              vty_out(vty, "! %s", VTY_NEWLINE);
              continue;
            }
          for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache))
            {
              switch (cache->type)
                {
                tr_tcp_config* tcp_config;
                tr_ssh_config* ssh_config;
              case TCP:
                tcp_config = cache->tr_config.tcp_config;
                vty_out(vty, "    rpki cache %s %s %s", tcp_config->host,
                    tcp_config->port, VTY_NEWLINE);
                break;

              case SSH:
                ssh_config = cache->tr_config.ssh_config;
                vty_out(vty, "    rpki cache %s %u %s %s %s %s %s",
                    ssh_config->host, ssh_config->port, ssh_config->username,
                    ssh_config->client_privkey_path,
                    ssh_config->client_pubkey_path,
                    ssh_config->server_hostkey_path != NULL ?
                        ssh_config->server_hostkey_path : " ", VTY_NEWLINE);
                break;

              default:
                break;
              }
          }
        vty_out(vty, "! %s", VTY_NEWLINE);
      }
    return 1;
  }
else
  {
    return 0;
  }
}

/**********************************/
/** Declaration of cli commands  **/
/**********************************/

DEFUN (enable_rpki,
    enable_rpki_cmd,
    "enable-rpki",
    BGP_STR
    "Enable rpki and enter rpki configuration mode\n")
{
  vty->node = RPKI_NODE;
  return CMD_SUCCESS;
}

DEFUN (rpki_polling_period,
    rpki_polling_period_cmd,
    "rpki polling_period " CMD_POLLING_PERIOD_RANGE,
    RPKI_OUTPUT_STRING
    "Set polling period\n"
    "Polling period value\n")
{
  if (argc != 1)
    {
      return CMD_ERR_INCOMPLETE;
    }
  VTY_GET_INTEGER_RANGE("polling_period", polling_period, argv[0], 1, 3600);
  return CMD_SUCCESS;
}

DEFUN (no_rpki_polling_period,
    no_rpki_polling_period_cmd,
    "no rpki polling_period ",
    NO_STR
    RPKI_OUTPUT_STRING
    "Set polling period back to default\n")
{
  polling_period = POLLING_PERIOD_DEFAULT;
  return CMD_SUCCESS;
}

DEFUN (rpki_timeout,
    rpki_timeout_cmd,
    "rpki timeout TIMEOUT",
    RPKI_OUTPUT_STRING
    "Set timeout\n"
    "Timeout value\n")
{
  if (argc != 1)
    {
      return CMD_ERR_INCOMPLETE;
    }
  VTY_GET_INTEGER("timeout", timeout, argv[0]);
  return CMD_SUCCESS;
}

DEFUN (no_rpki_timeout,
    no_rpki_timeout_cmd,
    "no rpki timeout",
    NO_STR
    RPKI_OUTPUT_STRING
    "Set timeout back to default\n")
{
  timeout = TIMEOUT_DEFAULT;
  return CMD_SUCCESS;
}

DEFUN (rpki_synchronisation_timeout,
    rpki_synchronisation_timeout_cmd,
    "rpki initial-synchronisation-timeout TIMEOUT",
    RPKI_OUTPUT_STRING
    "Set a timeout for the initial synchronisation of prefix validation data\n"
    "Timeout value\n")
{
  if (argc != 1)
    {
      return CMD_ERR_INCOMPLETE;
    }
  VTY_GET_INTEGER("timeout", initial_synchronisation_timeout, argv[0]);
  return CMD_SUCCESS;
}

DEFUN (no_rpki_synchronisation_timeout,
    no_rpki_synchronisation_timeout_cmd,
    "no rpki initial-synchronisation-timeout",
    NO_STR
    RPKI_OUTPUT_STRING
    "Set the inital synchronisation timeout back to default (30 sec.)\n")
{
  initial_synchronisation_timeout = INITIAL_SYNCHRONISATION_TIMEOUT_DEFAULT;
  return CMD_SUCCESS;
}

DEFUN (rpki_group,
    rpki_group_cmd,
    "rpki group PREFERENCE",
    RPKI_OUTPUT_STRING
    "Select an existing or start a new group of cache servers\n"
    "Preference Value for this group (lower value means higher preference)\n")
{
  int group_preference;
  cache_group* new_cache_group;
  if (argc != 1)
    {
      return CMD_ERR_INCOMPLETE;
    }
  VTY_GET_INTEGER("group preference", group_preference, argv[0]);
  // Select group for further configuration
  currently_selected_cache_group = find_cache_group(group_preference);
  
  // Group does not yet exist so create new one
  if (currently_selected_cache_group == NULL )
    {
      if ((new_cache_group = create_cache_group(group_preference)) == NULL )
        {
          vty_out(vty, "Could not create new rpki cache group because "
              "of memory allocation error%s", VTY_NEWLINE);
          return CMD_WARNING;
        }
      listnode_add(cache_group_list, new_cache_group);
      currently_selected_cache_group = new_cache_group;
    }
  return CMD_SUCCESS;
}

DEFUN (no_rpki_group,
    no_rpki_group_cmd,
    "no rpki group PREFERENCE",
    NO_STR
    RPKI_OUTPUT_STRING
    "Remove a group of cache servers\n"
    "Preference Value for this group (lower value means higher preference)\n")
{
  int group_preference;
  cache_group* cache_group;
  if (argc != 1)
    {
      return CMD_ERR_INCOMPLETE;
    }
  VTY_GET_INTEGER("group preference", group_preference, argv[0]);
  cache_group = find_cache_group(group_preference);
  if (cache_group == NULL )
    {
      vty_out(vty, "There is no cache group with preference value %d%s",
          group_preference, VTY_NEWLINE);
      return CMD_WARNING;
    }
  cache_group->delete_flag = 1;
  currently_selected_cache_group = NULL;
  return CMD_SUCCESS;
}

DEFUN (rpki_cache,
    rpki_cache_cmd,
    "rpki cache (A.B.C.D|WORD) PORT [SSH_UNAME] [SSH_PRIVKEY] [SSH_PUBKEY] [SERVER_PUBKEY]",
    RPKI_OUTPUT_STRING
    "Install a cache server to current group\n"
    "IP address of cache server\n Hostname of cache server\n"
    "Tcp port number \n"
    "SSH user name \n"
    "Path to own SSH private key \n"
    "Path to own SSH public key \n"
    "Path to Public key of cache server \n")
{
  int return_value = SUCCESS;
  if (list_isempty(cache_group_list))
    {
      vty_out(vty, "Cannot create new rpki cache because "
          "no cache group is defined.%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
  if (currently_selected_cache_group == NULL )
    {
      vty_out(vty, "No cache group is selected%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
  // use ssh connection
  if (argc == 5)
    {
      int port;
      VTY_GET_INTEGER("rpki cache ssh port", port, argv[1]);
      return_value = add_ssh_cache(
          currently_selected_cache_group->cache_config_list, argv[0], port,
          argv[2], argv[3], argv[4], NULL );
    }
  else if (argc == 6)
    {
      unsigned int port;
      VTY_GET_INTEGER("rpki cache ssh port", port, argv[1]);
      return_value = add_ssh_cache(
          currently_selected_cache_group->cache_config_list, argv[0], port,
          argv[2], argv[3], argv[4], argv[5]);
    }
  // use tcp connection
  else if (argc == 2)
    {
      return_value = add_tcp_cache(
          currently_selected_cache_group->cache_config_list, argv[0], argv[1]);
    }
  else
    {
      return CMD_ERR_INCOMPLETE;
    }
  if (return_value == ERROR)
    {
      vty_out(vty, "Could not create new rpki cache because "
          "of memory allocation error%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
  return CMD_SUCCESS;
}

DEFUN (no_rpki_cache,
    no_rpki_cache_cmd,
    "no rpki cache (A.B.C.D|WORD) [PORT]",
    NO_STR
    RPKI_OUTPUT_STRING
    "Remove a cache server from current group\n"
    "IP address of cache server\n Hostname of cache server\n"
    "Tcp port number (optional) \n")
{
  cache* cache = NULL;
  if (list_isempty(cache_group_list))
    {
      return CMD_SUCCESS;
    }
  if (currently_selected_cache_group == NULL )
    {
      vty_out(vty, "No cache group is selected%s", VTY_NEWLINE);
      return CMD_WARNING;
    }
  if (argc == 2)
    {
      unsigned int port;
      VTY_GET_INTEGER("rpki cache port", port, argv[1]);
      const unsigned int const_port = port;
      cache = find_cache(currently_selected_cache_group->cache_config_list,
          argv[0], argv[1], const_port);

    }
  else if (argc == 1)
    {
      cache = find_cache(currently_selected_cache_group->cache_config_list,
          argv[0], NULL, 0);

    }
  else
    {
      return CMD_ERR_INCOMPLETE;
    }
  if (cache == NULL )
    {
      vty_out(vty, "Cannot find cache %s%s", argv[0], VTY_NEWLINE);
      return CMD_WARNING;
    }
  cache->delete_flag = 1;
  return CMD_SUCCESS;
}


DEFUN (bgp_bestpath_prefix_validate_disable,
      bgp_bestpath_prefix_validate_disable_cmd,
       "bgp bestpath prefix-validate disable",
       "BGP specific commands\n"
       "Change the default bestpath selection\n"
       "Prefix validation attribute\n"
       "Disable prefix validation\n")
{
  struct bgp *bgp = vty->index;
  bgp_flag_set(bgp, BGP_FLAG_VALIDATE_DISABLE);
  reprocess_routes(bgp);
  return CMD_SUCCESS;
}

DEFUN (no_bgp_bestpath_prefix_validate_disable,
    no_bgp_bestpath_prefix_validate_disable_cmd,
       "no bgp bestpath prefix-validate disable",
       NO_STR
       "BGP specific commands\n"
       "Change the default bestpath selection\n"
       "Prefix validation attribute\n"
       "Disable prefix validation\n")
{
  struct bgp *bgp = vty->index;
  bgp_flag_unset (bgp, BGP_FLAG_VALIDATE_DISABLE);
  reprocess_routes(bgp);
  return CMD_SUCCESS;
}

DEFUN (bgp_bestpath_prefix_validate_allow_invalid,
      bgp_bestpath_prefix_validate_allow_invalid_cmd,
       "bgp bestpath prefix-validate allow-invalid",
       "BGP specific commands\n"
       "Change the default bestpath selection\n"
       "Prefix validation attribute\n"
       "Allow routes to be selected as bestpath even if their prefix validation status is invalid\n")
{
  struct bgp *bgp = vty->index;
  bgp_flag_set (bgp, BGP_FLAG_ALLOW_INVALID);
  reprocess_routes(bgp);
  return CMD_SUCCESS;
}

DEFUN (no_bgp_bestpath_prefix_validate_allow_invalid,
    no_bgp_bestpath_prefix_validate_allow_invalid_cmd,
       "no bgp bestpath prefix-validate allow-invalid",
       NO_STR
       "BGP specific commands\n"
       "Change the default bestpath selection\n"
       "Prefix validation attribute\n"
       "Allow routes to be selected as bestpath even if their prefix validation status is invalid\n")
{
  struct bgp *bgp = vty->index;
  bgp_flag_unset (bgp, BGP_FLAG_ALLOW_INVALID);
  reprocess_routes(bgp);
  return CMD_SUCCESS;
}

DEFUN (show_rpki_prefix_table,
    show_rpki_prefix_table_cmd,
    "show rpki prefix-table",
    SHOW_STR
    RPKI_OUTPUT_STRING
    "Show validated prefixes which were received from RPKI Cache")
{
  if (rpki_is_synchronized())
    {
      rpki_print_prefix_table(vty);
    }
  else
    {
      vty_out(vty, "No connection to RPKI cache server.%s", VTY_NEWLINE);
    }
  return CMD_SUCCESS;
}

DEFUN (show_rpki_cache_connection,
    show_rpki_cache_connection_cmd,
    "show rpki cache-connection",
    SHOW_STR
    RPKI_OUTPUT_STRING
    "Show to which RPKI Cache Servers we have a connection")
{
  if (rpki_is_synchronized())
    {
      struct listnode *cache_group_node;
      cache_group* cache_group;
      int group = rpki_get_connected_group();
      if (group == -1)
        {
          vty_out(vty, "Cannot find a connected group. %s", VTY_NEWLINE);
          return CMD_SUCCESS;
        }
      vty_out(vty, "Connected to group %d %s", group, VTY_NEWLINE);
      for (ALL_LIST_ELEMENTS_RO(cache_group_list, cache_group_node, cache_group))
        {
          if (cache_group->preference_value == group)
            {
              struct list* cache_list = cache_group->cache_config_list;
              struct listnode* cache_node;
              cache* cache;

              for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache))
                {
                  switch (cache->type)
                    {
                    tr_tcp_config* tcp_config;
                    tr_ssh_config* ssh_config;
                  case TCP:
                    tcp_config = cache->tr_config.tcp_config;
                    vty_out(vty, "rpki cache %s %s %s", tcp_config->host,
                        tcp_config->port, VTY_NEWLINE);
                    break;

                  case SSH:
                    ssh_config = cache->tr_config.ssh_config;
                    vty_out(vty, "  rpki cache %s %u %s", ssh_config->host,
                        ssh_config->port, VTY_NEWLINE);
                    break;

                  default:
                    break;
                  }
              }
          }
      }
  }
  else
  {
    vty_out(vty, "No connection to RPKI cache server.%s", VTY_NEWLINE);
  }

  return CMD_SUCCESS;
}

DEFUN (rpki_exit,
    rpki_exit_cmd,
    "exit",
    "Exit rpki configuration and restart rpki session")
{
  rpki_reset_session();
  vty->node = CONFIG_NODE;
  return CMD_SUCCESS;
}

/* quit is alias of exit. */
ALIAS(rpki_exit,
    rpki_quit_cmd,
    "quit",
    "Exit rpki configuration and restart rpki session")

DEFUN (rpki_end,
    rpki_end_cmd,
    "end",
    "End rpki configuration, restart rpki session and change to enable mode.")
{
  rpki_reset_session();
  vty_config_unlock(vty);
  vty->node = ENABLE_NODE;
  return CMD_SUCCESS;
}

DEFUN (debug_rpki,
    debug_rpki_cmd,
       "debug rpki",
       DEBUG_STR
       "Enable debugging for rpki")
{
  rpki_debug = 1;
  return CMD_SUCCESS;
}

DEFUN (no_debug_rpki,
       no_debug_rpki_cmd,
       "no debug rpki",
       NO_STR
       DEBUG_STR
       "Disable debugging for rpki")
{
  rpki_debug = 0;
  return CMD_SUCCESS;
}

static void
overwrite_exit_commands()
{
  unsigned int i;
  vector cmd_vector = rpki_node.cmd_vector;
  for (i = 0; i < cmd_vector->active; ++i)
  {
    struct cmd_element* cmd = (struct cmd_element*) vector_lookup(cmd_vector, i);
    if (strcmp(cmd->string, "exit") == 0 || strcmp(cmd->string, "quit") == 0
        || strcmp(cmd->string, "exit") == 0)
      {
        vector_unset(cmd_vector, i);
      }
  }
  /*
   The comments in the following 3 lines must not be removed.
   They prevent the script ../vtysh/extract.pl from copying the lines
   into ../vtysh/vtysh_cmd.c which would cause the commands to be ambiguous
   and we don't want that.
   */
  install_element(RPKI_NODE /*DO NOT REMOVE THIS COMMENT*/, &rpki_exit_cmd);
  install_element(RPKI_NODE /*DO NOT REMOVE THIS COMMENT*/, &rpki_quit_cmd);
  install_element(RPKI_NODE /*DO NOT REMOVE THIS COMMENT*/, &rpki_end_cmd);
}

/**************************************************/
/** Declaration of route-map match rpki command  **/
/**************************************************/

static route_map_result_t
route_match_rpki(void *rule, struct prefix *prefix, route_map_object_t type,
void *object)
{
  int* rpki_status = rule;
  struct bgp_info* bgp_info;

  if (type == RMAP_BGP)
  {
    bgp_info = object;

    if (rpki_validate_prefix(bgp_info->peer, bgp_info->attr, prefix)
        == *rpki_status)
      {
        return RMAP_MATCH;
      }
  }
  return RMAP_NOMATCH;
}

static void*
route_match_rpki_compile(const char *arg)
{
  int* rpki_status;

  rpki_status = XMALLOC(MTYPE_ROUTE_MAP_COMPILED, sizeof(u_char));

  if (strcmp(arg, "valid") == 0)
  *rpki_status = RPKI_VALID;
  else if (strcmp(arg, "invalid") == 0)
  *rpki_status = RPKI_INVALID;
  else
  *rpki_status = RPKI_NOTFOUND;

  return rpki_status;
}

static void
route_match_rpki_free(void *rule)
{
  rpki_set_route_map_active(0);
  XFREE(MTYPE_ROUTE_MAP_COMPILED, rule);
}

/* Route-Map match RPKI structure */
struct route_map_rule_cmd route_match_rpki_cmd = { "rpki", route_match_rpki,
route_match_rpki_compile, route_match_rpki_free };

/**************************************/
/** Declaration of public functions  **/
/**************************************/

unsigned int
get_number_of_cache_groups()
{
  delete_marked_cache_groups();
  return listcount(cache_group_list);
}

rtr_mgr_group*
get_rtr_mgr_groups()
{
  struct listnode *cache_group_node;
  cache_group* cache_group;
  rtr_mgr_group* rtr_mgr_groups;
  rtr_mgr_group* loop_group_pointer;

  delete_marked_cache_groups();
  int number_of_groups = listcount(cache_group_list);
  if (number_of_groups == 0)
  {
    return NULL ;
  }

  if ((rtr_mgr_groups = XMALLOC(MTYPE_BGP_RPKI_CACHE_GROUP,
    number_of_groups * sizeof(rtr_mgr_group))) == NULL )
  {
    return NULL ;
  }
  loop_group_pointer = rtr_mgr_groups;

  for (ALL_LIST_ELEMENTS_RO(cache_group_list, cache_group_node, cache_group))
  {
    struct list* cache_list = cache_group->cache_config_list;
    struct listnode* cache_node;
    cache* cache;
    rtr_socket** loop_cache_pointer;
    int number_of_caches = listcount(cache_list);
    if (number_of_caches == 0)
      {
        break;
      }
    if ((loop_group_pointer->sockets = XMALLOC(MTYPE_BGP_RPKI_CACHE,
        number_of_caches * sizeof(rtr_socket*))) == NULL )
      {
        return NULL ;
      }
    loop_cache_pointer = loop_group_pointer->sockets;

    for (ALL_LIST_ELEMENTS_RO(cache_list, cache_node, cache))
      {
        *loop_cache_pointer = cache->rtr_socket;
        loop_cache_pointer++;
      }
    loop_group_pointer->sockets_len = number_of_caches;
    loop_group_pointer->preference = cache_group->preference_value;
    loop_group_pointer++;
  }

  if (loop_group_pointer == rtr_mgr_groups)
  {
    // No caches were found in config file
    return NULL ;
  }
  return rtr_mgr_groups;
}

void
free_rtr_mgr_groups(rtr_mgr_group* group, int length)
{
  int i;
  rtr_mgr_group* group_temp = group;
  for (i = 0; i < length; ++i)
  {
    XFREE(MTYPE_BGP_RPKI_CACHE, group_temp->sockets);
    group_temp++;
  }

  XFREE(MTYPE_BGP_RPKI_CACHE_GROUP, group);
}

void
delete_cache_group_list()
{
  list_delete(cache_group_list);
}

void
install_cli_commands()
{
  cache_group_list = list_new();
  cache_group_list->del = delete_cache_group;
  cache_group_list->count = 0;

  install_node(&rpki_node, rpki_config_write);
  install_default(RPKI_NODE);
  overwrite_exit_commands();
  install_element(CONFIG_NODE, &enable_rpki_cmd);

  /* Install rpki polling period commands */
  install_element(RPKI_NODE, &rpki_polling_period_cmd);
  install_element(RPKI_NODE, &no_rpki_polling_period_cmd);

  /* Install rpki timeout commands */
  install_element(RPKI_NODE, &rpki_timeout_cmd);
  install_element(RPKI_NODE, &no_rpki_timeout_cmd);

  /* Install rpki synchronisation timeout commands */
  install_element(RPKI_NODE, &rpki_synchronisation_timeout_cmd);
  install_element(RPKI_NODE, &no_rpki_synchronisation_timeout_cmd);

  /* Install rpki group commands */
  install_element(RPKI_NODE, &rpki_group_cmd);
  install_element(RPKI_NODE, &no_rpki_group_cmd);

  /* Install rpki cache commands */
  install_element(RPKI_NODE, &rpki_cache_cmd);
  install_element(RPKI_NODE, &no_rpki_cache_cmd);

  /* Install prefix_validate disable commands */
  install_element(BGP_NODE, &bgp_bestpath_prefix_validate_disable_cmd);
  install_element(BGP_NODE, &no_bgp_bestpath_prefix_validate_disable_cmd);

  /* Install prefix_validate allow_invalid commands */
  install_element(BGP_NODE, &bgp_bestpath_prefix_validate_allow_invalid_cmd);
  install_element(BGP_NODE, &no_bgp_bestpath_prefix_validate_allow_invalid_cmd);

  /* Install show commands */
  install_element(VIEW_NODE, &show_rpki_prefix_table_cmd);
  install_element(VIEW_NODE, &show_rpki_cache_connection_cmd);
  install_element(RESTRICTED_NODE, &show_rpki_prefix_table_cmd);
  install_element(RESTRICTED_NODE, &show_rpki_cache_connection_cmd);
  install_element(ENABLE_NODE, &show_rpki_prefix_table_cmd);
  install_element(ENABLE_NODE, &show_rpki_cache_connection_cmd);

  /* Install debug commands */
  install_element(CONFIG_NODE, &debug_rpki_cmd);
  install_element(ENABLE_NODE, &debug_rpki_cmd);
  install_element(CONFIG_NODE, &no_debug_rpki_cmd);
  install_element(ENABLE_NODE, &no_debug_rpki_cmd);

  /* Install route match */
  route_map_install_match(&route_match_rpki_cmd);
}
