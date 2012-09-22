/*
 * This file contains tests for the merlin_{encode,decode}()"
 * functions, ensuring we don't garble data before we send it off
 */
#define _GNU_SOURCE 1
#define NSCORE
#include <dlfcn.h>
#include <nagios/nebstructs.h>
#include <nagios/nebmodules.h>
#include <nagios/nebmods.h>
#include <nagios/broker.h>
#include <nagios/macros.h>
#include <nagios/statusdata.h>
#include <nagios/objects.h>
#include <nagios/nagios.h>
#include "shared.h"
#include "hookinfo.h"
#include "sql.h"
#include "daemon.h"
#include "test_utils.h"
#include "module.h"
#include "nagios-stubs.h"

#define HOST_NAME "webex"
#define SERVICE_DESCRIPTION "http"
#define OUTPUT "The plugin output"
#define LONG_PLUGIN_OUTPUT "some\nrandom\ndata"
#define PERF_DATA "random_value='5;5;5'"
#define CONTACT_NAME "ae"
#define AUTHOR_NAME "Pelle plutt"
#define COMMENT_DATA "comment data"
#define COMMAND_NAME "notify-by-squirting"
#define COMMAND_STRING "SCHEDULE_FORCED_HOST_SVC_CHECKS"
#define COMMAND_ARGS "w2k3std.op5.se;1193930749"

#define test_compare(str) ok_str(mod->str, orig->str, #str)

#define CP() printf("ALIVE @ %s->%s:%d\n", __FILE__, __func__, __LINE__)

/**
   Sleeps for a short amount of time (under 1 second).
   If the environment variable MTEST_SLOW is set
   and its valu does not start with the digit 0 then
   a longer sleep period is used (required for testing
   on my VM setup, where mysql cannot write/flush fast
   enough for the default setup).

   e.g. to run in slow mode:

   MTEST_SLOW=1 ./mtest configfile.cfg

   If you use any value other than '1' then execution
   will pause() until a signal is received. e.g. the user
   must tap Ctrl-C to continue (this is non-intuitive, but
   gives me a chance to confirm db-side changes).
*/
static void zzz()
{
	static int slow = -2;
	static char const * env = NULL;

	if (slow == -2) {
		env = getenv("MTEST_SLOW");
		if (!env) {
			slow = 0;
		} else if (*env == 'p' || *env == '0' || *env == 'P')
			slow = -1;
		else {
			slow = atoi(env);
		}
	}

	if (slow == -1 && isatty(fileno(stdin))) {
		char lbuf[4096];
		puts("!!! PRESS ENTER TO CONTINUE !!!");
		fgets(lbuf, sizeof(lbuf) - 1, stdin);
	} else if (slow > 0) {
		sleep(slow);
	} else {
		usleep(999999);
	}
}

static host *hosts;
static service *services;
static merlin_event pkt;

static nebmodule *neb;
static char *cache_file = "/opt/monitor/var/objects.cache";
static char *status_log = "/opt/monitor/var/status.log";
static int (*hooks[NEBCALLBACK_NUMITEMS])(int, void *);
static int callback_is_tested(int id, const char *caller);

static int (*post_config_init)(int, void *);

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
	pkt.hdr.len = merlin_encode_event(&pkt, data);
	merlin_decode_event(&pkt);
	return pkt.body;
}

/**
   Counts the number of rows in the given result. ACHTUNG: this is destructive:
   it must traverse the result in order to count the results (thanks to OCILIB).
 */
static uint count_rows_traverse(db_wrap_result * result)
{
	unsigned int n = 0;
	if (result) {
	    for( ; 0 == result->api->step(result); ++n ) {}
	    /* ACHTUNG: the oci driver can only return the number of rows
		 PROCESSED SO FAR, which is really stupid because we already
		 know how many rows we've processed so far.
	     */
	}

	return n;
}

/**
   Returns the result of result->api->num_rows(), but note that value
   may behave differently depending on the driver! OCI returns only
   the number of rows traversed so far, whereas DBI returns the total
   number of rows from e.g. a SELECT statement.
 */
static uint count_rows(db_wrap_result * result)
{
	size_t n = 0;

	if (result) {
		result->api->num_rows(result, &n);
	}

	return n;
}


