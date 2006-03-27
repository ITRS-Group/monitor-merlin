#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "shared.h"
#include "config.h"
#include "ipc.h"

/** global variables present in both daemon and module with same defaults **/
int is_noc = 0; /* default to poller in daemon and module */
int debug = 0;

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

int pulse_interval = 15;
int grok_common_var(struct compound *config, struct cfg_var *v)
{
	if (!strcmp(v->var, "mode")) {
		if (!strcmp(v->val, "poller"))
			is_noc = 0;
		else if (!strcmp(v->val, "noc") || !strcmp(v->val, "master"))
			is_noc = 1;
		else
			cfg_error(config, v, "Unknown value");

		return 1;
	}

	if (!strcmp(v->var, "pulse_interval")) {
		pulse_interval = (unsigned)strtoul(v->val, NULL, 10);
		if (!pulse_interval) {
			cfg_warn(config, v, "Illegal pulse_interval. Using default.");
			pulse_interval = 15;
		}
		return 1;
	}

	if (!strncmp(v->var, "ipc_", 4)) {
		if (!ipc_grok_var(v->var, v->val))
			cfg_error(config, v, "Failed to grok IPC option");

		return 1;
	}

	if (!strncmp(v->var, "log_", 4)) {
		if (!log_grok_var(v->var, v->val))
			cfg_error(config, v, "Failed to grok logging option");

		return 1;
	}

	return 0;
}

void grok_common_compound(struct compound *comp)
{
	int i;

	for (i = 0; i < comp->vars; i++)
		grok_common_var(comp, comp->vlist[i]);
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
