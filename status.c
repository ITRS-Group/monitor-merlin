#include "sql.h"
#include "status.h"
#include "daemon.h"
#include <stdio.h>
#include <string.h>
#include <dbi/dbi.h>

static struct object_state *object_states[2];
static size_t num_objects[2];

static int state_compare(const void *_a, const void *_b)
{
	object_state *a, *b;
	a = (object_state *)_a;
	b = (object_state *)_b;

	return strcmp(a->name, b->name);
}

static struct object_state *store_object_states(dbi_result result, size_t *count)
{
	int i = 0;
	struct object_state *state_ary = NULL;

	*count = dbi_result_get_numrows(result);
	if (!*count)
		goto out;

	state_ary = calloc(*count, sizeof(struct object_state));
	if (!state_ary)
		goto out;

	while (dbi_result_next_row(result)) {
		int state, state_type;
		struct object_state *os = &state_ary[i++];

		os->name = dbi_result_get_string_copy_idx(result, 1);
		state = dbi_result_get_int_idx(result, 2);
		state_type = dbi_result_get_int_idx(result, 3);
		os->state = concat_state(state_type, state);
	}

	/*
	 * Some sql engines sort case-insensitively, but we can't
	 * use that since "FOO" and "foo" are both valid names, and
	 * we're not allowed to be agnostic about them.
	 * In order to be 100% safe, we sort the results ourself
	 * instead. Note that in most cases, this will just loop
	 * once over the objects without actually do anything.
	 */
	qsort(state_ary, *count, sizeof(object_state), state_compare);

	out:
	sql_free_result();
	return state_ary;
}

static int prime_host_states(size_t *count)
{
	sql_query("SELECT host_name, current_state, state_type "
	          "FROM %s.host ORDER BY host_name", sql_db_name());
	object_states[0] = store_object_states(sql_get_result(), count);
	num_objects[0] = *count;

	return object_states[0] != NULL;
}

static int prime_service_states(size_t *count)
{
	sql_query("SELECT CONCAT(host_name, ';', service_description) as name, current_state, state_type "
	          "FROM %s.service ORDER BY name", sql_db_name());
	object_states[1] = store_object_states(sql_get_result(), count);
	num_objects[1] = *count;

	return object_states[1] != NULL;
}

static void destroy_states(struct object_state *ostate, size_t count)
{
	size_t i;

	if (!ostate)
		return;

	for (i = 0; i < count; i++)
		free(ostate[i].name);

	free(ostate);
}

int prime_object_states(size_t *hosts, size_t *services)
{
	if (!use_database)
		return 0;

	destroy_states(object_states[0], num_objects[0]);
	destroy_states(object_states[1], num_objects[1]);

	return prime_host_states(hosts) | prime_service_states(services);
}

object_state *get_object_state(const char *name, size_t id)
{
	size_t mid, high, low = 0;
	int result;
	object_state *ary;

	high = num_objects[id];
	ary = object_states[id];

	/* binary search in the alphabetically sorted array */
	while (low < high) {
		object_state *st;

		mid = low + ((high - low) / 2);
		st = &ary[mid];
		result = strcmp(name, st->name);
		if (result > 0) {
			low = mid + 1;
			continue;
		}
		if (result < 0) {
			high = mid;
			continue;
		}

		/* we hit the sweet spot */
		return st;
	}

	return NULL;
}

object_state *get_host_state(const char *name)
{
	return get_object_state(name, 0);
}

object_state *get_service_state(const char *h_name, const char *s_name)
{
	char name[4096];

	snprintf(name, sizeof(name) - 1, "%s;%s", h_name, s_name);
	return get_object_state(name, 1);
}

size_t foreach_state(int id, int (*fn)(object_state *))
{
	size_t i;
	for (i = 0; i < num_objects[id]; i++) {
		object_state *st = &object_states[id][i];
		fn(st);
	}
	return i;
}
