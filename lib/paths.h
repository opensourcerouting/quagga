/*
 * Path helper functions (for namespaces)
 * Copyright (C) 2009 David Lamparter
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your
 * option) any later version.
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

#ifndef _ZEBRA_PATHS_H
#define _ZEBRA_PATHS_H

extern void path_set_namespace(const char *ns);

/* the following two functions return pointers to a _static_ buffer!
 * if you need to keep a reference to the values, do a strdup
 */
extern const char *path_state(const char *filename);
extern const char *path_config(const char *filename);

#endif /* _ZEBRA_PATHS_H */
