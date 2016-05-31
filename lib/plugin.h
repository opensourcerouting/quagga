/*
 * Copyright (c) 2015-16  David Lamparter, for NetDEF, Inc.
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

#ifndef _QUAGGA_PLUGIN_H
#define _QUAGGA_PLUGIN_H

#include <stdint.h>
#include <stdbool.h>

#if !defined(__GNUC__)
# error plugin code needs GCC visibility extensions
#elif __GNUC__ < 4
# error plugin code needs GCC visibility extensions
#else
# define DSO_PUBLIC __attribute__ ((visibility ("default")))
# define DSO_SELF   __attribute__ ((visibility ("protected")))
# define DSO_LOCAL  __attribute__ ((visibility ("hidden")))
#endif

struct qplug_runtime;

struct qplug_info {
	/* single-line few-word title */
	const char *name;
	/* human-readable version number, should not contain spaces */
	const char *version;
	/* one-paragraph description */
	const char *description;

	int (*init)(void);

#if 0
	/* list of daemons with which this plugin can be used.
	 * semicolon separated:  ";daemon1;daemon2;daemon3;"
	 * (note semicolon at start!)
	 * NULL or special value "all" means no restrictions.
	 *
	 * NB: in most cases, loading a daemon-specific module will fail
	 * anyway because there will be unresolved symbols
	 * (if not, loader code will unload the module without calling init)
	 */
	const char *compat_list;

	/* for version checks when depending on other plugins;  not used or
	 * displayed anywhere in main Quagga
	 *
	 * failing with unresolved symbols when a prerequisite plugin is
	 * missing is always an option too */
	uint8_t v_major, v_minor, v_revision, v_patch;
#endif
};

/* primary entry point structure to be present in loadable plugin under
 * "_qplug_this_plugin" dlsym() name
 *
 * note space for future extensions is reserved below, so other modules
 * (e.g. memory management, hooks) can add fields
 *
 * const members/info are in qplug_info.
 */
struct qplug_runtime {
	struct qplug_runtime *next;

	const struct qplug_info *info;
	void *dl_handle;
	bool finished_loading;

	char *load_name;
	char *load_args;
};

/* space-reserving foo */
struct _qplug_runtime_size {
	struct qplug_runtime r;
	/* this will barf if qplug_runtime exceeds 1024 bytes ... */
	uint8_t space[1024 - sizeof(struct qplug_runtime)];
};
union _qplug_runtime_u {
	struct qplug_runtime r;
	struct _qplug_runtime_size s;
};

extern DSO_SELF union _qplug_runtime_u _qplug_this_plugin;
#define THIS_PLUGIN (&_qplug_this_plugin.r)

#define QUAGGA_PLUGIN_SETUP(...) \
	static const struct qplug_info _qplug_info = { __VA_ARGS__ }; \
	DSO_SELF union _qplug_runtime_u _qplug_this_plugin = { \
		.r.info = &_qplug_info, \
	};

extern struct qplug_runtime *qplug_list;

extern struct qplug_runtime *qplug_load(const char *spec,
		char *err, size_t err_len);
extern void qplug_unload(struct qplug_runtime *plugin);

#endif /* _QUAGGA_PLUGIN_H */
