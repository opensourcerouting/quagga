/*
 * Copyright (c) 2016  David Lamparter, for NetDEF, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _QUAGGA_HOOK_H
#define _QUAGGA_HOOK_H

#include "plugin.h"
#include "memory.h"

struct hookent {
	struct hookent *next;
	void *hookfn;
	void *hookarg;
	struct qplug_runtime *plugin;
};

struct hook {
	const char *hookname;
	struct hookent *entries;
};

DECLARE_MTYPE(HOOK_ENTRY)

#define HOOK_ADDDEF(...) (void *hookarg, __VA_ARGS__)
#define HOOK_ADDARG(...) (hookarg, __VA_ARGS__)

#define DECLARE_HOOK(name, arglist, passlist) \
	extern void name ## _sub_plugin (void (*funcptr) HOOK_ADDDEF arglist, \
			void *arg, struct qplug_runtime *plugin); \
	static inline void name ## _sub (void (*funcptr) HOOK_ADDDEF arglist, \
			void *arg) { \
		name ## _sub_plugin (funcptr, arg, THIS_PLUGIN); \
	}

#define DEFINE_HOOK(name, arglist, passlist) \
	static struct hook _hook_ ## name = { \
		.hookname = #name, \
		.entries = NULL, \
	}; \
	static void name ## _call arglist { \
		struct hookent *he = _hook_ ## name .entries; \
		void *hookarg; \
		union { \
			void *voidptr; \
			void (*fptr) HOOK_ADDDEF arglist; \
		} u; \
		for (; he; he = he->next) { \
			hookarg = he->hookarg; \
			u.voidptr = he->hookfn; \
			u.fptr HOOK_ADDARG passlist; \
		} \
	} \
	void name ## _sub_plugin (void (*funcptr) HOOK_ADDDEF arglist, \
			void *arg, struct qplug_runtime *plugin) { \
		struct hookent *he = XCALLOC(MTYPE_TMP, sizeof(*he)); \
		he->hookfn = funcptr; \
		he->hookarg = arg; \
		he->next = _hook_ ## name .entries; \
		_hook_ ## name .entries = he; \
	}

#endif /* _QUAGGA_HOOK_H */
