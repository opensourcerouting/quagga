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

#ifndef BABEL_AUTH_H
#define BABEL_AUTH_H

/* configured security association */
struct babel_csa_item
{
  unsigned hash_algo;
  char * keychain_name;
};

/* authentication statistics */
struct babel_auth_stats
{
  unsigned long plain_sent;
  unsigned long plain_recv;
  unsigned long auth_sent;
  unsigned long auth_sent_ng_nokeys;     /* ESA list empty in Tx              */
  unsigned long auth_recv_ng_nokeys;     /* ESA list empty on Rx              */
  unsigned long auth_recv_ng_no_pcts;    /* no PC/TS TLVs in the packet       */
  unsigned long auth_recv_ng_pcts;       /* 1st PS/TS TLV fails ANM check     */
  unsigned long auth_recv_ng_hd;         /* no HD TLV passes ESA check        */
  unsigned long auth_recv_ok;
  unsigned long internal_err;
};

#include "command.h"

#ifdef HAVE_LIBGCRYPT
#include "cryptohash.h"
#include "if.h"
#include "thread.h"
#define BABEL_MAXDIGESTSOUT 8
#define BABEL_MAXDIGESTSIN 4
/* 1 PC/TS, maximum size/amount of HD TLVs, all including Type and Length fields */
#define BABEL_MAXAUTHSPACE (8 + BABEL_MAXDIGESTSOUT * (4 + HASH_SIZE_MAX))
extern int babel_auth_check_packet (struct interface *, const unsigned char *,
                                    const unsigned char *, const u_int16_t);
extern int babel_auth_make_packet (struct interface *, unsigned char *, const u_int16_t);
extern int babel_auth_do_housekeeping (struct thread *);
#else
#define BABEL_MAXDIGESTSOUT 0
#define BABEL_MAXDIGESTSIN 0
#define BABEL_MAXAUTHSPACE 0
#endif /* HAVE_LIBGCRYPT */

extern void babel_auth_init (void);
extern int babel_auth_config_write (struct vty *);
extern void show_babel_auth_parameters (struct vty *);

#endif /* BABEL_AUTH_H */