static uint count_table_rows(const char *table_name)
{
	sql_query("SELECT 'x' FROM %s", /*reminder: we don't use '*'
				      here, to avoid fetching arbitrarily
				      large data.*/ table_name);
	return count_rows_traverse(sql_get_result());
}

static void verify_count(const char *name, uint expected, const char *fmt, ...)
{
	uint count;
	va_list ap;
	va_start(ap, fmt);
	sql_vquery(fmt, ap);
	va_end(ap);
	count = count_rows_traverse(sql_get_result());
	ok_uint(count, expected, name);
}

static void load_hosts_and_services(void)
{
	int i = 0;
	db_wrap_result * result = NULL;

	num_hosts = count_table_rows("host");
	num_services = count_table_rows("service");
	if (!num_hosts || !num_services) {
		crash("hosts: %u; services: %u;\nCan't run tests with a db like that\n",
			  num_hosts, num_services);
	}

	hosts = calloc(num_hosts, sizeof(*hosts));
	sql_query("SELECT host_name FROM host");
	result = sql_get_result();
	assert(NULL != result);
	ok_uint(num_hosts, count_rows(result), "db_wrap host count");
	i = 0;
	while (i < num_hosts && (0 == result->api->step(result))) {
		hosts[i].name = NULL;
		db_wrap_result_string_copy_ndx(result, 0, & hosts[i].name, NULL);
		hosts[i].plugin_output = OUTPUT;
		hosts[i].perf_data = PERF_DATA;
		hosts[i].long_plugin_output = LONG_PLUGIN_OUTPUT;
		i++;
	}
	ok_uint(i, num_hosts, "number of hosts loaded");
	ok_uint(num_hosts, count_rows(result), "db_wrap host count");

	services = calloc(num_services, sizeof(*services));
	sql_query("SELECT host_name, service_description FROM service");
	result = sql_get_result();

	assert(NULL != result);
	ok_uint(num_services, count_rows(result), "db_wrap service count");
	i = 0;
	while (i < num_services && (0 == result->api->step(result))) {
		services[i].host_name = NULL;
		db_wrap_result_string_copy_ndx(result, 0, &services[i].host_name, NULL);
		services[i].description = NULL;
		db_wrap_result_string_copy_ndx(result, 1, &services[i].description, NULL);
		services[i].plugin_output = OUTPUT;
		services[i].perf_data = PERF_DATA;
		services[i].long_plugin_output = LONG_PLUGIN_OUTPUT;
		i++;
	}
	ok_uint(num_services, count_rows(result), "db_wrap service count");
	ok_uint(i, num_services, "number of services loaded");
}

static void t_setup(void)
{
	printf("Using database '%s'\n", sql_db_name());
	printf("%u comments\n", count_table_rows("comment_tbl"));
	printf("%u hosts\n", count_table_rows("host"));
	printf("%u services\n", count_table_rows("service"));

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
	sql_query("UPDATE host SET current_state = 155");
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
	free(ds);
	free(orig);
	verify_count("host status updates db properly", num_hosts,
			"SELECT 'x' FROM host WHERE current_state = 4");
}

static void test_service_status(void)
{
	merlin_service_status *orig, *mod;
	nebstruct_service_status_data *ds;
	int i;

	orig = calloc(1, sizeof(*orig));
	ds = calloc(1, sizeof(*ds));

	/*
	 * run the tests for all services
	 * first we set an arbitrary state, and then we check how many
	 * rows have the state we've set
	 */
	sql_query("UPDATE service SET current_state = 155");
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

		s->current_state = 15;
		ds->object_ptr = s;
		merlin_mod_hook(NEBCALLBACK_SERVICE_STATUS_DATA, ds);
	}
	zzz();
	verify_count("service status updates db properly", num_services,
				 "SELECT * FROM service WHERE current_state = 15");

	free(ds);
	free(orig);
}

