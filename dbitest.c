#include "sql.h"
#include "status.h"
#include <stdio.h>
#include <unistd.h>

#define die(fmt, args...) do { fprintf(stderr, fmt, ##args); exit(1); } while(0)

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

	return 0;
}
