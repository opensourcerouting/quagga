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

#include <zebra.h>
#include <log.h>
#include <paths.h>

static char *namespace = "";

void path_set_namespace(const char *ns)
{
	size_t len;

	if (*namespace)
		free(namespace);
	if (!ns || !*ns) {
		namespace = "";
		return;
	}

	len = strlen(ns);
	namespace = malloc(len + 2);
	if (!namespace) {		/* ugh... */
		namespace = "";
		return;
	};
	namespace[0] = '/';
	memcpy(namespace + 1, ns, len + 1);
}

const char *path_state(const char *filename)
{
	static char buf[MAXPATHLEN];
	snprintf(buf, sizeof(buf), "%s%s/%s",
			PATH_STATE, namespace, filename);
	return buf;
}

const char *path_config(const char *filename)
{
	static char buf[MAXPATHLEN];
	snprintf(buf, sizeof(buf), "%s%s/%s",
			PATH_CONFIG, namespace, filename);
	return buf;
}

