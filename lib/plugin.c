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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <dlfcn.h>

#include "plugin.h"
#include "memory.h"
#include "version.h"

DEFINE_MTYPE_STATIC(LIB, PLUGIN_LOADNAME, "Plugin loading name")
DEFINE_MTYPE_STATIC(LIB, PLUGIN_LOADARGS, "Plugin loading arguments")

static struct qplug_info qplug_daemon_executable_info = {
	.name = "libzebra",
	.version = QUAGGA_VERSION,
	.description = "libzebra (no plugin)",
};
union _qplug_runtime_u qplug_default = {
	.r.info = &qplug_daemon_executable_info,
	.r.finished_loading = 1,
};

#if defined(HAVE_SYS_WEAK_ALIAS_ATTRIBUTE)
union _qplug_runtime_u _qplug_this_plugin
	__attribute__((weak, alias("qplug_default")));
#elif defined(HAVE_SYS_WEAK_ALIAS_PRAGMA)
#pragma weak _qplug_this_plugin = qplug_default
#else
#error need weak symbol support
#endif

struct qplug_runtime *qplug_list = &_qplug_this_plugin.r;
static struct qplug_runtime **qplug_last = &_qplug_this_plugin.r.next;

#define PLUGIN_PATH "/usr/lib/quagga/plugins"

struct qplug_runtime *qplug_load(const char *spec,
		char *err, size_t err_len)
{
	void *handle = NULL, *handle2;
	char name[PATH_MAX], fullpath[PATH_MAX], *args;
	struct qplug_runtime *rtinfo;
	const struct qplug_info *info;

	snprintf(name, sizeof(name), "%s", spec);
	args = strchr(name, ':');
	if (args)
		*args++ = '\0';

	if (!strchr(name, '/')) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s.so",
				PLUGIN_PATH, name);
		handle = dlopen(fullpath, RTLD_LAZY | RTLD_GLOBAL);
	}
	if (!handle) {
		snprintf(fullpath, sizeof(fullpath), "%s", name);
		handle = dlopen(fullpath, RTLD_LAZY | RTLD_GLOBAL);
	}
	if (!handle) {
		if (err)
			snprintf(err, err_len,
					"loading plugin \"%s\" failed: %s",
					name, dlerror());
		return NULL;
	}

	rtinfo = dlsym(handle, "_qplug_this_plugin");
	if (!rtinfo) {
		dlclose(handle);
		if (err)
			snprintf(err, err_len,
					"\"%s\" is not a Quagga plugin: %s",
					name, dlerror());
		return NULL;
	}
	rtinfo->load_name = XSTRDUP(MTYPE_PLUGIN_LOADNAME, name);
	if (args)
		rtinfo->load_args = XSTRDUP(MTYPE_PLUGIN_LOADARGS, args);
	info = rtinfo->info;

	if (rtinfo->finished_loading) {
		dlclose(handle);
		if (err)
			snprintf(err, err_len,
					"plugin \"%s\" already loaded",
					name);
		goto out_fail;
	}

#if 0
	/* not worth the effort - incompatible plugins will fail to load
	 * with undefined references anyway */
	do {
		char daemon_delim[64];
		snprintf(daemon_delim, sizeof(daemon_delim), ";%s;",
				qplug_daemon_name);

		if (!info->compat_list)
			break;
		if (strstr(info->compat_list, daemon_delim))
			break;
		if (strstr(info->compat_list, ";all;"))
			break;

		dlclose(handle);
		if (err)
			snprintf(err, err_len,
					"plugin \"%s\" not compatible with"
					"this Quagga daemon", name);
		goto out_fail;
	} while (0);
#endif

	/* force symbol resolution now, so we don't explode at runtime.
	 * this is not on first load to make more specific errors possible */
	handle2 = dlopen(fullpath, RTLD_NOW | RTLD_GLOBAL);
	if (!handle2) {
		if (err)
			snprintf(err, err_len,
					"plugin \"%s\" symbol resolution "
					"failed: %s", name, dlerror());
		dlclose(handle);
		goto out_fail;
	}
	dlclose(handle);
	rtinfo->dl_handle = handle2;

	if (info->init && info->init()) {
		dlclose(handle2);
		if (err)
			snprintf(err, err_len,
					"plugin \"%s\" initialisation failed",
					name);
		goto out_fail;
	}

	rtinfo->finished_loading = 1;

	*qplug_last = rtinfo;
	qplug_last = &rtinfo->next;
	return rtinfo;

out_fail:
	if (rtinfo->load_args)
		XFREE(MTYPE_PLUGIN_LOADARGS, rtinfo->load_args);
	XFREE(MTYPE_PLUGIN_LOADNAME, rtinfo->load_name);
	return NULL;
}

void qplug_unload(struct qplug_runtime *plugin)
{
}
