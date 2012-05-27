/*
Copyright 2012 by Denis Ovsienko

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <zebra.h>
#include "log.h"
#include "memory.h"
#include "stream.h"
#include "linklist.h"
#include "cryptohash.h"

#include "babeld/babel_auth.h"
#include "babeld/babel_interface.h"
#include "babeld/util.h"
#include "babeld/message.h"

#define BABEL_TS_BASE_ZERO        0
#define BABEL_TS_BASE_UNIX        1
#define BABEL_DEFAULT_TS_BASE     BABEL_TS_BASE_UNIX
#define BABEL_DEFAULT_ANM_TIMEOUT 300

/* authentic neighbors memory */
struct babel_anm_item
{
  struct in6_addr address;
  struct interface *ifp;
  time_t last_recv;
  u_int16_t last_pc;
  u_int32_t last_ts;
};

/* local routing process variables */
static u_int16_t auth_packetcounter;
static u_int32_t auth_timestamp;
static unsigned char ts_base;
static u_int32_t anm_timeout;
static struct list *anmlist;

/* statistics */
static unsigned long stats_plain_sent;
static unsigned long stats_plain_recv;
static unsigned long stats_auth_sent;
static unsigned long stats_auth_recv_ng_pcts;
static unsigned long stats_auth_recv_ng_hd;
static unsigned long stats_auth_recv_ok;
static unsigned long stats_internal_err;

static const struct message ts_base_cli_str[] =
{
  { BABEL_TS_BASE_ZERO, "zero"      },
  { BABEL_TS_BASE_UNIX, "unixtime"  },
};
static const size_t ts_base_cli_str_max = sizeof (ts_base_cli_str) / sizeof (struct message);

static const struct message ts_base_str[] =
{
  { BABEL_TS_BASE_ZERO, "NVRAM-less PC wrap counter"  },
  { BABEL_TS_BASE_UNIX, "UNIX time w/PC wrap counter" },
};
static const size_t ts_base_str_max = sizeof (ts_base_str) / sizeof (struct message);


/* List hook function to deallocate an ANM record. */
static void
babel_anm_free (void * node)
{
  XFREE (MTYPE_BABEL_AUTH, node);
}

static void
babel_auth_stats_reset()
{
  stats_plain_sent = 0;
  stats_plain_recv = 0;
  stats_auth_sent = 0;
  stats_auth_recv_ng_pcts = 0;
  stats_auth_recv_ng_hd = 0;
  stats_auth_recv_ok = 0;
  stats_internal_err = 0;
}

void
show_babel_auth_parameters (struct vty *vty)
{
    vty_out(vty,
            "libgcrypt enabled       = %s%s"
            "HMAC-SHA256 enabled     = %s%s"
            "HMAC-SHA384 enabled     = %s%s"
            "HMAC-SHA512 enabled     = %s%s"
            "HMAC-RMD160 enabled     = %s%s"
            "HMAC-Whirlpool enabled  = %s%s"
            "MaxDigestsIn            = %u%s"
            "MaxDigestsOut           = %u%s"
            "Timestamp               = %u%s"
            "Packet counter          = %u%s"
            "Timestamp base          = %s%s"
            "Memory timeout          = %u%s",
#ifdef HAVE_LIBGCRYPT
            "yes", VTY_NEWLINE,
#else
            "no", VTY_NEWLINE,
#endif /* HAVE_LIBGCRYPT */
            hash_algo_enabled (HASH_HMAC_SHA256) ? "yes" : "no", VTY_NEWLINE,
            hash_algo_enabled (HASH_HMAC_SHA384) ? "yes" : "no", VTY_NEWLINE,
            hash_algo_enabled (HASH_HMAC_SHA512) ? "yes" : "no", VTY_NEWLINE,
            hash_algo_enabled (HASH_HMAC_RMD160) ? "yes" : "no", VTY_NEWLINE,
            hash_algo_enabled (HASH_HMAC_WHIRLPOOL) ? "yes" : "no", VTY_NEWLINE,
            BABEL_MAXDIGESTSIN, VTY_NEWLINE,
            BABEL_MAXDIGESTSOUT, VTY_NEWLINE,
            auth_timestamp, VTY_NEWLINE,
            auth_packetcounter, VTY_NEWLINE,
            LOOKUP (ts_base_str, ts_base), VTY_NEWLINE,
            anm_timeout, VTY_NEWLINE);
}

DEFUN (anm_timeout_val,
       anm_timeout_val_cmd,
       "anm-timeout <5-4294967295>",
       "Authentic neighbors memory\n"
       "Timeout in seconds")
{
  VTY_GET_INTEGER_RANGE ("timeout", anm_timeout, argv[0], 5, UINT32_MAX);
  return CMD_SUCCESS;
}

DEFUN (no_anm_timeout_val,
       no_anm_timeout_val_cmd,
       "no anm-timeout <5-4294967295>",
       NO_STR
       "Authentic neighbors memory\n"
       "Timeout in seconds")
{
  anm_timeout = BABEL_DEFAULT_ANM_TIMEOUT;
  return CMD_SUCCESS;
}

ALIAS (no_anm_timeout_val,
       no_anm_timeout_cmd,
       "no anm-timeout",
       NO_STR
       "Authentic neighbors memory\n"
       "Timeout in seconds")

DEFUN (ts_base_val,
       ts_base_val_cmd,
       "ts-base (zero|unixtime)",
       "Packet timestamp base\n"
       "NVRAM-less PC wrap counter\n"
       "UNIX time w/PC wrap counter")
{
  if (! strcmp (argv[0], "zero"))
    ts_base = BABEL_TS_BASE_ZERO;
  else if (! strcmp (argv[0], "unixtime"))
    ts_base = BABEL_TS_BASE_UNIX;
  return CMD_SUCCESS;
}

