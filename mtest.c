/*
 * This file contains tests for the "blockify()/deblockify()"
 * function, ensuring we don't garble data before we send it off
 */
#include "nagios/broker.h"
#include "nagios/macros.h"
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

#define test_compare(str) ok_str(mod->str, orig->str, #str)

static char *cache_file = "/opt/monitor/var/objects.cache";
static char *status_log = "/opt/monitor/var/status.log";
char *config_file;

int send_paths(void)
{
	size_t config_path_len, cache_path_len;
	merlin_event pkt;
	int result;

	if (!config_file || !cache_file) {
		lerr("config_file or xodtemplate_cache_file not set");
		return -1;
	}

	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.code = CTRL_PATHS;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;
	memset(pkt.body, 0, sizeof(pkt.body));

	/*
	 * Add the paths to pkt.body as nul-terminated strings.
	 * We simply rely on 32K bytes to be enough to hold the
	 * three paths we're interested in (and they are if we're
	 * on a unixy system, where PATH_MAX is normally 4096).
	 * We cheat a little and use pkt.hdr.len as an offset
	 * to the bytestream.
	 */
	config_path_len = strlen(config_file);
	cache_path_len = strlen(cache_file);
	memcpy(pkt.body, config_file, config_path_len);
	pkt.hdr.len = config_path_len;
	memcpy(pkt.body + pkt.hdr.len + 1, cache_file, cache_path_len);
	pkt.hdr.len += cache_path_len + 1;
	if (status_log && *status_log) {
		memcpy(pkt.body + pkt.hdr.len + 1, status_log, strlen(status_log));
		pkt.hdr.len += strlen(status_log) + 1;
	}

	/* nul-terminate and include the nul-char */
	pkt.body[pkt.hdr.len++] = 0;
	pkt.hdr.selection = 0;

	result = ipc_send_event(&pkt);
	if (result == packet_size(&pkt))
		return 0;

	return result;
}

static char **hosts;
static uint num_hosts;
static uint num_services;
static struct mtest_service {
	char *host_name;
	char *service_description;
} **services;

static uint count_rows(dbi_result result)
{
	uint rows = 0;

	if (result) {
		while (dbi_result_next_row(result))
			rows++;
	}

	return rows;
}

static uint count_table_rows(const char *table_name)
{
	sql_query("SELECT * FROM %s", table_name);
	return count_rows(sql_get_result());
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
		hosts[i] = dbi_result_get_string_copy_idx(result, 1);
		i++;
	}
	ok_uint(i, num_hosts, "number of hosts loaded");

	services = calloc(num_services, sizeof(*services));
	sql_query("SELECT host_name, service_description FROM service");
	result = sql_get_result();
	ok_uint(num_services, dbi_result_get_numrows(result), "libdbi service count");
	i = 0;
	while (i < num_services && dbi_result_next_row(result)) {
		services[i] = malloc(sizeof(*services));
		services[i]->host_name = dbi_result_get_string_copy_idx(result, 1);
		services[i]->service_description = dbi_result_get_string_copy_idx(result, 2);
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

	if (ipc_init() < 0) {
		t_fail("ipc_init()");
		crash("Failed to initialize ipc socket");
	} else {
		t_pass("ipc_init()");
	}
	ok_int(send_paths(), 0, "Sending paths");
	sleep(2);
	load_hosts_and_services();
}

static int test_flapping(char *service, int type)
{
	const char *typestr;
	nebstruct_flapping_data *orig, *mod;
	merlin_event pkt;
	if (type == NEBTYPE_FLAPPING_START)
		typestr = "start";
	else
		typestr = "stop";

	orig = calloc(1, sizeof(*orig));
	orig->host_name = HOST_NAME;
	orig->percent_change = 78.5;
	orig->high_threshold = 53.9;
	orig->low_threshold = 20.1;
	orig->comment_id = 1;
	if (service) {
		orig->service_description = service;
		orig->flapping_type = SERVICE_FLAPPING;
	} else {
		orig->flapping_type = HOST_FLAPPING;
	}
	blockify(orig, NEBCALLBACK_FLAPPING_DATA, pkt.body, sizeof(pkt.body));
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_FLAPPING_DATA);
	mod = (nebstruct_flapping_data *)pkt.body;
	test_compare(host_name);
	test_compare(service_description);
	pkt.hdr.len = blockify(orig, NEBCALLBACK_FLAPPING_DATA, pkt.body, sizeof(pkt.body));
	pkt.hdr.type = NEBCALLBACK_FLAPPING_DATA;
	pkt.hdr.selection = 0;
	mod->type = type;
	return ipc_send_event(&pkt);
}

