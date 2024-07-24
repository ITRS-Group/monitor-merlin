#include "logging.h"
#include "ipc.h"
#include "shared.h"

#ifdef MERLIN_MODULE_BUILD
#include <module/oconfsplit.h>
#endif

int db_log_reports = 1;
int db_log_notifications = 1;
merlin_confsync global_csync;

/* This lets the module build without database stuff linked in */
extern int sql_config(const char *key, const char *value);

static const char *config_key_expires(const char *var)
{
	if (!strcmp(var, "ipc_debug_write"))
		return "2011-05";
	if (!strcmp(var, "ipc_debug_read"))
		return "2011-05";

	return NULL;
}

void grok_db_compound(struct cfg_comp *c)
{
	unsigned int vi;

	use_database = 1;
	for (vi = 0; vi < c->vars; vi++) {
		struct cfg_var *v = c->vlist[vi];
		if (!strcmp(v->key, "log_report_data")) {
			db_log_reports = strtobool(v->value);
		} else if (!prefixcmp(v->key, "log_notification")) {
			db_log_notifications = strtobool(v->value);
		} else if (!prefixcmp(v->key, "track_current")) {
			lwarn("Option '%s' in the database compound is deprecated", v->key);
		} else if (!strcmp(v->key, "enabled")) {
			use_database = strtobool(v->value);
		} else {
			sql_config(v->key, v->value);
		}
	}
}

/*
 * Parse config sync options.
 *
 * This is used for each node and also in the daemon compound
 */
int grok_confsync_compound(struct cfg_comp *comp, merlin_confsync *csync)
{
	unsigned i;

	if (!comp || !csync) {
		return -1;
	}

	/*
	 * first we reset it. An empty compound in the configuration
	 * means "reset the defaults and don't bother syncing this
	 * server automagically"
	 */
	memset(csync, 0, sizeof(*csync));
	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		if (!strcmp(v->key, "push")) {
			csync->push.cmd = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "fetch") || !strcmp(v->key, "pull")) {
			csync->fetch.cmd = strdup(v->value);
			continue;
		}
		/*
		 * we ignore additional variables here, since the
		 * config sync script may want to add additional
		 * stuff to handle
		 */
	}

	return 0;
}

/**
 * Convert a string containing a positive number less than 2^64 to an unsigned long long int
 *
 * @param value A numeric string that can start with +
 * @param out   Where the number will be written on a successful return, otherwise undefined.
 *              The value might be overwritten on failure.
 * @return      1 on success, 0 on failure
 */
static int read_positive_number(const char *value, unsigned long long int *out)
{
	char *endptr = NULL;
	
	assert(value != NULL);
	assert(out != NULL);

	if (*value == '-') {
		return 0;
	}

	*out = strtoull(value, &endptr, 10);

	if (*endptr != '\0' || endptr == value) {
		return 0;
	}

	return 1;
}

/**
 * @return 1 on success, 0 on failure
 */
static int grok_binlog_var(const char *key, const char *value)
{
	if (!strcmp(key, "binlog_dir")) {
		if (binlog_dir != NULL)
			free(binlog_dir);
		binlog_dir = strdup(value);
		return 1;
	}

	if (!strcmp(key, "binlog_max_memory_size")) {
		return read_positive_number(value, &binlog_max_memory_size);
	}

	if (!strcmp(key, "binlog_max_file_size")) {
		return read_positive_number(value, &binlog_max_file_size);
	}

	if (!strcmp(key, "binlog_persist")) {
		int binlog_val = atoi(value);
		if (binlog_val == 0) {
		 	binlog_persist = false;
		} else {
			binlog_persist = true;
		}
		return 1;
	}

	return 0;
}

/**
 * This function may call exit() down the line.
 *
 * @return 1 if the var was parsed, 0 if it did not match any rule
 */
int grok_common_var(struct cfg_comp *config, struct cfg_var *v)
{
	const char *expires;

	if (!strcmp(v->key, "pulse_interval")) {
		pulse_interval = (unsigned)strtoul(v->value, NULL, 10);
		if (!pulse_interval) {
			cfg_warn(config, v, "Illegal pulse_interval. Using default.");
			pulse_interval = 10;
		}
		return 1;
	}

	expires = config_key_expires(v->key);
	if (expires) {
		cfg_warn(config, v, "'%s' is a deprecated variable, scheduled for "
			 "removal at the first release after %s", v->key, expires);
		/* it's understood, in a way */
		return 1;
	}

	if (!prefixcmp(v->key, "ipc_")) {
		if (!ipc_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok IPC option");

		return 1;
	}

	if (!prefixcmp(v->key, "log_") || !strcmp(v->key, "use_syslog")) {
		if (!log_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok logging option");

		return 1;
	}

	if (!prefixcmp(v->key, "binlog_")) {
		if (!grok_binlog_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok binlog option");

		return 1;
	}

	if (!prefixcmp(v->key, "oconfsplit_")) {
#ifdef MERLIN_MODULE_BUILD
		if (!split_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok oconfsplit option");
#endif

		return 1;
	}
	return 0;
}
