#include "logutils.h"
#include "cfgfile.h"
#include "auth.h"
#include <naemon/naemon.h>

static dkhash_table *auth_hosts, *auth_services;
static int blocksize = 1 << 20;

int auth_host_ok(const char *host)
{
	return !!dkhash_get(auth_hosts, host, NULL);
}

int auth_service_ok(const char *host, const char *svc)
{
	return !!(dkhash_get(auth_services, host, svc) || auth_host_ok(host));
}

int auth_read_input(FILE *input)
{
	auth_hosts = dkhash_create(8192);
	auth_services = dkhash_create(131072);
	if (!input)
		return 0;
	char *objectstr, *alloced;
	int tot_read = 0, len = 0;
	alloced = objectstr = malloc(blocksize);
	while ((len = fread(objectstr, 1, blocksize - tot_read - 1, input)) > 0) {
		tot_read += len;
		alloced[tot_read] = 0;
		if (tot_read + 1 >= blocksize) {
			blocksize *= 2;
			alloced = objectstr = realloc(alloced, blocksize);
		}
		objectstr = alloced + tot_read;
	}
	alloced[tot_read] = 0;
	objectstr = alloced;
	char *hostend;
	while (1) {
		char delim;
		hostend = strpbrk(objectstr, "\n;");
		if (hostend) {
			delim = *hostend;
			*hostend = 0;
		}
		dkhash_insert(auth_hosts, strdup(objectstr), NULL, (void*)1);
		if (!hostend)
			break;
		objectstr = hostend + 1;
		if (delim == '\n')
			break;
	}
	if (!hostend) {
		goto error_out;
	}
	char *svcend;
	while (1) {
		char delim;
		hostend = strpbrk(objectstr, "\n;");
		if (!hostend)
			break;
		if (*hostend == '\n') {
			// invalid. let's ignore the odd hostname
			goto error_out;
		}
		svcend = strpbrk(hostend+1, "\n;");
		*hostend = 0;
		if (svcend) {
			delim = *svcend;
			*svcend = 0;
		}
		dkhash_insert(auth_services, strdup(objectstr), strdup(hostend + 1), (void*)1);
		if (!svcend)
			break;
		objectstr = svcend + 1;
		if (delim == '\n')
			break;
	}
	free (alloced);
	return 0;
error_out:
	free(alloced);
	return 1;
}