static int test_host_check(void)
{
	nebstruct_host_check_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = calloc(1, sizeof(*orig));
	orig->host_name = HOST_NAME;
	orig->output = OUTPUT;
	orig->perf_data = PERF_DATA;
	gettimeofday(&orig->start_time, NULL);
	gettimeofday(&orig->end_time, NULL);
	len = blockify(orig, NEBCALLBACK_HOST_CHECK_DATA, pkt.body, sizeof(pkt.body));
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_HOST_CHECK_DATA);
	mod = (nebstruct_host_check_data *)pkt.body;
	test_compare(host_name);
	test_compare(output);
	test_compare(perf_data);
	len = blockify(orig, NEBCALLBACK_HOST_CHECK_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_HOSTCHECK_PROCESSED;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_HOST_CHECK_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

static int test_service_check(void)
{
	nebstruct_service_check_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = calloc(1, sizeof(*orig));
	orig->service_description = SERVICE_DESCRIPTION;
	orig->host_name = HOST_NAME;
	orig->output = OUTPUT;
	orig->perf_data = PERF_DATA;
	gettimeofday(&orig->start_time, NULL);
	gettimeofday(&orig->end_time, NULL);
	len = blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, pkt.body, sizeof(pkt.body));
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_SERVICE_CHECK_DATA);
	mod = (nebstruct_service_check_data *)pkt.body;
	test_compare(host_name);
	test_compare(output);
	test_compare(perf_data);
	test_compare(service_description);
	len = blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_SERVICECHECK_PROCESSED;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

static int test_contact_notification_method(char *service_description)
{
	nebstruct_contact_notification_method_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = calloc(1, sizeof(*orig));
	orig->host_name = HOST_NAME;
	orig->service_description = SERVICE_DESCRIPTION;
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

	len = blockify(orig, NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, pkt.body, sizeof(pkt.body));
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA);
	mod = (nebstruct_contact_notification_method_data *)pkt.body;
	test_compare(host_name);
	test_compare(service_description);
	test_compare(output);
	test_compare(contact_name);
	test_compare(ack_author);
	test_compare(ack_data);
	test_compare(command_name);
	len = blockify(orig, NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_CONTACTNOTIFICATIONMETHOD_END;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA;
	pkt.hdr.selection = 0;

	return ipc_send_event(&pkt);
}

static int test_adding_comment(char *service_description)
{
	nebstruct_comment_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = calloc(1, sizeof(*orig));
	orig->host_name = HOST_NAME;
	orig->author_name = AUTHOR_NAME;
	orig->comment_data = COMMENT_DATA;
	orig->entry_time = time(NULL);
	orig->expires = 1;
	orig->expire_time = time(NULL) + 300;
	orig->comment_id = 1;
	if (service_description)
		orig->service_description = service_description;
	len = blockify(orig, NEBCALLBACK_COMMENT_DATA, pkt.body, sizeof(pkt.body));
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_COMMENT_DATA);
	mod = (nebstruct_comment_data *)pkt.body;
	test_compare(host_name);
	test_compare(author_name);
	test_compare(comment_data);
	test_compare(service_description);
	len = blockify(orig, NEBCALLBACK_COMMENT_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_COMMENT_ADD;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_COMMENT_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

static int test_deleting_comment(void)
{
	nebstruct_comment_data *orig;
	merlin_event pkt;

	orig = (void *)pkt.body;
	memset(orig, 0, sizeof(*orig));
	orig->type = NEBTYPE_COMMENT_DELETE;
	orig->comment_id = 1;
	pkt.hdr.len = sizeof(*orig);
	pkt.hdr.type = NEBCALLBACK_COMMENT_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
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
}

int main(int argc, char **argv)
{
	char silly_buf[1024];
	int i, errors = 0;

	use_database = 1;

	t_set_colors(0);

	if (argc < 2) {
		ipc_grok_var("ipc_socket", "/opt/monitor/op5/merlin/ipc.sock");
	} else {
		printf("Reading config from '%s'\n", argv[1]);
		grok_config(argv[1]);
	}

	t_setup();

	for (i = 0; i < NEBCALLBACK_NUMITEMS; i++) {
		struct hook_info_struct *hi = &hook_info[i];

		if (hi->cb_type != i) {
			errors++;
			printf("hook_info for callback %d claims it's for callback %d\n",
					i, hi->cb_type);
		}
	}
	if (errors) {
		printf("%d error(s) in hookinfo struct. Expect coredumps\n", errors);
		errors = 0;
	} else {
		printf("No errors in hookinfo struct ordering\n");
	}

	while ((fgets(silly_buf, sizeof(silly_buf), stdin))) {

		if (!ipc_is_connected(0)) {
			printf("ipc socket is not connected\n");
			ipc_reinit();
			continue;
		}

		test_host_check();
		test_service_check();
		test_adding_comment(NULL);
		test_deleting_comment();
		test_adding_comment(SERVICE_DESCRIPTION);
		test_deleting_comment();
		test_contact_notification_method(NULL);
		test_contact_notification_method(SERVICE_DESCRIPTION);
		test_flapping(NULL, NEBTYPE_FLAPPING_START);
		test_flapping(NULL, NEBTYPE_FLAPPING_STOP);
		test_flapping(SERVICE_DESCRIPTION, NEBTYPE_FLAPPING_START);
		test_flapping(SERVICE_DESCRIPTION, NEBTYPE_FLAPPING_STOP);
		printf("## Total errrors: %d\n", errors);
	}

	return 0;
}
