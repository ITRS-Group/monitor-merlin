#include <string.h>
#include <naemon/naemon.h>
#include "logging.h"
#include "db_updater.h"
#include "cfgfile.h"
#include "shared.h"
#include "configuration.h"
#include "sql.h"

static void *neb_handle = NULL;
static int is_importing = 0;
static char *import_program;

static void import_completion (__attribute__((unused)) struct wproc_result *result, __attribute__((unused)) void *data, __attribute__((unused)) int val)
{
	is_importing = 0;
	ldebug("Import completed");
}

/*
 * import objects and status from objects.cache and status.log,
 * respecively
 */
static int import_objects_and_status(char *cfg, char *cache)
{
	char *cmd;
	int result = 0;

	/* don't bother if an import is already in progress */
	if (is_importing) {
		lwarn("Import already in progress. Ignoring import event");
		return 0;
	}

	if (!import_program) {
		lerr("No import program specified. Ignoring import event");
		return 0;
	}

	asprintf(&cmd, "%s --nagios-cfg='%s' "
			 "--db-type='%s' --db-name='%s' --db-user='%s' --db-pass='%s' --db-host='%s' --db-conn_str='%s'",
			 import_program, cfg,
			 sql_db_type(), sql_db_name(), sql_db_user(), sql_db_pass(), sql_db_host(), sql_db_conn_str());
	if (cache && *cache) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --cache='%s'", cmd2, cache);
		free(cmd2);
	}

	if (sql_db_port()) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --db-port='%u'", cmd2, sql_db_port());
		free(cmd2);
	}

	is_importing = 1;
	ldebug("Running import program: %s", cmd);
	wproc_run_callback(cmd, 60, import_completion, NULL, NULL);
	free(cmd);

	return result;
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "import_program")) {
			import_program = strdup(v->value);
			continue;
		}
	}

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];
		uint vi;

		if (!prefixcmp(c->name, "database")) {
			for (vi = 0; vi < c->vars; vi++) {
				struct cfg_var *v = c->vlist[vi];
				if (!strcmp(v->key, "log_report_data")) {
					db_log_reports = strtobool(v->value);
				} else if (!prefixcmp(v->key, "log_notification")) {
					db_log_notifications = strtobool(v->value);
				} else {
					sql_config(v->key, v->value);
				}
			}
			continue;
		}
	}
}

static int read_config(char *cfg_file)
{
	uint i;
	struct cfg_comp *config;

	merlin_config_file = nspath_absolute(cfg_file, config_file_dir);

	if (!(config = cfg_parse_file(merlin_config_file))) {
		lwarn("Failed to read config file %s", merlin_config_file);
		free(merlin_config_file);
		merlin_config_file = NULL;
		return -1;
	}

	for (i = 0; i < config->vars; i++) {
		if (!prefixcmp(config->vlist[i]->key, "log_") || !strcmp(config->vlist[i]->key, "use_syslog")) {
			if (!log_grok_var(config->vlist[i]->key, config->vlist[i]->value)) {
				cfg_error(config, config->vlist[i], "Failed to grok logging option");
				return 1;
			}
		}
	}

	for (i = 0; i < config->nested; i++) {
		struct cfg_comp *c = config->nest[i];
		if (!prefixcmp(c->name, "daemon")) {
			grok_daemon_compound(c);
			continue;
		}
	}

	cfg_destroy_compound(config);
	return 0;
}


/*
 * This function gets called before and after Nagios has read its config
 * and written its objects.cache and status.log files.
 * We want to setup object lists and such here, so we only care about the
 * case where config has already been read.
 */
static int post_config_init(__attribute__((unused)) int cb, void *ds)
{
	char *cache_file;

	if (*(int *)ds != NEBTYPE_PROCESS_EVENTLOOPSTART)
		return 0;

	asprintf(&cache_file, "/%s/timeperiods.cache", temp_path);
	import_objects_and_status(config_file, cache_file);

	db_updater_register(neb_handle);

	return 0;
}

/**
 * Initialization routine for the eventbroker module. This
 * function gets called by Nagios when it's done loading us
 */
int nebmodule_init(__attribute__((unused)) int flags, __attribute__((unused)) char *arg, nebmodule *handle)
{
	neb_handle = (void *)handle;

	if (read_config(arg) < 0) {
		return -1;
	}
	log_init();

	neb_register_callback(NEBCALLBACK_PROCESS_DATA, neb_handle, 0, post_config_init);

	return 0;
}

/**
 * Called by Nagios prior to the module being unloaded.
 * This function is supposed to release all pointers we've allocated
 * and make sure we reset it to a state where we can initialize it
 * later.
 */
int nebmodule_deinit(__attribute__((unused)) int flags, __attribute__((unused)) int reason)
{
	db_updater_unregister(neb_handle);
	return 0;
}
