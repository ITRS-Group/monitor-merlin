/*
 * This file contains tests for the "blockify()/deblockify()"
 * function, ensuring we don't garble data before we send it off
 */
#include "nagios/broker.h"
#include "shared.h"
#include "hookinfo.h"
#include "test_utils.h"

#define HOST_NAME "devel"
#define SERVICE_DESCRIPTION "PING"
#define OUTPUT "The plugin output"
#define PERF_DATA "random_value='5;5;5'"
#define CONTACT_NAME "contact-name"
#define test_compare(str) ok_str(mod->str, orig->str, #str)

static char *cache_file = "/opt/monitor/var/objects.cache";
static char *status_log = "/opt/monitor/var/status.log";
char *config_file;

static int test_service_check_data(int *errors)
{
	nebstruct_service_check_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = calloc(1, sizeof(*orig));

	orig->service_description = strdup(SERVICE_DESCRIPTION);
	orig->host_name = strdup(HOST_NAME);
	orig->output = strdup(OUTPUT);
	orig->perf_data = strdup(PERF_DATA);
	gettimeofday(&orig->start_time, NULL);
	gettimeofday(&orig->end_time, NULL);
	len = blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, pkt.body, sizeof(pkt.body));
	printf("blockify() returned %d\n", len);
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_SERVICE_CHECK_DATA);
	mod = (nebstruct_service_check_data *)pkt.body;
	test_compare(host_name);
	test_compare(output);
	test_compare(perf_data);
	test_compare(service_description);
	printf("Sending ipc_event for service '%s' on host '%s'\n  output: '%s'\n  perfdata: %s'\n",
		   mod->service_description, mod->host_name, mod->output, mod->perf_data);
	len = blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_SERVICECHECK_PROCESSED;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

static int test_host_check_data(int *errors)
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
	printf("blockify() returned %d\n", len);
	deblockify(pkt.body, sizeof(pkt.body), NEBCALLBACK_HOST_CHECK_DATA);
	mod = (nebstruct_host_check_data *)pkt.body;
	test_compare(host_name);
	test_compare(output);
	test_compare(perf_data);
	printf("Sending ipc_event for host '%s'\n  output: '%s'\n  perfdata: %s'\n",
		   mod->host_name, mod->output, mod->perf_data);
	len = blockify(orig, NEBCALLBACK_HOST_CHECK_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_HOSTCHECK_PROCESSED;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_HOST_CHECK_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

#define AUTHOR_NAME "Pelle plutt"
#define COMMENT_DATA "comment data"
#define COMMAND_NAME "notify-by-squirting"
static int test_contact_notification_method(int *errors, char *service_description)
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

static int test_adding_comment(int *errors, char *service_description)
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
	if (service_description) {
		test_compare(service_description);
	}
	len = blockify(orig, NEBCALLBACK_COMMENT_DATA, pkt.body, sizeof(pkt.body));
	mod->type = NEBTYPE_COMMENT_ADD;
	pkt.hdr.len = len;
	pkt.hdr.type = NEBCALLBACK_COMMENT_DATA;
	pkt.hdr.selection = 0;
	return ipc_send_event(&pkt);
}

static int test_deleting_comment(int *errors)
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

	t_set_colors(0);

	if (argc < 2) {
		ipc_grok_var("ipc_socket", "/opt/monitor/op5/merlin/ipc.sock");
	} else {
		printf("Reading config from '%s'\n", argv[1]);
		grok_config(argv[1]);
	}

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

	ipc_init();
	while ((fgets(silly_buf, sizeof(silly_buf), stdin))) {

		if (!ipc_is_connected(0)) {
			printf("ipc socket is not connected\n");
			ipc_reinit();
			continue;
		}
		test_host_check_data(&errors);
		test_service_check_data(&errors);
		test_adding_comment(&errors, NULL);
		test_deleting_comment(&errors);
		test_contact_notification_method(&errors, NULL);
		test_contact_notification_method(&errors, SERVICE_DESCRIPTION);
		test_adding_comment(&errors, SERVICE_DESCRIPTION);
		test_deleting_comment(&errors);
		printf("## Total errrors: %d\n", errors);
	}

	return 0;
}
