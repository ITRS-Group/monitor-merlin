#include "logutils.h"
#include "cfgfile.h"
#include "auth.h"
#include <naemon/naemon.h>
#include <glib.h>

static GHashTable *auth_hosts, *auth_services;
static int blocksize = 1 << 20;

int auth_host_ok(const char *host)
{
	return !!g_hash_table_lookup(auth_hosts, host);
}

int auth_service_ok(const char *host, const char *svc)
{
	return !!(g_hash_table_lookup(auth_services, &((nm_service_key){ (char *) host, (char *) svc })) || auth_host_ok(host));
}

int auth_read_input(FILE *input)
{
	char *objectstr, *alloced;
	int tot_read = 0, len = 0;
	char *hostend, *svcend;

	auth_hosts = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
	auth_services = g_hash_table_new_full(nm_service_hash, nm_service_equal, (GDestroyNotify) nm_service_key_destroy, NULL);
	if (!input)
		return 0;
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
	while (1) {
		char delim;
		hostend = strpbrk(objectstr, "\n;");
		if (hostend) {
			delim = *hostend;
			*hostend = 0;
		}
		g_hash_table_insert(auth_hosts, strdup(objectstr), (void*)1);
		if (!hostend)
			break;
		objectstr = hostend + 1;
		if (delim == '\n')
			break;
	}
	if (!hostend) {
		goto error_out;
	}
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
		g_hash_table_insert(auth_services, nm_service_key_create(objectstr, hostend+1), (void*)1);
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
