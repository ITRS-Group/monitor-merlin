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
	int errors = 0;

	orig = malloc(sizeof(*orig));
	mod = malloc(sizeof(*mod));

	orig->service_description = strdup(SERVICE_DESCRIPTION);
	orig->host_name = strdup(HOST_NAME);
	orig->output = strdup(OUTPUT);
	orig->perf_data = strdup(PERF_DATA);
	blockify(orig, NEBCALLBACK_SERVICE_CHECK_DATA, buf, sizeof(buf));
	deblockify(buf, sizeof(buf), NEBCALLBACK_SERVICE_CHECK_DATA);
	mod = (nebstruct_service_check_data *)buf;
	errors += !!strcmp(mod->host_name, orig->host_name);
	errors += !!strcmp(mod->service_description, orig->service_description);
	errors += !!strcmp(mod->output, orig->output);
	errors += !!strcmp(mod->perf_data, orig->perf_data);

	return errors;
}

int test_host_check_data()
{
	nebstruct_host_check_data *orig, *mod;
	char buf[16384];
	int errors = 0;

	orig = malloc(sizeof(*orig));
	mod = malloc(sizeof(*mod));

	orig->host_name = HOST_NAME;
	orig->output = OUTPUT;
	orig->perf_data = PERF_DATA;
	blockify(orig, NEBCALLBACK_HOST_CHECK_DATA, buf, sizeof(buf));
	deblockify(buf, sizeof(buf), NEBCALLBACK_HOST_CHECK_DATA);
	mod = (nebstruct_host_check_data *)buf;
	errors += !!strcmp(mod->host_name, orig->host_name);
	errors += !!strcmp(mod->output, orig->output);
	errors += !!strcmp(mod->perf_data, orig->perf_data);
	printf("orig: '%s' (%p); mod: '%s' (%p)\n",
		   orig->host_name, orig->host_name, mod->host_name, mod->host_name);
	printf("orig: '%s' (%p); mod: '%s' (%p)\n",
		   orig->output, orig->output, mod->output, mod->output);
	printf("orig: '%s' (%p); mod: '%s' (%p)\n",
		   orig->perf_data, orig->perf_data, mod->perf_data, mod->perf_data);

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
		printf("usage: %s <config-file>\n", argv[0]);
		return 1;
	}

	printf("Reading config from '%s'\n", argv[1]);
	grok_config(argv[1]);

	ipc_init();

	errors += test_service_check_data();
	errors += test_host_check_data();
	printf("Total errrors: %d\n", errors);

	return !!errors;
}
