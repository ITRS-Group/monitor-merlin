#include <stdlib.h>
#include <string.h>
#include "state.h"
#include "logging.h"
#include <naemon/naemon.h>

#define HOST_STATES_HASH_BUCKETS 4096
#define SERVICE_STATES_HASH_BUCKETS (HOST_STATES_HASH_BUCKETS * 4)
static dkhash_table *host_states, *svc_states;

int state_init(void)
{
	host_states = dkhash_create(HOST_STATES_HASH_BUCKETS);
	if (!host_states)
		return -1;

	svc_states = dkhash_create(SERVICE_STATES_HASH_BUCKETS);
	if (!svc_states) {
		free(host_states);
		host_states = NULL;
		return -1;
	}

	return 0;
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
	old_state = dkhash_get(host_states, host, NULL);
	if (!old_state) {
		int *cur_state;

		cur_state = malloc(sizeof(*cur_state));
		*cur_state = CAT_STATE(state, type);
		dkhash_insert(host_states, strdup(host), NULL, cur_state);
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
	old_state = dkhash_get(svc_states, host, desc);
	if (!old_state) {
		int *cur_state;

		cur_state = malloc(sizeof(*cur_state));
		*cur_state = CAT_STATE(state, type);
		dkhash_insert(svc_states, strdup(host), strdup(desc), cur_state);
		return 1;
	}

	return has_state_change(old_state, state, type);
}
