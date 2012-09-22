#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include "logutils.h"
#include "test_utils.h"
#include <nagios/nebstructs.h>
#include <nagios/nebmodules.h>
#include <nagios/nebmods.h>
#include <nagios/broker.h>
#include <nagios/objects.h>
#include <nagios/macros.h>
#include <nagios/lib/libnagios.h>
#include "nagios-stubs.h"

static int (*hooks[NEBCALLBACK_NUMITEMS])(int, void *);
static int reg_errors;
static int dereg_errors;

int is_tested(int callback_type, const char *caller_func)
{
	switch (callback_type) {
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_DOWNTIME_DATA:
	case NEBCALLBACK_PROCESS_DATA:
		t_pass("%s: callback %d is handled by nebtest",
			   caller_func, callback_type);
		return 1;
	}

	t_fail("%s: callback %d is unhandled by nebtest",
		   caller_func, callback_type);
	return 0;
}

int neb_register_callback(int callback_type, void *mod_handle,
						  int priority, int (*callback_func)(int,void *))
{
	if (!is_tested(callback_type, __func__))
		reg_errors |= 1 << callback_type;

	if (hooks[callback_type]) {
		reg_errors |= 1 << callback_type;
		printf("! Registering a second hook for %d!", callback_type);
	}

	hooks[callback_type] = callback_func;

	return 0;
}

int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *))
{
	if (!is_tested(callback_type, __func__))
		dereg_errors |= 1 << callback_type;

	if (!hooks[callback_type]) {
		dereg_errors |= 1 << callback_type;
		printf("! Trying to unregister an unregistered hook!\n");
	}

	hooks[callback_type] = NULL;

	return 0;
}

static void check_callbacks(void)
{
	int i;

	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		/* only print it if something's wrong, to suppress noise */
		if (!hooks[i])
			continue;

		t_fail("hook %d not unregistered by module unload function", i);
	}
}

static int check_symbols(void *dso)
{
	int i, result = 0;

	const char *syms[] = {
		"__neb_api_version", "nebmodule_init", "nebmodule_deinit",
			NULL,
	};

	for (i = 0; syms[i]; i++) {
		const char *sym = syms[i];

		if (dlsym(dso, sym))
			t_pass("%s exists in module", sym);
		else {
			t_fail("%s can't be looked up: %s", sym, dlerror());
			result = -1;
		}
	}

	return result;
}


static char *__host_name = "ndb, host";
static char *__service_description = "ndb, service";
#define init_ds(x) \
	do { \
		memset(&x, 0, sizeof(x)); \
		x.timestamp.tv_sec = time(NULL); \
		x.host_name = __host_name; \
	} while(0)
#define set_service(x) x.service_description = __service_description

static int test_sql_host_insert(void)
{
	int (*hook)(int, void *);
	nebstruct_host_check_data ds;
	int cb = NEBCALLBACK_HOST_CHECK_DATA;

	if (!(hook = hooks[cb])) {
		return -1;
	}

	init_ds(ds);
	ds.type = NEBTYPE_HOSTCHECK_PROCESSED;
	ds.output = "nebtest host check";
	return hook(cb, &ds);
}

static int test_sql_service_insert(void)
{
	int (*hook)(int, void *);
	nebstruct_service_check_data ds;
	int cb = NEBCALLBACK_SERVICE_CHECK_DATA;

	if (!(hook = hooks[cb])) {
		printf("  ! Missing service_check hook\n");
		return -1;
	}

	init_ds(ds);
	set_service(ds);
	ds.type = NEBTYPE_SERVICECHECK_PROCESSED;
	ds.output = strdup("nebtest service check");

	return hook(cb, &ds);
}

static int test_sql_process_data_insert(void)
{
	int (*hook)(int, void *);
	nebstruct_process_data ds;
	int cb = NEBCALLBACK_PROCESS_DATA;

	if (!(hook = hooks[cb])) {
		printf("  ! Missing process data hook\n");
		return -1;
	}

	memset(&ds, 0, sizeof(ds));
	ds.timestamp.tv_sec = time(NULL);
	ds.type = NEBTYPE_PROCESS_START;

	return hook(cb, &ds);
}

static void test_sql_downtime_insert(void)
{
	int (*hook)(int, void *);
	nebstruct_downtime_data ds;
	int cb = NEBCALLBACK_DOWNTIME_DATA;

	if (!(hook = hooks[cb])) {
		t_fail("downtime data hook missing");
		return;
	}
	t_pass("downtime data hook exists");

	init_ds(ds);
	ds.type = NEBTYPE_DOWNTIME_START;
	ds.comment_data = "nebtest downtime";

	ok_int(hook(cb, &ds), 0, "host downtime insertion");
	set_service(ds);
	ok_int(hook(cb, &ds), 0, "service downtime insertion");
}


static void test_sql_inserts(void)
{
	t_start("Testing hooks");
	ok_int(test_sql_host_insert(), 0, "host check hook");
	ok_int(test_sql_service_insert(), 0, "service check hook");
	test_sql_downtime_insert();
	ok_int(test_sql_process_data_insert(), 0, "process data hook");
	t_end();
}

static nebmodule *neb;
static void test_one_module(char *arg, int test_sql)
{
	static void *dso = NULL;
	char *path;
	int (*init_func)(int, const char *, nebmodule *);
	int (*deinit_func)(int, int);
	int (*log_grok_var)(const char *, const char *);

	if (dso)
		dlclose(dso);

	if (strchr(arg, '/'))
		path = arg;
	else {
		int len = strlen(arg);
		path = calloc(len + 3, 1);
		path[0] = '.';
		path[1] = '/';
		memcpy(path + 2, arg, len);
	}

	dso = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	t_start("Testing module '%s'", path);
	if (!dso) {
		t_fail("dlopen(%s) failed: %s", path, dlerror());
		return;
	}

	t_pass("dlopen(%s) worked out fine", path);
	log_grok_var = dlsym(dso, "log_grok_var");
	if (log_grok_var) {
		log_grok_var("log_file", "/dev/null");
	}
	t_start("Checking symbols in '%s'", path);
	check_symbols(dso);
	t_end();

	if (test_sql) {
		t_start("Testing sql inserts");
		init_func = dlsym(dso, "nebmodule_init");
		ok_int(init_func(-1, neb->args, neb), 0, "module init function");

		test_sql_inserts();

		deinit_func = dlsym(dso, "nebmodule_deinit");
		ok_int(deinit_func(0, 0), 0, "module deinit function");
		ok_int(reg_errors, 0, "registering callbacks");
		ok_int(dereg_errors, 0, "deregistering callbacks");
		check_callbacks();
		t_end();
	}

	return;
}

int main(int argc, char **argv)
{
	int i, test_sql = 0;

	t_set_colors(0);
	t_verbose = 1;
	neb = calloc(sizeof(*neb), 1);

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-' && arg[1] == 'f' && i < argc - 1) {
			neb->args = argv[++i];
			continue;
		}
		if (!strcmp(arg, "--test-sql")) {
			test_sql = 1;
			continue;
		}

		test_one_module(arg, test_sql);
	}

	return t_end();
}
