/*
 * This file contains tests for the "blockify()/deblockify()"
 * function, ensuring we don't garble data before we send it off
 */
#define NSCORE
#include <dlfcn.h>
#include "nagios/nebstructs.h"
#include "nagios/nebmodules.h"
#include "nagios/nebmods.h"
#include "nagios/broker.h"
#include "nagios/macros.h"
#include "nagios/statusdata.h"
#include "nagios/objects.h"
#include "nagios/nagios.h"
#include "shared.h"
#include "hookinfo.h"
#include "sql.h"
#include "daemon.h"
#include "test_utils.h"
#include "module.h"

#define HOST_NAME "webex"
#define SERVICE_DESCRIPTION "http"
#define OUTPUT "The plugin output"
#define PERF_DATA "random_value='5;5;5'"
#define CONTACT_NAME "ae"
#define AUTHOR_NAME "Pelle plutt"
#define COMMENT_DATA "comment data"
#define COMMAND_NAME "notify-by-squirting"
#define COMMAND_STRING "SCHEDULE_FORCED_HOST_SVC_CHECKS"
#define COMMAND_ARGS "w2k3std.op5.se;1193930749"

#define test_compare(str) ok_str(mod->str, orig->str, #str)

#define CP() printf("ALIVE @ %s->%s:%d\n", __FILE__, __func__, __LINE__)
#define zzz() usleep(150000)

static host *hosts;
static uint num_hosts;
static uint num_services;
static service *services;
static merlin_event pkt;

static nebmodule *neb;
static char *cache_file = "/opt/monitor/var/objects.cache";
static char *status_log = "/opt/monitor/var/status.log";
static int (*hooks[NEBCALLBACK_NUMITEMS])(int, void *);
static int callback_is_tested(int id, const char *caller);

static int (*post_config_init)(int, void *);

/* variables provided by Nagios and required by module */
char *config_file = "/opt/monitor/etc/nagios.cfg";
service *service_list = NULL;
hostgroup *hostgroup_list = NULL;
host *host_list = NULL;
char *macro_x[MACRO_X_COUNT];
int event_broker_options = 0;
int daemon_dumps_core = 0;

/* nagios functions we must have for dlopen() to work properly */
int schedule_new_event(int a, int b, time_t c, int d, unsigned long e,
					   void *f, int g, void *h, void *i, int j)
{
	return 0;
}
host *find_host(char *host_name)
{
	return 0;
}
service *find_service(char *host_name, char *service_description)
{
	return NULL;
}
int update_all_status_data(void)
{
	return 0;
}

int neb_register_callback(int callback_type, void *mod_handle,
						  int priority, int (*callback_func)(int,void *))
{
	if (callback_type == NEBCALLBACK_PROCESS_DATA) {
		post_config_init = callback_func;
	}

	callback_is_tested(callback_type, __func__);
	if (!callback_func)
		t_fail("%s registered with NULL callback_func",
			   callback_name(callback_type));
	hooks[callback_type] = callback_func;

	return 0;
}

int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *))
{
	if (!hooks[callback_type]) {
		t_fail("%s unregistered from NULL",
			   callback_name(callback_type));
	}

	hooks[callback_type] = NULL;

	return 0;
}

/* various testing helpers */
static void *blk_prep(void *data)
{
	blockify_event(&pkt, data);
	deblockify_event(&pkt);
	return pkt.body;
}

static uint count_rows(dbi_result result)
{
	if (result) {
		return dbi_result_get_numrows(result);
	}
	return 0;
}

static uint count_table_rows(const char *table_name)
{
	sql_query("SELECT * FROM %s", table_name);
	return count_rows(sql_get_result());
}

static void verify_count(const char *name, uint expected, const char *fmt, ...)
{
	uint count;
	va_list ap;
	va_start(ap, fmt);
	sql_vquery(fmt, ap);
	va_end(ap);
	count = count_rows(sql_get_result());
	ok_uint(count, expected, name);
}

