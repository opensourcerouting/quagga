/* Interface link state tracking
 * Copyright (C) 2008 Stephen Hemminger
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
#include "log.h"
#include "privs.h"

#include "log.h"
#include "privs.h"
#include "prefix.h"

#include "zebra/interface.h"

extern struct zebra_privs_t zserv_privs;

static int
linkdetect (const char *name, int onoff)
{
  FILE *fp;
  int save_errno;
  char proc_name[128];

  snprintf(proc_name, sizeof(proc_name)-1, 
	   "/proc/sys/net/ipv4/conf/%s/link_detect", name);
  
  if ( zserv_privs.change(ZPRIVS_RAISE) )
    zlog_err ("Can't raise privileges, %s", safe_strerror (errno) );

  fp = fopen (proc_name, "w");
  save_errno = errno;
  if (!fp)
    {
      if ( zserv_privs.change(ZPRIVS_LOWER) )
	zlog_err ("Can't lower privileges, %s", safe_strerror (errno));

      zlog_info("Can't %s link-detect, %s:%s",
		onoff ? "enable" : "disable",
		proc_name, safe_strerror(save_errno));
      return -1;
    }
  else
    {
      fprintf (fp, "%d\n", onoff);
      fclose (fp);

      if ( zserv_privs.change(ZPRIVS_LOWER) )
	zlog_err ("Can't lower privileges, %s", safe_strerror (errno));

      return onoff;
    }
}

int
if_linkdetect_on (const char *name)
{
  return linkdetect (name, 1);
}

int
if_linkdetect_off (const char *name)
{
  return linkdetect (name, 1);
}
