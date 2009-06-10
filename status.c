#include "sql.h"
#include <stdio.h>
#include <dbi/dbi.h>

struct object_state {
	char *name;
	int state;
};
static struct object_state *object_states[2];
static size_t num_objects[2];

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
		os->state = (state_type << 16) | state;
	}

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
	destroy_states(object_states[0], num_objects[0]);
	destroy_states(object_states[1], num_objects[1]);

	return prime_host_states(hosts) | prime_service_states(services);
}
