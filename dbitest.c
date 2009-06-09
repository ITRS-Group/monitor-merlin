#include "sql.h"
#include <stdio.h>
#include <dbi/dbi.h>

#define die(fmt, args...) do { fprintf(stderr, fmt, ##args); exit(1); } while(0)

struct object_state {
	char *name;
	int state;
};
static struct object_state *object_states[2];

static struct object_state *store_object_states(dbi_result result, size_t *count)
{
	int i = 0;
	struct object_state *state_ary;

	*count = dbi_result_get_numrows(result);
	if (!*count)
		return NULL;

	state_ary = calloc(*count, sizeof(struct object_state));
	if (!state_ary)
		return NULL;

	while (dbi_result_next_row(result)) {
		int state, state_type;
		struct object_state *os = &state_ary[i++];

		os->name = dbi_result_get_string_copy_idx(result, 1);
		state = dbi_result_get_int_idx(result, 2);
		state_type = dbi_result_get_int_idx(result, 3);
		os->state = (state_type << 16) | state;

		printf("state: %d; state_type: %d; name: %s; os->state: %d\n",
			   state, state_type, os->name, os->state);
	}

	return state_ary;
}

static int prime_host_states(size_t *count)
{
	sql_query("SELECT host_name, current_state, state_type "
	          "FROM %s.host ORDER BY host_name", sql_db_name());
	object_states[0] = store_object_states((dbi_result)sql_get_result(), count);

	return object_states[0] != NULL;
}

static int prime_service_states(size_t *count)
{
	sql_query("SELECT CONCAT(host_name, ';', service_description) as name, current_state, state_type "
	          "FROM %s.service ORDER BY name", sql_db_name());
	object_states[1] = store_object_states((dbi_result)sql_get_result(), count);

	return object_states[1] != NULL;
}

int main(int argc, char **argv)
{
	size_t count;

	if (sql_init() < 0)
		die("sql_init() failed");

	prime_host_states(&count);
	printf("Primed status for %d hosts\n", count);
	prime_service_states(&count);
	printf("Primed status for %d services\n", count);

	return 0;
}
