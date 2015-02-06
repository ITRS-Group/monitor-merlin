#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "logging.h"
#include <glib.h>
#include <naemon/naemon.h>

static GHashTable *host_states, *svc_states;

int state_init(void)
{
	host_states = g_hash_table_new_full(g_str_hash, g_str_equal,
			free, free);
	svc_states = g_hash_table_new_full(nm_service_hash, nm_service_equal,
			(GDestroyNotify) nm_service_key_destroy, free);
	return 0;
}

void state_deinit(void)
{
	g_hash_table_destroy(host_states);
	g_hash_table_destroy(svc_states);
	host_states = NULL;
	svc_states = NULL;
}

static inline int has_state_change(int *old, int state, int type)
{
	/*
	 * A state change is considered to consist of a change
	 * to either state_type or state, so we OR the two
	 * together to form a complete state. This will make
	 * the module log as follows:
	 *    service foo;poo is HARD OK initially
	 *    service foo;poo goes to SOFT WARN, attempt 1   (logged)
	 *    service foo;poo goes to SOFT WARN, attempt 2   (not logged)
	 *    service foo;poo goes to HARD WARN              (logged)
	 */
	state = CAT_STATE(state, type);

	if (*old == state)
		return 0;

	*old = state;
	return 1;
}

int host_has_new_state(char *host, int state, int type)
{
	int *old_state;

	if (!host) {
		lerr("host_has_new_state() called with NULL host");
		return 0;
	}
	old_state = g_hash_table_lookup(host_states, host);
	if (!old_state) {
		int *cur_state;

		cur_state = malloc(sizeof(*cur_state));
		*cur_state = CAT_STATE(state, type);
		g_hash_table_insert(host_states, strdup(host), cur_state);
		return 1;
	}

	return has_state_change(old_state, state, type);
}

int service_has_new_state(char *host, char *desc, int state, int type)
{
	int *old_state;

	if (!host) {
		lerr("service_has_new_state() called with NULL host");
		return 0;
	}
	if (!desc) {
		lerr("service_has_new_state() called with NULL desc");
		return 0;
	}
	old_state = g_hash_table_lookup(svc_states, &((nm_service_key){host, desc}));
	if (!old_state) {
		int *cur_state;

		cur_state = malloc(sizeof(*cur_state));
		*cur_state = CAT_STATE(state, type);
		g_hash_table_insert(svc_states, nm_service_key_create(host, desc), cur_state);
		return 1;
	}

	return has_state_change(old_state, state, type);
}