static void load_hosts_and_services(void)
{
	int i = 0;
	dbi_result result;

	num_hosts = count_table_rows("host");
	num_services = count_table_rows("service");
	if (!num_hosts || !num_services) {
		crash("hosts: %u; services: %u;\nCan't run tests with a db like that\n",
			  num_hosts, num_services);
	}

	hosts = calloc(num_hosts, sizeof(*hosts));
	sql_query("SELECT host_name FROM host");
	result = sql_get_result();
	ok_uint(num_hosts, dbi_result_get_numrows(result), "libdbi host count");
	i = 0;
	while (i < num_hosts && dbi_result_next_row(result)) {
		hosts[i].name = dbi_result_get_string_copy_idx(result, 1);
		hosts[i].plugin_output = OUTPUT;
		hosts[i].perf_data = PERF_DATA;
		hosts[i].long_plugin_output = NULL;
		i++;
	}
	ok_uint(i, num_hosts, "number of hosts loaded");

	services = calloc(num_services, sizeof(*services));
	sql_query("SELECT host_name, service_description FROM service");
	result = sql_get_result();
	ok_uint(num_services, dbi_result_get_numrows(result), "libdbi service count");
	i = 0;
	while (i < num_services && dbi_result_next_row(result)) {
		services[i].host_name = dbi_result_get_string_copy_idx(result, 1);
		services[i].description = dbi_result_get_string_copy_idx(result, 2);
		i++;
	}
	ok_uint(i, num_services, "number of services loaded");
}

static void t_setup(void)
{
	sql_init();
	printf("Using database '%s'\n", sql_db_name());
	printf("%u comments\n", count_table_rows("comment"));
	printf("%u hosts\n", count_table_rows("host"));
	printf("%u services\n", count_table_rows("service"));

	macro_x[MACRO_OBJECTCACHEFILE] = cache_file;
	macro_x[MACRO_STATUSDATAFILE] = status_log;
	if (ipc_init() < 0) {
		t_fail("ipc_init()");
		crash("Failed to initialize ipc socket");
	} else {
		t_pass("ipc_init()");
	}
	ok_int(send_paths(), 0, "Sending paths");
	sleep(4);
	load_hosts_and_services();
}

#define GLOBAL_HOST_EVENT_HANDLER "/path/to/global-host-event-handler"
#define GLOBAL_SVC_EVENT_HANDLER "/path/to/global-svc-event-handler"
static void test_program_status(void)
{
	nebstruct_program_status_data *orig, *mod;

	orig = calloc(1, sizeof(*orig));
	orig->global_host_event_handler = GLOBAL_HOST_EVENT_HANDLER;
	orig->global_service_event_handler = GLOBAL_SVC_EVENT_HANDLER;
	mod = blk_prep(orig);
	test_compare(global_host_event_handler);
	test_compare(global_service_event_handler);

	/* both set */
	merlin_mod_hook(pkt.hdr.type, orig);

	/* only service event handler set */
	orig->global_host_event_handler = NULL;
	merlin_mod_hook(pkt.hdr.type, orig);

	/* neither set */
	orig->global_service_event_handler = NULL;
	merlin_mod_hook(pkt.hdr.type, orig);

	/* only host event handler set */
	orig->global_host_event_handler = GLOBAL_HOST_EVENT_HANDLER;
	merlin_mod_hook(pkt.hdr.type, orig);
	free(orig);
}

static void test_external_command(void)
{
	nebstruct_external_command_data *orig, *mod;

	orig = calloc(1, sizeof(*orig));
	orig->command_string = COMMAND_STRING;
	orig->command_args = COMMAND_ARGS;
	mod = blk_prep(orig);
	test_compare(command_string);
	test_compare(command_args);
	merlin_mod_hook(pkt.hdr.type, orig);
	free(orig);
}

