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

#include "command.h"

#ifdef HAVE_LIBGCRYPT
#define BABEL_MAXDIGESTSOUT 8
#define BABEL_MAXDIGESTSIN 4
#else
#define BABEL_MAXDIGESTSOUT 0
#define BABEL_MAXDIGESTSIN 0
#endif /* HAVE_LIBGCRYPT */

extern void babel_auth_init (void);
extern int babel_auth_config_write (struct vty *);
extern void show_babel_auth_parameters (struct vty *);

#endif /* BABEL_AUTH_H */
