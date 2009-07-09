/*
 * This file contains tests for the "blockify()/deblockify()"
 * function, ensuring we don't garble data before we send it off
 */
#include "nagios/broker.h"
#include "shared.h"
#include "hookinfo.h"

#define HOST_NAME "devel"
#define SERVICE_DESCRIPTION "PING"
#define OUTPUT "The plugin output"
#define PERF_DATA "random_value='5;5;5'"

#define test_compare(str) _compare_ptr_strings(mod->str, orig->str, errors)

static inline void _compare_ptr_strings(char *a, char *b, int *errors)
{
	*errors += a == b;
	*errors += !!strcmp(a, b);
}

static int test_service_check_data(int *errors)
{
	nebstruct_service_check_data *orig, *mod;
	merlin_event pkt;
	int len;

	orig = malloc(sizeof(*orig));
	mod = malloc(sizeof(*mod));

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

	orig = malloc(sizeof(*orig));

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
	printf("blockify returned %d\n", len);
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

static int grok_config(char *path)
{
	struct cfg_comp *config;
	int i;

	config = cfg_parse_file(path);
	if (!config)
		return -1;

	for (i = 0; i < config->vars; i++) {
		struct cfg_var *v = config->vlist[i];

		if (grok_common_var(config, v))
			continue;
		printf("'%s' = '%s' is not grok'ed as a common variable\n", v->key, v->value);
	}

	return 0;
}

int main(int argc, char **argv)
{
	char silly_buf[1024];
	int i, errors = 0;

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
		test_adding_comment(&errors, "PING");
		test_deleting_comment(&errors);
		printf("## Total errrors: %d\n", errors);
	}


	return 0;
}