static void test_flapping(void)
{
	nebstruct_flapping_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	orig->percent_change = 78.5;
	orig->high_threshold = 53.9;
	orig->low_threshold = 20.1;
	orig->comment_id = 1;

	orig->flapping_type = HOST_FLAPPING;
	orig->type = NEBTYPE_FLAPPING_START;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("set hosts to flapping", num_hosts,
				 "SELECT * FROM host WHERE is_flapping = 1");

	orig->type = NEBTYPE_FLAPPING_STOP;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("set hosts to not flapping", 0,
				 "SELECT * FROM host WHERE is_flapping = 1");


	orig->flapping_type = SERVICE_FLAPPING;
	orig->type = NEBTYPE_FLAPPING_START;
	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("set services to flapping", num_services,
				 "SELECT * FROM service WHERE is_flapping = 1");

	orig->type = NEBTYPE_FLAPPING_STOP;
	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("set services to not flapping", 0,
				 "SELECT * FROM service WHERE is_flapping = 1");

	free(orig);
}

static void test_host_check(void)
{
	nebstruct_host_check_data *orig, *mod;
	int i;

	return;
	orig = calloc(1, sizeof(*orig));

	/*
	 * run the tests for all hosts
	 * first we set an arbitrary state, and then we check how many
	 * rows have the state we've set
	 */
	sql_query("UPDATE host SET current_state = 155");
	orig->type = NEBTYPE_HOSTCHECK_PROCESSED;
	for (i = 0; i < num_hosts; i++) {
		host *h = &hosts[i];

		orig->host_name = h->name;
		orig->output = h->plugin_output;
		orig->perf_data = h->perf_data;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(output);
		test_compare(perf_data);
		merlin_mod_hook(NEBCALLBACK_HOST_STATUS_DATA, orig);
	}
	free(orig);
}

static void test_service_check(void)
{
	nebstruct_service_check_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	gettimeofday(&orig->start_time, NULL);
	gettimeofday(&orig->end_time, NULL);

	mod->type = NEBTYPE_SERVICECHECK_PROCESSED;
	for (i = 0; i < num_services; i++) {
		service *s = &services[i];
		orig->host_name = s->host_name;
		orig->service_description = s->description;
		orig->output = s->plugin_output;
		orig->perf_data = s->perf_data;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(output);
		test_compare(perf_data);
		test_compare(service_description);
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, orig);
	}
	free(orig);
}

static void test_host_status(void)
{
	merlin_host_status *orig, *mod;
	nebstruct_host_status_data *ds;
	int i;

	orig = calloc(1, sizeof(*orig));
	ds = calloc(1, sizeof(*ds));

	/*
	 * run the tests for all hosts
	 * first we set an arbitrary state, and then we check how many
	 * rows have the state we've set
	 */
	for (i = 0; i < num_hosts; i++) {
		host *h = &hosts[i];

		orig->name = h->name;
		orig->state.plugin_output = h->plugin_output;
		orig->state.perf_data = h->perf_data;
		orig->state.long_plugin_output = h->long_plugin_output;
		mod = blk_prep(orig);
		ok_str(mod->name, h->name, "host name transfers properly");
		ok_str(mod->state.plugin_output, h->plugin_output, "host output transfers properly");
		ok_str(mod->state.perf_data, h->perf_data, "performance data transfers properly");

		h->current_state = 4;
		ds->object_ptr = h;
		merlin_mod_hook(NEBCALLBACK_HOST_STATUS_DATA, ds);
	}
	zzz();
	verify_count("host status updates db properly", num_hosts,
				 "SELECT * FROM host WHERE current_state = 4");
	free(ds);
	free(orig);
}

static void test_service_status(void)
{
	merlin_service_status *orig, *mod;
	nebstruct_service_status_data *ds;
	int i;

	orig = calloc(1, sizeof(*orig));
	ds = calloc(1, sizeof(*ds));
	for (i = 0; i < num_services; i++) {
		service *s = &services[i];

		orig->host_name = s->host_name;
		orig->service_description = s->description;
		orig->state.plugin_output = s->plugin_output;
		orig->state.perf_data = s->perf_data;
		orig->state.long_plugin_output = s->long_plugin_output;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		ok_str(orig->state.plugin_output, mod->state.plugin_output, "plugin_output must match");
		ok_str(orig->state.perf_data, mod->state.perf_data, "perf_data must match");
		ok_str(orig->state.long_plugin_output, mod->state.long_plugin_output, "long plugin output must match");

		ds->object_ptr = s;
		s->current_state = 15;
		merlin_mod_hook(NEBCALLBACK_SERVICE_STATUS_DATA, ds);
	}
	zzz();
	verify_count("service status updates db properly", num_services,
				 "SELECT * FROM service WHERE current_state = 15");

	free(ds);
	free(orig);
}

