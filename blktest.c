/*
 * This file contains tests for the "blockify()/deblockify()"
 * function, ensuring we don't garble data before we send it off
 */
#include "nagios/nebstructs.h"
#include "nagios/nebcallbacks.h"
#include "test_utils.h"

#define HOST_NAME "thehost"
#define SERVICE_DESCRIPTION "A service of random description"
#define OUTPUT "The plugin output"
#define PERF_DATA "random_value='5;5;5'"

int test_service_check_data()
{
	nebstruct_service_check_data *orig, *mod;
	char buf[16384];
	int len, errors = 0;

	orig = malloc(sizeof(*orig));
	mod = malloc(sizeof(*mod));

	orig->service_description = strdup(SERVICE_DESCRIPTION);
	orig->host_name = strdup(HOST_NAME);
	orig->output = strdup(OUTPUT);
	orig->perf_data = strdup(PERF_DATA);
	len = blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, buf, sizeof(buf));
	printf("blockify() returned %d\n", len);
	deblockify(buf, sizeof(buf), NEBCALLBACK_SERVICE_CHECK_DATA);
	mod = (nebstruct_service_check_data *)buf;
	errors += mod->host_name == orig->host_name;
	errors += mod->service_description == orig->service_description;
	errors += mod->output == orig->output;
	errors += mod->perf_data == orig->perf_data;
	errors += !!strcmp(mod->host_name, orig->host_name);
	errors += !!strcmp(mod->service_description, orig->service_description);
	errors += !!strcmp(mod->output, orig->output);
	errors += !!strcmp(mod->perf_data, orig->perf_data);
	printf("Sending ipc_event for service '%s' on host '%s'\n  output: '%s'\n  perfdata: %s'\n",
		   mod->service_description, mod->host_name, mod->output, mod->perfdata);
	ipc_send_event(NEBCALLBACK_SERVICE_CHECK_DATA, 0, buf, len);

	return errors;
}

int test_host_check_data()
{
	nebstruct_host_check_data *orig, *mod;
	char buf[16384];
	int len, errors = 0;

	orig = malloc(sizeof(*orig));
	mod = malloc(sizeof(*mod));

	orig->host_name = HOST_NAME;
	orig->output = OUTPUT;
	orig->perf_data = PERF_DATA;
	len = blockify(orig, NEBCALLBACK_HOST_CHECK_DATA, buf, sizeof(buf));
	printf("blockify() returned %d\n", len);
	deblockify(buf, sizeof(buf), NEBCALLBACK_HOST_CHECK_DATA);
	mod = (nebstruct_host_check_data *)buf;
	errors += mod->host_name == orig->host_name;
	errors += mod->output == orig->output;
	errors += mod->perf_data == orig->perf_data;
	errors += !!strcmp(mod->host_name, orig->host_name);
	errors += !!strcmp(mod->output, orig->output);
	errors += !!strcmp(mod->perf_data, orig->perf_data);
	printf("Sending ipc_event for host '%s'\n  output: '%s'\n  perfdata: %s'\n",
		   mod->host_name, mod->output, mod->perfdata);
	ipc_send_event(NEBCALLBACK_HOST_CHECK_DATA, 0, buf, len);

	return errors;
}

static int grok_config(char *path)
{
	struct compound *config;
	int i;

	config = cfg_parse_file(path);
	if (!config)
		return -1;

	for (i = 0; i < config->vars; i++) {
		struct cfg_var *v = config->vlist[i];

		if (grok_common_var(config, v))
			continue;
		printf("'%s' = '%s' is not grok'ed as a common variable\n", v->var, v->val);
	}

	return 0;
}

int main(int argc, char **argv)
{
	int errors = 0;

	if (argc < 2) {
		ipc_grok_var("ipc_socket", "/opt/monitor/op5/merlin/ipc.sock");
	} else {
		printf("Reading config from '%s'\n", argv[1]);
		grok_config(argv[1]);
	}

	ipc_init();

	errors += test_service_check_data();
	errors += test_host_check_data();
	printf("Total errrors: %d\n", errors);

	return !!errors;
}
