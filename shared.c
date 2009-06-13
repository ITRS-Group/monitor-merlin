#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "nagios/nebcallbacks.h"
#include "shared.h"
#include "config.h"
#include "ipc.h"

/** global variables present in both daemon and module **/
int is_noc = 0; /* denotes if we have pollers or peers */
int debug = 0;  /* doesn't actually do anything right now */
int is_module = 1; /* the daemon sets this to 0 immediately */
int pulse_interval = 15;

#ifndef ISSPACE
# define ISSPACE(c) (c == ' ' || c == '\t')
#endif

char *next_word(char *str)
{
	while (!ISSPACE(*str))
		str++;

	while (ISSPACE(*str) || *str == ',')
		str++;

	if (*str)
		return str;

	return NULL;
}

#define CB_ENTRY(s) #s
static const char *callback_names[NEBCALLBACK_NUMITEMS] = {
	CB_ENTRY(NEBCALLBACK_RESERVED0),
	CB_ENTRY(NEBCALLBACK_RESERVED1),
	CB_ENTRY(NEBCALLBACK_RESERVED2),
	CB_ENTRY(NEBCALLBACK_RESERVED3),
	CB_ENTRY(NEBCALLBACK_RESERVED4),
	CB_ENTRY(NEBCALLBACK_RAW_DATA),
	CB_ENTRY(NEBCALLBACK_NEB_DATA),
	CB_ENTRY(NEBCALLBACK_PROCESS_DATA),
	CB_ENTRY(NEBCALLBACK_TIMED_EVENT_DATA),
	CB_ENTRY(NEBCALLBACK_LOG_DATA),
	CB_ENTRY(NEBCALLBACK_SYSTEM_COMMAND_DATA),
	CB_ENTRY(NEBCALLBACK_EVENT_HANDLER_DATA),
	CB_ENTRY(NEBCALLBACK_NOTIFICATION_DATA),
	CB_ENTRY(NEBCALLBACK_SERVICE_CHECK_DATA),
	CB_ENTRY(NEBCALLBACK_HOST_CHECK_DATA),
	CB_ENTRY(NEBCALLBACK_COMMENT_DATA),
	CB_ENTRY(NEBCALLBACK_DOWNTIME_DATA),
	CB_ENTRY(NEBCALLBACK_FLAPPING_DATA),
	CB_ENTRY(NEBCALLBACK_PROGRAM_STATUS_DATA),
	CB_ENTRY(NEBCALLBACK_HOST_STATUS_DATA),
	CB_ENTRY(NEBCALLBACK_SERVICE_STATUS_DATA),
	CB_ENTRY(NEBCALLBACK_ADAPTIVE_PROGRAM_DATA),
	CB_ENTRY(NEBCALLBACK_ADAPTIVE_HOST_DATA),
	CB_ENTRY(NEBCALLBACK_ADAPTIVE_SERVICE_DATA),
	CB_ENTRY(NEBCALLBACK_EXTERNAL_COMMAND_DATA),
	CB_ENTRY(NEBCALLBACK_AGGREGATED_STATUS_DATA),
	CB_ENTRY(NEBCALLBACK_RETENTION_DATA),
	CB_ENTRY(NEBCALLBACK_CONTACT_NOTIFICATION_DATA),
	CB_ENTRY(NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA),
	CB_ENTRY(NEBCALLBACK_ACKNOWLEDGEMENT_DATA),
	CB_ENTRY(NEBCALLBACK_STATE_CHANGE_DATA),
	CB_ENTRY(NEBCALLBACK_CONTACT_STATUS_DATA),
	CB_ENTRY(NEBCALLBACK_ADAPTIVE_CONTACT_DATA)
};

const char *callback_name(int id)
{
	if (id < 0 || id > NEBCALLBACK_NUMITEMS - 1)
		return NULL;

	return callback_names[id];
}

const char *config_key_expires(const char *var)
{
	if (!strcmp(var, "mode"))
		return "2009-10";

	return NULL;
}

int grok_common_var(struct compound *config, struct cfg_var *v)
{
	const char *expires;

	if (!strcmp(v->var, "pulse_interval")) {
		pulse_interval = (unsigned)strtoul(v->val, NULL, 10);
		if (!pulse_interval) {
			cfg_warn(config, v, "Illegal pulse_interval. Using default.");
			pulse_interval = 15;
		}
		return 1;
	}

	if (!prefixcmp(v->var, "ipc_")) {
		if (!ipc_grok_var(v->var, v->val))
			cfg_error(config, v, "Failed to grok IPC option");

		return 1;
	}

	if (!prefixcmp(v->var, "log_")) {
		if (!log_grok_var(v->var, v->val))
			cfg_error(config, v, "Failed to grok logging option");

		return 1;
	}

	expires = config_key_expires(v->var);
	if (expires) {
		cfg_warn(config, v, "'%s' is a deprecated variable, scheduled for "
			 "removal at the first release after %s", v->var, expires);
	}

	return 0;
}

static int nsel;
static char **selection_table = NULL;

char *get_sel_name(int index)
{
	if (index < 0 || index >= nsel)
		return NULL;

	return selection_table[index];
}

int get_sel_id(const char *name)
{
	int i;

	if (!nsel || !name)
		return -1;

	for (i = 0; i < nsel; i++) {
		if (!strcmp(name, selection_table[i]))
			return i;
	}

	return -1;
}

int get_num_selections(void)
{
	return nsel;
}

int add_selection(char *name)
{
	int i;

	/* don't add the same selection twice */
	for (i = 0; i < nsel; i++)
		if (!strcmp(name, selection_table[i]))
			return i;

	selection_table = realloc(selection_table, sizeof(char *) * (nsel + 1));
	selection_table[nsel] = name;

	return nsel++;
}