static void test_contact_notification_method(void)
{
	nebstruct_contact_notification_method_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	orig->output = OUTPUT;
	orig->contact_name = CONTACT_NAME;
	orig->reason_type = 1;
	orig->state = 0;
	orig->escalated = 0;
	orig->ack_author = AUTHOR_NAME;
	orig->ack_data = COMMENT_DATA;
	orig->command_name = COMMAND_NAME;
	gettimeofday(&orig->start_time, NULL);
	gettimeofday(&orig->end_time, NULL);
	orig->type = NEBTYPE_CONTACTNOTIFICATIONMETHOD_END;

	sql_query("TRUNCATE notification");
	/* test setting all hosts to flapping state */
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		test_compare(output);
		test_compare(contact_name);
		test_compare(ack_author);
		test_compare(ack_data);
		test_compare(command_name);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("Adding host notifications", num_hosts,
				 "SELECT * FROM notification");

	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].host_name;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		test_compare(output);
		test_compare(contact_name);
		test_compare(ack_author);
		test_compare(ack_data);
		test_compare(command_name);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("Adding service notifications", num_hosts + num_services,
				 "SELECT * FROM notification");

	free(orig);
}

static void test_comment(void)
{
	nebstruct_comment_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	orig->author_name = AUTHOR_NAME;
	orig->comment_data = COMMENT_DATA;
	orig->entry_time = time(NULL);
	orig->expires = 1;
	orig->expire_time = time(NULL) + 300;

	/* set up for testing comments for all hosts and services */
	sql_query("TRUNCATE comment");

	/* test adding comments for all hosts */
	orig->type = NEBTYPE_COMMENT_ADD;
	for (i = 0; i < num_hosts; i++) {
		host *h = &hosts[i];

		orig->host_name = h->name;
		orig->comment_id = i + 1;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(author_name);
		test_compare(comment_data);
		test_compare(service_description);
		merlin_mod_hook(NEBCALLBACK_COMMENT_DATA, orig);
	}
	zzz();
	verify_count("adding host comments", num_hosts, "SELECT * FROM comment");

	/* test deleting host comments */
	orig->type = NEBTYPE_COMMENT_DELETE;
	for (i = 0; i < num_hosts; i++) {
		host *h = &hosts[i];

		orig->host_name = h->name;
		orig->comment_id = i + 1;
		merlin_mod_hook(NEBCALLBACK_COMMENT_DATA, orig);
	}
	zzz();
	verify_count("removing host comments", 0, "SELECT * FROM comment");

	/* test adding comments for all services */
	orig->type = NEBTYPE_COMMENT_ADD;
	for (i = 0; i < num_services; i++) {
		service *s = &services[i];

		orig->host_name = s->host_name;
		orig->service_description = s->description;
		orig->comment_id = i + 1;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(author_name);
		test_compare(comment_data);
		test_compare(service_description);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("adding service comments", num_services, "SELECT * FROM comment");

	/* test removing comments for all services */
	orig->type = NEBTYPE_COMMENT_DELETE;
	for (i = 0; i < num_services; i++) {
		service *s = &services[i];

		orig->host_name = s->host_name;
		orig->service_description = s->description;
		orig->comment_id = i + 1;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("removing service comments", 0, "SELECT * FROM comment");

	free(orig);
}

static void test_downtime(void)
{
	nebstruct_downtime_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	orig->author_name = AUTHOR_NAME;
	orig->comment_data = COMMENT_DATA;

	sql_query("TRUNCATE scheduled_downtime");
	/* test adding downtime for all hosts */
	orig->type = NEBTYPE_DOWNTIME_ADD;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		orig->downtime_id = i + 1;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		test_compare(author_name);
		test_compare(comment_data);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("adding host downtimes", num_hosts,
				 "SELECT * FROM scheduled_downtime");

	/* test removing all the added downtime */
	orig->type = NEBTYPE_DOWNTIME_DELETE;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		orig->downtime_id = i + 1;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("removing host downtimes", 0,
				 "SELECT * FROM scheduled_downtime");

	orig->type = NEBTYPE_DOWNTIME_ADD;
	for (i = 0; i < num_services; i++) {
		orig->downtime_id = i + 1;
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
		mod = blk_prep(orig);
		test_compare(host_name);
		test_compare(service_description);
		test_compare(author_name);
		test_compare(comment_data);
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("adding service downtimes", num_services,
				 "SELECT * FROM scheduled_downtime");

	orig->type = NEBTYPE_DOWNTIME_DELETE;
	for (i = 0; i < num_services; i++) {
		orig->downtime_id = i + 1;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("deleting service downtimes", 0,
				 "SELECT * FROM scheduled_downtime");

	/* set up for testing starting and stopping of host/service downtimes */
	sql_query("UPDATE host SET scheduled_downtime_depth = 0");
	sql_query("UPDATE service SET scheduled_downtime_depth = 0");
	/* test starting host downtimes */
	orig->type = NEBTYPE_DOWNTIME_START;
	orig->service_description = NULL;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("starting host downtimes", num_hosts,
				 "SELECT * FROM host WHERE scheduled_downtime_depth > 0");

	/* test starting host downtimes */
	orig->type = NEBTYPE_DOWNTIME_STOP;
	for (i = 0; i < num_hosts; i++) {
		orig->host_name = hosts[i].name;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz();
	verify_count("stopping host downtimes", num_hosts,
				 "SELECT * FROM host WHERE scheduled_downtime_depth = 0");

	/* test starting service downtime */
	orig->type = NEBTYPE_DOWNTIME_START;
	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz(); verify_count("starting service downtimes", num_services,
						"SELECT * FROM service WHERE scheduled_downtime_depth > 0");

	/* test stopping service downtime */
	orig->type = NEBTYPE_DOWNTIME_STOP;
	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
		merlin_mod_hook(pkt.hdr.type, orig);
	}
	zzz(); verify_count("stopping service downtimes", num_services,
						"SELECT * FROM service WHERE scheduled_downtime_depth = 0");

	free(orig);
}

static void grok_cfg_compound(struct cfg_comp *config, int level)
{
	int i;

	for (i = 0; i < config->vars; i++) {
		struct cfg_var *v = config->vlist[i];

		if (level == 1 && prefixcmp(config->name, "test"))
			break;
		if (!prefixcmp(config->name, "test")) {
			if (!strcmp(v->key, "objects.cache")) {
				cache_file = strdup(v->value);
				continue;
			}
			if (!strcmp(v->key, "status.log") || !strcmp(v->key, "status.sav")) {
				status_log = strdup(v->value);
				continue;
			}
			if (!strcmp(v->key, "nagios.cfg")) {
				config_file = strdup(v->value);
				continue;
			}
		}

		if (!level && grok_common_var(config, v))
			continue;
		if (level == 2 && !prefixcmp(config->name, "database")) {
			sql_config(v->key, v->value);
			continue;
		}
		printf("'%s' = '%s' is not grok'ed as a common variable\n", v->key, v->value);
	}

	for (i = 0; i < config->nested; i++) {
		grok_cfg_compound(config->nest[i], level + 1);
	}
}

static void grok_config(char *path)
{
	struct cfg_comp *config;

	config = cfg_parse_file(path);
	if (!config)
		crash("Failed to parse config from '%s'\n", path);

	grok_cfg_compound(config, 0);
	cfg_destroy_compound(config);
}

static void check_callbacks(void)
{
	int i;

	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		if (hooks[i]) {
			t_fail("callback %s not unregistered by module unload",
				   callback_name(i));
		}
	}
}

static int check_symbols(void *dso)
{
	int i, ret = 0;
	const char *syms[] = {
		"__neb_api_version", "nebmodule_init", "nebmodule_deinit", NULL,
	};

	for (i = 0; syms[i]; i++) {
		const char *sym = syms[i];
		if (dlsym(dso, sym))
			t_pass("symbol %s exists", sym);
		else {
			t_fail("symbol %s is missing", sym);
			ret = -1;
		}
	}
	return ret;
}

static int test_one_module(char *arg)
{
	static void *dso = NULL;
	char *path;
	int result = 0, retval;
	static int (*init_func)(int, const char *, nebmodule *);
	static int (*deinit_func)(int, int);

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
	if (!dso) {
		printf("dlopen() failed: %s\n", dlerror());
		return -1;
	}
	check_symbols(dso);

	init_func = dlsym(dso, "nebmodule_init");
	retval = init_func(-1, neb->args, neb);
	if (retval) {
		printf("Failed to run init function from %s\n", arg);
		return -1;
	}

	deinit_func = dlsym(dso, "nebmodule_deinit");
	result |= deinit_func(0, 0);
	check_callbacks();
	return result;
}

#define T_ENTRY(cb, func) \
	{ NEBCALLBACK_##cb##_DATA, #cb, "test_"#func, test_##func }
static struct merlin_test {
	int callback;
	const char *cb_name, *funcname;
	void (*test)(void);
} mtest[] = {
	T_ENTRY(PROGRAM_STATUS, program_status),
	T_ENTRY(EXTERNAL_COMMAND, external_command),
	T_ENTRY(CONTACT_NOTIFICATION_METHOD, contact_notification_method),
	T_ENTRY(HOST_STATUS, host_status),
	T_ENTRY(SERVICE_STATUS, service_status),
	T_ENTRY(COMMENT, comment),
	T_ENTRY(FLAPPING, flapping),
	T_ENTRY(DOWNTIME, downtime),
};

static int callback_is_tested(int id, const char *caller)
{
	int i;
	if (id == NEBCALLBACK_PROCESS_DATA)
		return 1;

	for (i = 0; i < ARRAY_SIZE(mtest); i++) {
		if (mtest[i].callback == id) {
			t_pass("%s: callback %s is tested", caller, callback_name(id));
			return 1;
		}
	}

	t_fail("%s: Callback %s is not tested", caller, callback_name(id));
	return 0;
}

int nebmodule_init(int, char *, void *);
int nebmodule_deinit(int, int);
int main(int argc, char **argv)
{
	int i;

	use_database = 1;

	t_set_colors(0);

	log_grok_var("log_file", "stdout");
	log_grok_var("log_level", "warn");
	if (argc < 2) {
		crash("No arguments. Wtf??");
	}
	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];
		int eq_opt = 0;

		if ((opt = strchr(arg, '='))) {
			*opt++ = '\0';
			eq_opt = 1;
		} else if (i < argc - 1) {
			opt = argv[i + 1];
		}

		if (!strcmp(arg, "--module") || !strcmp(arg, "-m")) {
			return test_one_module(opt);
		}
		if (!strcmp(arg, "--verbose") || !strcmp(arg, "-v")) {
			t_verbose++;
			continue;
		}
		grok_config(arg);
	}

	t_setup();

	nebmodule_init(-1, NULL, NULL);
	if (post_config_init) {
		nebstruct_process_data ds;
		ds.type = NEBTYPE_PROCESS_EVENTLOOPSTART;
		post_config_init(NEBCALLBACK_PROCESS_DATA, &ds);
	}

	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		struct hook_info_struct *hi = &hook_info[i];

		ok_int(hi->cb_type, i, "hook is properly ordered");
	}

	/* shut valgrind up */
	memset(&pkt, 0, sizeof(pkt));

	for (i = 0; i < ARRAY_SIZE(mtest); i++) {
		struct merlin_test *t = &mtest[i];

		pkt.hdr.type = t->callback;

		zzz();
		printf("Running %s to test %s\n", t->funcname, t->cb_name);

		t->test();
	}

	nebmodule_deinit(-1, -1);
	return t_end();
}
