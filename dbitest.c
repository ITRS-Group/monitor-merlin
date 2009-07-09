#include "sql.h"
#include "status.h"
#include <stdio.h>
#include <unistd.h>

#define die(fmt, args...) do { fprintf(stderr, fmt, ##args); exit(1); } while(0)

int use_database = 1;
static size_t ok, id;
static int try_states(object_state *orig)
{
	object_state *st = get_object_state(orig->name, id);

	if (st != orig) {
		printf("Failed to locate item '%s'\n", orig->name);
		printf("ok lookups: %d\n", ok);
		exit(1);
	}
	ok++;

	return 0;
}

int main(int argc, char **argv)
{
	size_t hosts, services;
	int i, iterations = 1;

	if (sql_init() < 0)
		die("sql_init() failed");

	for (i = 1; i < argc; i++) {
		if (atoi(argv[i]) > 0)
			iterations = atoi(argv[i]);
	}

	for (i = 0; i < iterations; i++) {
		prime_object_states(&hosts, &services);
		if (iterations > 1)
			sleep(1);
	}
	printf("Primed status for %zu hosts and %zu services\n", hosts, services);
	foreach_state(0, try_states);
	printf("%zu of %zu hosts located ok\n", ok, hosts);
	ok = 0; id = 1;
	foreach_state(1, try_states);
	printf("%zu of %zu services located ok\n", ok, services);

	return 0;
}
