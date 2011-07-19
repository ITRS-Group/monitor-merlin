#include "logutils.h"
#include "cfgfile.h"
#include "hash.h"
#include "auth.h"

static const char *user;
static char **cgroups;
static uint num_cgroups, cgroup_idx;
static hash_table *auth_hosts, *auth_services;
static int is_host_root = 1, is_service_root = 1;

int auth_host_ok(const char *host)
{
	if (is_host_root)
		return 1;

	return !!hash_find(auth_hosts, host);
}

int auth_service_ok(const char *host, const char *svc)
{
	if (is_service_root || is_host_root)
		return 1;

	if ((hash_find2(auth_services, host, svc)) || auth_host_ok(host))
		return 1;

	return 0;
}

static int list_has_entry(const char *list, const char *ent)
{
	unsigned int len;
	const char *p;

	len = strlen(ent);

	for (p = list - 1; p; p = strchr(++p, ',')) {
		p++;
		if (!strncmp(p, ent, len) && (p[len] == ',' || !p[len])) {
			return 1;
		}
	}

	return 0;
}

static int list_has_any_entry(const char *list, char **ents, int nents)
{
	int i;

	for (i = 0; i < nents; i++) {
		if (list_has_entry(list, ents[i])) {
			return 1;
		}
	}
	return 0;
}

static int grok_host(struct cfg_comp *obj)
{
	uint i;
	char *host_name = NULL;
	char *contacts = NULL;
	char *groups = NULL;

	for (i = 0; i < obj->vars; i++) {
		struct cfg_var *v = obj->vlist[i];
		if (!strcmp(v->key, "host_name")) {
			host_name = v->value;
		} else if (!strcmp(v->key, "contacts")) {
			contacts = v->value;
		} else if (!strcmp(v->key, "contact_groups")) {
			groups = v->value;
		}
	}

	if ((contacts && list_has_entry(contacts, user)) ||
		(groups && list_has_any_entry(groups, cgroups, cgroup_idx)))
	{
		hash_add(auth_hosts, host_name, (void *)user);
	}

	if (!host_name)
		return -1;

	return 0;
}

static int grok_service(struct cfg_comp *obj)
{
	uint i;
	char *host_name = NULL;
	char *contacts = NULL;
	char *groups = NULL;
	char *service_description = NULL;

	for (i = 0; i < obj->vars; i++) {
		struct cfg_var *v = obj->vlist[i];
		if (!strcmp(v->key, "host_name")) {
			host_name = v->value;
		} else if (!strcmp(v->key, "contacts")) {
			contacts = v->value;
		} else if (!strcmp(v->key, "contact_groups")) {
			groups = v->value;
		} else if (!strcmp(v->key, "service_description")) {
			service_description = v->value;
		}
	}

	if ((contacts && list_has_entry(contacts, user)) ||
		(groups && list_has_any_entry(groups, cgroups, cgroup_idx)))
	{
		hash_add2(auth_services, host_name, service_description, (void *)user);
	}

	return 0;
}

static int grok_contactgroup(struct cfg_comp *obj)
{
	uint i;
	char *name = NULL;
	char *members = NULL;

	for (i = 0; i < obj->vars; i++) {
		struct cfg_var *v = obj->vlist[i];
		if (!strcmp(v->key, "contactgroup_name")) {
			name = v->value;
		} else if (!strcmp(v->key, "members")) {
			members = v->value;
		}
	}

	if (list_has_entry(members, user)) {
		if (cgroup_idx >= num_cgroups - 1) {
			cgroups = realloc(cgroups, (num_cgroups + 5) * sizeof(char *));
			if (!cgroups) {
				printf("Failed to realloc(cgroups)\n");
				exit(1);
			}
			num_cgroups += 5;
		}
		cgroups[cgroup_idx++] = name;
	}

	return 0;
}

static int grok_object(struct cfg_comp *conf, const char *str, int (*handler)(struct cfg_comp *))
{
	uint i;

	for (i = 0; i < conf->nested; i++) {
		struct cfg_comp *obj;
		obj = conf->nest[i];
		if (!strcmp(obj->name, str)) {
			handler(obj);
		}
	}

	return 0;
}

void auth_set_user(const char *username)
{
	user = username;
}

const char *auth_get_user(void)
{
	return user;
}

void auth_parse_permission(const char *key, const char *value)
{
	int val;

	if (!user || prefixcmp(key, "authorized_for_"))
		return;

	val = list_has_entry(value, user) | list_has_entry(value, "*");
	if (!prefixcmp(key, "authorized_for_all_services")) {
		is_service_root = val;
	}
	if (!prefixcmp(key, "authorized_for_all_hosts")) {
		is_host_root = val;
		is_service_root = val;
	}
}

static struct cfg_comp *conf;
int auth_init(const char *path)
{
	int host_buckets, service_buckets;

	/*
	 * if the user can see all hosts (and thus all services),
	 * we don't need to read the configuration
	 */
	if (is_host_root)
		return 0;

	cgroups = calloc(20, sizeof(char *));
	if (cgroups) {
		num_cgroups = 20;
	}

	conf = cfg_parse_file(path);
	if (!conf) {
		printf("Failed to parse %s for some reason\n", path);
		exit(1);
	}

	grok_object(conf, "define contactgroup", grok_contactgroup);
	if (!is_host_root) {
		host_buckets = conf->nested / 10;
		auth_hosts = hash_init(host_buckets);
		grok_object(conf, "define host", grok_host);
	}

	if (!is_service_root) {
		service_buckets = conf->nested / 2;
		auth_services = hash_init(service_buckets);
		grok_object(conf, "define service", grok_service);
	}
	return 0;
}

void auth_deinit(void)
{
	if (conf)
		cfg_destroy_compound(conf);

	if (auth_hosts)
		free(auth_hosts);
	if (auth_services)
		free(auth_services);
	if (cgroups)
		free(cgroups);
}