static void test_host_check(void)
{
	merlin_host_status *orig, *mod;
	nebstruct_host_check_data *ds;
	int i;

	orig = calloc(1, sizeof(*orig));
	ds = calloc(1, sizeof(*ds));
	ds->type = NEBTYPE_HOSTCHECK_PROCESSED;

	/*
	 * run the tests for all hosts
	 * first we set an arbitrary state, and then we check how many
	 * rows have the state we've set
	 */
	sql_query("UPDATE host SET current_state = 155");
	if (host_perf_table)
		sql_query("TRUNCATE TABLE %s", host_perf_table);
	sql_query("TRUNCATE TABLE report_data");
	zzz();
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
		h->state_type = SOFT_STATE;

		/*
		 * We send this a bunch of times. Once to reset the stored
		 * data in the daemon so report-data gets updated, and then
		 * twice with the same state so we know the report_data
		 * table gets updated as it should and no more.
		 */
		h->current_state = 19;
		/* should update perf_data and report_data */
		ds->object_ptr = h;
		merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, ds);

		/* should update perf_data and report_data */
		h->current_state = 1;
		merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, ds);

		/* shouldn't update anything (removed as 'dupe') */
		merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, ds);

		/* should update perf_data but not report_data */
		h->plugin_output = "lalala";
		merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, ds);

		/* should update perf_data and report_data */
		h->state_type = HARD_STATE;
		merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, ds);
	}
	zzz();
	verify_count("host check status insertion", num_hosts,
				 "SELECT * FROM host WHERE current_state = 1");
	if (host_perf_table) {
		char *query;
		asprintf(&query, "select * from %s", host_perf_table);
		verify_count("host check perf_data insertion",
					 num_hosts * 4, query);
		free(query);
	}
	verify_count("host check report_data insertion", num_hosts * 3,
				 "SELECT * FROM report_data WHERE event_type = 801");
	free(ds);
	free(orig);
}

#define BROKEN_BUFSIZE (MERLIN_IOC_BUFSIZE * 2)
static void test_broken_packets(void)
{
	int i, seed;
	nebstruct_service_check_data *ds;
	uint disconnects = 0;

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.sig.id = MERLIN_SIGNATURE;
	ds = calloc(1, sizeof(*ds));
	ds->type = NEBTYPE_SERVICECHECK_PROCESSED;
	pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
	pkt.hdr.selection = DEST_BROADCAST;

	for (seed = 0; seed < 1; seed += 8237) {
		srand(seed);
		for (i = 0; i < num_services; i++) {
			int real_len, sent;
			merlin_service_status st_obj;

			pkt.hdr.len = HDR_SIZE + (rand() & (MAX_PKT_SIZE - 1));
			MOD2NET_STATE_VARS(st_obj.state, ((service *)&services[i]));
			real_len = merlin_encode_event(&pkt, (void *)&st_obj);
			pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
			sent = node_send(&ipc, &pkt, pkt.hdr.len, 0);
			if (real_len != sent && ipc.sock < 0) {
				disconnects++;
			}

			if (ok_int(ipc_is_connected(0), 1, "daemon's borked-packet handling") != TEST_PASS) {
				break;
			}
		}
	}
	fprintf(stderr, "####### Broken packet tests complete. %u disconnects of %u sent\n",
		   disconnects, num_services);

	free(ds);
}

static void test_service_check(void)
{
	merlin_service_status *orig, *mod;
	nebstruct_service_check_data *ds;
	int i;

	orig = calloc(1, sizeof(*orig));
	ds = calloc(1, sizeof(*ds));
	ds->type = NEBTYPE_SERVICECHECK_PROCESSED;

	/*
	 * run the tests for all services
	 * first we set an arbitrary state, and then we check how many
	 * rows have the state we've set
	 */
	sql_query("UPDATE service SET current_state = 155");
	if (service_perf_table)
		sql_query("TRUNCATE TABLE %s", service_perf_table);
	sql_query("TRUNCATE TABLE report_data");
	zzz();
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
		s->state_type = SOFT_STATE;

		/*
		 * We send this a bunch of times. Once to reset the stored
		 * data in the daemon so report-data gets updated, and then
		 * twice with the same state so we know the report_data
		 * table gets updated as it should and no more.
		 */
		s->current_state = 19;
		/* should update perf_data and report_data */
		ds->object_ptr = s;
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, ds);

		/* should update perf_data and report_data */
		s->current_state = 1;
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, ds);

		/* should update neither perf_data nor report_data */
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, ds);

		/* should update perf_data but not report_data */
		s->plugin_output = "lalala";
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, ds);

		/* should update perf_data and report_data */
		s->state_type = HARD_STATE;
		merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, ds);
	}
	zzz();
	verify_count("service check status updates", num_services,
				 "SELECT * FROM service WHERE current_state = 1");
	if (service_perf_table) {
		char *query;
		asprintf(&query, "SELECT * FROM %s", service_perf_table);
		verify_count("service check perfdata insertions",
					 num_services * 4, query);
	}
	verify_count("service check report_data insertions", num_services * 3,
				 "SELECT * FROM report_data WHERE event_type = 701");

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

	sql_query("TRUNCATE TABLE notification");
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
				 "SELECT 'x' FROM notification");

	for (i = 0; i < num_services; i++) {
		orig->host_name = services[i].host_name;
		orig->service_description = services[i].description;
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
	sql_query("TRUNCATE TABLE comment_tbl");

	/* test adding comments for all hosts */
	orig->type = NEBTYPE_COMMENT_LOAD;
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
	verify_count("adding host comments", num_hosts, "SELECT * FROM comment_tbl");

	/* test deleting host comments */
	orig->type = NEBTYPE_COMMENT_DELETE;
	for (i = 0; i < num_hosts; i++) {
		host *h = &hosts[i];

		orig->host_name = h->name;
		orig->comment_id = i + 1;
		merlin_mod_hook(NEBCALLBACK_COMMENT_DATA, orig);
	}
	zzz();
	verify_count("removing host comments", 0, "SELECT * FROM comment_tbl");

	/* test adding comments for all services */
	orig->type = NEBTYPE_COMMENT_LOAD;
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
	verify_count("adding service comments", num_services, "SELECT * FROM comment_tbl");

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
	verify_count("removing service comments", 0, "SELECT * FROM comment_tbl");

	free(orig);
}

