#include "sql.h"
#include "status.h"
#include <stdio.h>

#define die(fmt, args...) do { fprintf(stderr, fmt, ##args); exit(1); } while(0)

int main(int argc, char **argv)
{
	size_t hosts, services;
	int i;

	if (sql_init() < 0)
		die("sql_init() failed");

	for (i = 0; i < 100; i++) {
		prime_object_states(&hosts, &services);
		sleep(1);
	}
	printf("Primed status for %zu hosts and %zu services\n", hosts, services);

	return 0;
}