DEFUN (no_ts_base,
       no_ts_base_val_cmd,
       "no ts-base (zero|unixtime)",
       NO_STR
       "Packet timestamp base\n"
       "NVRAM-less PC wrap counter\n"
       "UNIX time w/PC wrap counter")
{
  ts_base = BABEL_DEFAULT_TS_BASE;
  return CMD_SUCCESS;
}

ALIAS (no_ts_base,
       no_ts_base_cmd,
       "no ts-base",
       NO_STR
       "Packet timestamp base")

DEFUN (show_babel_authentication_stats,
       show_babel_authentication_stats_cmd,
       "show babel authentication stats",
       SHOW_STR
       "Babel information\n"
       "Packet authentication\n"
       "Authentication statistics")
{
  const char *format_lu = "%-27s: %lu%s";

  vty_out (vty, "== Packet authentication statistics ==%s", VTY_NEWLINE);
  vty_out (vty, format_lu, "Plain Rx", stats_plain_recv, VTY_NEWLINE);
  vty_out (vty, format_lu, "Plain Tx", stats_plain_sent, VTY_NEWLINE);
  vty_out (vty, format_lu, "Authenticated Tx", stats_auth_sent, VTY_NEWLINE);
  vty_out (vty, format_lu, "Authenticated Rx OK", stats_auth_recv_ok, VTY_NEWLINE);
  vty_out (vty, format_lu, "Authenticated Rx bad PC/TS", stats_auth_recv_ng_pcts, VTY_NEWLINE);
  vty_out (vty, format_lu, "Authenticated Rx bad HD", stats_auth_recv_ng_hd, VTY_NEWLINE);
  vty_out (vty, format_lu, "Internal errors", stats_internal_err, VTY_NEWLINE);
  vty_out (vty, format_lu, "ANM records", listcount (anmlist), VTY_NEWLINE);
  return CMD_SUCCESS;
}

DEFUN (clear_babel_authentication_stats,
       clear_babel_authentication_stats_cmd,
       "clear babel authentication stats",
       CLEAR_STR
       "Babel information\n"
       "Packet authentication\n"
       "Authentication statistics")
{
  babel_auth_stats_reset();
  return CMD_SUCCESS;
}

DEFUN (show_babel_authentication_memory,
       show_babel_authentication_memory_cmd,
       "show babel authentication memory",
       SHOW_STR
       "Babel information\n"
       "Packet authentication\n"
       "Authentic neighbors memory")
{
  struct listnode *node;
  struct babel_anm_item *anm;
  char buffer[INET6_ADDRSTRLEN];
  time_t now = quagga_time (NULL);
  const char *format_s = "%46s %10s %10s %5s %10s%s";
  const char *format_u = "%46s %10s %10u %5u %10u%s";

  vty_out (vty, "ANM timeout: %u seconds, ANM records: %u%s", anm_timeout,
           listcount (anmlist), VTY_NEWLINE);
  vty_out (vty, format_s, "Source address", "Interface", "TS", "PC", "Age", VTY_NEWLINE);
  for (ALL_LIST_ELEMENTS_RO (anmlist, node, anm))
  {
    inet_ntop (AF_INET6, &anm->address, buffer, INET6_ADDRSTRLEN);
    vty_out (vty, format_u, buffer, anm->ifp->name, anm->last_ts, anm->last_pc,
             now - anm->last_recv, VTY_NEWLINE);
  }
  return CMD_SUCCESS;
}

DEFUN (clear_babel_authentication_memory,
       clear_babel_authentication_memory_cmd,
       "clear babel authentication memory",
       CLEAR_STR
       "Babel information\n"
       "Packet authentication\n"
       "Authentic neighbors memory")
{
  list_delete_all_node (anmlist);
  return CMD_SUCCESS;
}

int
babel_auth_config_write (struct vty *vty)
{
  int lines = 0;

  if (anm_timeout != BABEL_DEFAULT_ANM_TIMEOUT)
  {
      vty_out (vty, " anm-timeout %u%s", anm_timeout, VTY_NEWLINE);
      lines++;
  }
  if (ts_base != BABEL_DEFAULT_TS_BASE)
  {
      vty_out (vty, " ts-base %s%s", LOOKUP (ts_base_cli_str, ts_base), VTY_NEWLINE);
      lines++;
  }
  return lines;
}

void
babel_auth_init()
{
  if (hash_library_init())
    exit (1);
  anmlist = list_new();
  anmlist->del = babel_anm_free;
  babel_auth_stats_reset();
  auth_packetcounter = 0;
  auth_timestamp = 0;
  anm_timeout = BABEL_DEFAULT_ANM_TIMEOUT;
  ts_base = BABEL_DEFAULT_TS_BASE;
  install_element (BABEL_NODE, &anm_timeout_val_cmd);
  install_element (BABEL_NODE, &no_anm_timeout_val_cmd);
  install_element (BABEL_NODE, &no_anm_timeout_cmd);
  install_element (BABEL_NODE, &ts_base_val_cmd);
  install_element (BABEL_NODE, &no_ts_base_val_cmd);
  install_element (BABEL_NODE, &no_ts_base_cmd);
  install_element (VIEW_NODE, &show_babel_authentication_stats_cmd);
  install_element (VIEW_NODE, &show_babel_authentication_memory_cmd);
  install_element (ENABLE_NODE, &show_babel_authentication_stats_cmd);
  install_element (ENABLE_NODE, &show_babel_authentication_memory_cmd);
  install_element (ENABLE_NODE, &clear_babel_authentication_stats_cmd);
  install_element (ENABLE_NODE, &clear_babel_authentication_memory_cmd);
}