static void test_downtime(void)
{
	nebstruct_downtime_data *orig, *mod;
	int i;

	orig = calloc(1, sizeof(*orig));
	orig->author_name = AUTHOR_NAME;
	orig->comment_data = COMMENT_DATA;

	sql_query("TRUNCATE TABLE scheduled_downtime");
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
	uint i;

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
	T_ENTRY(HOST_CHECK, host_check),
	T_ENTRY(SERVICE_CHECK, service_check),
	{ 0, "protocol stability", "test_broken_packets", test_broken_packets },
};

static int callback_is_tested(int id, const char *caller)
{
	uint i;
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
	char *merlin_conf = NULL;
	char cwd[PATH_MAX];

	use_database = 1;
	getcwd(cwd, sizeof(cwd));

	t_set_colors(0);

	log_grok_var("log_file", "stdout");
	log_grok_var("log_level", "info");
	if (argc < 2) {
		crash("No arguments. Wtf??");
	}
	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];

		if ((opt = strchr(arg, '='))) {
			*opt++ = '\0';
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
		merlin_conf = arg;
	}

	macro_x[MACRO_OBJECTCACHEFILE] = cache_file;
	macro_x[MACRO_STATUSDATAFILE] = status_log;

	nebmodule_init(-1, merlin_conf, NULL);
	/* make sure corefiles end up where we started from */
	chdir(cwd);
	if (!post_config_init) {
		t_fail("post_config_init not set (fatal)");
		exit(1);
	} else {
		sql_config("commit_interval", "0");
		sql_config("commit_queries", "0");
		sql_init();
		sql_query("TRUNCATE TABLE host");
		sql_query("TRUNCATE TABLE service");

		time_t start = time(NULL);
		nebstruct_process_data ds;
		ds.type = NEBTYPE_PROCESS_EVENTLOOPSTART;
		post_config_init(NEBCALLBACK_PROCESS_DATA, &ds);
		if (start + 1 < time(NULL)) {
			t_pass("import causes module to stall");
		} else {
			t_fail("import doesn't cause module to stall");
		}
	}
	t_setup();

	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		struct hook_info_struct *hi = &hook_info[i];

		ok_int(hi->cb_type, i, "hook is properly ordered");
	}

	/* shut valgrind up */
	memset(&pkt, 0, sizeof(pkt));

	for (i = 0; i < (int)ARRAY_SIZE(mtest); i++) {
		struct merlin_test *t = &mtest[i];

		pkt.hdr.type = t->callback;

		zzz();
		printf("Running %s to test %s\n", t->funcname, t->cb_name);

		t->test();
		if (!ipc_is_connected(0)) {
			t_fail("%s tests seems to crash the merlin daemon", t->cb_name);
			break;
		}
	}

	nebmodule_deinit(-1, -1);
	return t_end();
}
