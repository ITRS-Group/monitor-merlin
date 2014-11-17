#include "shared.h"
#include "misc.h"
#include "logging.h"
#include "ipc.h"
#include <naemon/naemon.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <libgen.h>

static struct {
	bitmap *hosts;
	bitmap *commands;
	bitmap *contactgroups;
	bitmap *contacts;
	bitmap *timeperiods;
	/* for including partial groups */
	bitmap *servicegroups;
	bitmap *hostgroups;
} map;

bitmap *htrack; /* tracks hosts passed to fcache_host() */

static FILE *fp;
static struct object_count cached, partial;

/* remove code for simple lists with only a *next pointer */
#define nsplit_slist_remove(LSTART, LENTRY, LNEXT, LPREV) \
	do { \
		if (LPREV) \
			LPREV->next = LNEXT; \
		else \
			LSTART = LNEXT; \
		free(LENTRY); \
	} while (0)

static inline void nsplit_cache_command(struct command *cmd)
{
	if (!cmd || bitmap_isset(map.commands, cmd->id))
		return;

	cached.commands++;
	fcache_command(fp, cmd);
	bitmap_set(map.commands, cmd->id);
}

static int map_hostgroup_hosts(const char *hg_name)
{
	struct hostgroup *hg;
	struct hostsmember *m;

	if (!(hg = find_hostgroup(hg_name))) {
		printf("Failed to locate hostgroup '%s'\n", hg_name);
		return -1;
	}
	for (m = hg->members; m; m = m->next) {
		struct host *h = m->host_ptr;
		bitmap_set(map.hosts, h->id);
	}
	return 0;
}

static inline void nsplit_cache_timeperiod(struct timeperiod *tp)
{
	if (tp && !bitmap_isset(map.timeperiods, tp->id)) {
		struct timeperiodexclusion *exc;
		bitmap_set(map.timeperiods, tp->id);
		cached.timeperiods++;
		fcache_timeperiod(fp, tp);
		for (exc = tp->exclusions; exc; exc = exc->next) {
			nsplit_cache_timeperiod(exc->timeperiod_ptr);
		}
	}
}

static inline void nsplit_cache_hostdependencies(objectlist *olist)
{
	objectlist *list;

	for (list = olist; list; list = list->next) {
		struct hostdependency *dep = (struct hostdependency *)list->object_ptr;
		if (bitmap_isset(map.hosts, dep->master_host_ptr->id)) {
			nsplit_cache_timeperiod(dep->dependency_period_ptr);
			cached.hostdependencies++;
			fcache_hostdependency(fp, dep);
		}
	}
}

static inline void nsplit_cache_servicedependencies(objectlist *olist)
{
	objectlist *list;
	for (list = olist; list; list = list->next) {
		struct servicedependency *dep = (struct servicedependency *)list->object_ptr;
		if (!bitmap_isset(map.hosts, dep->master_service_ptr->host_ptr->id))
			continue;
		cached.servicedependencies++;
		nsplit_cache_timeperiod(dep->dependency_period_ptr);
		fcache_servicedependency(fp, dep);
	}
}

static void nsplit_cache_contacts(struct contactsmember *cm_list)
{
	struct contactsmember *cm;

	for (cm = cm_list; cm; cm = cm->next) {
		struct commandsmember *cmdm;
		struct contact *c = cm->contact_ptr;

		if (bitmap_isset(map.contacts, c->id))
			continue;
		nsplit_cache_timeperiod(c->host_notification_period_ptr);
		nsplit_cache_timeperiod(c->service_notification_period_ptr);
		for (cmdm = c->host_notification_commands; cmdm; cmdm = cmdm->next) {
			nsplit_cache_command(cmdm->command_ptr);
		}
		for (cmdm = c->service_notification_commands; cmdm; cmdm = cmdm->next) {
			nsplit_cache_command(cmdm->command_ptr);
		}
		bitmap_set(map.contacts, c->id);
		cached.contacts++;
		fcache_contact(fp, c);
	}
}

static void nsplit_cache_contactgroups(contactgroupsmember *cm)
{
	for (; cm; cm = cm->next) {
		struct contactgroup *cg = cm->group_ptr;
		if (bitmap_isset(map.contactgroups, cg->id))
			continue;
		cached.contactgroups++;
		nsplit_cache_contacts(cg->members);
		bitmap_set(map.contactgroups, cg->id);
		fcache_contactgroup(fp, cg);
	}
}

/*
 * hosts and services share a bunch of objects. we track them
 * here. Unfortunately, dependencies and escalations are still
 * separate object types, so those can't be included here
 */
#define nsplit_cache_slaves(o) \
	do { \
		nsplit_cache_command(o->event_handler_ptr); \
		nsplit_cache_command(o->check_command_ptr); \
		nsplit_cache_timeperiod(o->check_period_ptr); \
		nsplit_cache_timeperiod(o->notification_period_ptr); \
		nsplit_cache_contactgroups(o->contact_groups); \
		nsplit_cache_contacts(o->contacts); \
	} while (0)



static void nsplit_cache_host(struct host *h)
{
	struct hostsmember *parent, *next, *prev = NULL;
	struct servicesmember *sm, *sp, *sp_prev, *sp_next;
	objectlist *olist;

	if (bitmap_isset(htrack, h->id)) {
		return;
	}
	bitmap_set(htrack, h->id);
	nsplit_cache_slaves(h);

	/* massage the parent list */
	for (parent = h->parent_hosts; parent; parent = next) {
		next = parent->next;
		if (bitmap_isset(map.hosts, parent->host_ptr->id)) {
			prev = parent;
			continue;
		}
		free(parent->host_name);
		free(parent);
		if (prev)
			prev->next = next;
		else {
			h->parent_hosts = next;
		}
	}
	cached.hosts++;
	fcache_host(fp, h);
	nsplit_cache_hostdependencies(h->exec_deps);
	nsplit_cache_hostdependencies(h->notify_deps);

	for (olist = h->escalation_list; olist; olist = olist->next) {
		struct hostescalation *he = (struct hostescalation *)olist->object_ptr;
		nsplit_cache_timeperiod(he->escalation_period_ptr);
		nsplit_cache_contactgroups(he->contact_groups);
		nsplit_cache_contacts(he->contacts);
		cached.hostescalations++;
		fcache_hostescalation(fp, he);
	}

	for (sm = h->services; sm; sm = sm->next) {
		struct service *s = sm->service_ptr;
		nsplit_cache_slaves(s);
		/* remove cross-host service parents, if any */
		for (sp_prev = NULL, sp = s->parents; sp; sp_prev = sp, sp = sp_next) {
			sp_next = sp->next;
			if (!bitmap_isset(map.hosts, sp->service_ptr->host_ptr->id))
				nsplit_slist_remove(s->parents, sp, sp_next, sp_prev);
		}
		cached.services++;
		fcache_service(fp, s);
		nsplit_cache_servicedependencies(s->exec_deps);
		nsplit_cache_servicedependencies(s->notify_deps);
		for (olist = s->escalation_list; olist; olist = olist->next) {
			struct serviceescalation *se = (struct serviceescalation *)olist->object_ptr;
			nsplit_cache_timeperiod(se->escalation_period_ptr);
			nsplit_cache_contactgroups(se->contact_groups);
			nsplit_cache_contacts(se->contacts);
			cached.serviceescalations++;
			fcache_serviceescalation(fp, se);
		}
	}
}

static int nsplit_partial_groups(void)
{
	struct hostgroup *hg;
	struct servicegroup *sg;

	for (hg = hostgroup_list; hg; hg = hg->next) {
		struct hostsmember *hm, *prev = NULL, *next;
		int removed = 0;

		if (bitmap_isset(map.hostgroups, hg->id)) {
			continue;
		}
		for (hm = hg->members; hm; hm = next) {
			next = hm->next;
			if (bitmap_isset(map.hosts, hm->host_ptr->id)) {
				prev = hm;
				continue;
			}
			/* not a tracked host. Remove it */
			removed++;
			if (prev)
				prev->next = next;
			else
				hg->members = next;
			free(hm);
		}
		if (hg->members) {
			if (removed)
				partial.hostgroups++;
			else
				cached.hostgroups++;
			fcache_hostgroup(fp, hg);
		}
	}

	for (sg = servicegroup_list; sg; sg = sg->next) {
		struct servicesmember *sm, *prev = NULL, *next;
		int removed = 0;
		for (sm = sg->members; sm; sm = next) {
			next = sm->next;
			if (bitmap_isset(map.hosts, sm->service_ptr->host_ptr->id)) {
				prev = sm;
				continue;
			}
			if (prev)
				prev->next = next;
			else
				sg->members = next;
			free(sm);
		}
		if (sg->members) {
			if (removed)
				partial.servicegroups++;
			else
				cached.servicegroups++;
			fcache_servicegroup(fp, sg);
		}
	}
	return 0;
}

static int nsplit_cache_stuff(const char *orig_groups)
{
	int ngroups = 0;
	char *groups, *comma, *grp;

	if (!orig_groups)
		return EXIT_FAILURE;

	grp = groups = strdup(orig_groups);
	for (grp = groups; grp != NULL; grp = comma ? comma + 1 : NULL) {
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		ngroups++;
		if (map_hostgroup_hosts(grp) < 0)
			return -1;

		/* restore the string so we can iterate it once more */
		if (comma)
			*comma = ',';
	} while (grp);

	/* from here on out, all hosts are tagged. */
	for (grp = groups; grp != NULL; grp = comma ? comma + 1 : NULL) {
		struct hostgroup *hg;
		struct hostsmember *m;
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		hg = find_hostgroup(grp);
		cached.hostgroups++;
		fcache_hostgroup(fp, hg);
		bitmap_set(map.hostgroups, hg->id);
		for (m = hg->members; m; m = m->next) {
			nsplit_cache_host(m->host_ptr);
		}
	} while (grp);

	return 0;
}

int split_config(void)
{
	int i;

	char *groups = NULL;
	merlin_node *node;

	/* create our tracker maps */
	htrack = bitmap_create(num_objects.hosts);
	map.hosts = bitmap_create(num_objects.hosts);
	map.commands = bitmap_create(num_objects.commands);
	map.timeperiods = bitmap_create(num_objects.timeperiods);
	map.contacts = bitmap_create(num_objects.contacts);
	map.contactgroups = bitmap_create(num_objects.contactgroups);
	map.hostgroups = bitmap_create(num_objects.hostgroups);

	for (i = 0; i < num_pollers; i++) {
		char *outfile, *tmpfile;
		int fd;
		node = poller_table[i];
		if (asprintf(&tmpfile, "%s/config/%s.cfg.XXXXXX", CACHEDIR, node->name) == -1) {
			lerr("Cannot nodesplit: there was an error generating temporary file name: %s", strerror(errno));
			continue;
		}
		if (asprintf(&outfile, "%s/config/%s.cfg", CACHEDIR, node->name) == -1) {
			lerr("Cannot nodesplit: there was an error generating file name: %s", strerror(errno));
			continue;
		}
		fd = mkstemp(tmpfile);
		if (!fd) {
			lerr("Cannot nodesplit: Failed to create temporary file '%s' for writing: %s", tmpfile, strerror(errno));
			continue;
		}
		fp = fdopen(fd, "r+");
		if (!fp) {
			lerr("Cannot nodesplit: Failed to open '%s' for writing: %s", tmpfile, strerror(errno));
			continue;
		}

		groups = node->hostgroups;

		bitmap_clear(htrack);
		bitmap_clear(map.hosts);
		bitmap_clear(map.commands);
		bitmap_clear(map.timeperiods);
		bitmap_clear(map.contacts);
		bitmap_clear(map.contactgroups);
		bitmap_clear(map.hostgroups);

		/* global commands are always included */
		nsplit_cache_command(ochp_command_ptr);
		nsplit_cache_command(ocsp_command_ptr);
		nsplit_cache_command(global_host_event_handler_ptr);
		nsplit_cache_command(global_service_event_handler_ptr);
		nsplit_cache_command(find_command(host_perfdata_command));
		nsplit_cache_command(find_command(service_perfdata_command));
		nsplit_cache_command(find_command(host_perfdata_file_processing_command));
		nsplit_cache_command(find_command(service_perfdata_file_processing_command));

		if (nsplit_cache_stuff(groups) < 0) {
			lerr("Caching for %s failed. Skipping", node->name);
			continue;
		}
		nsplit_partial_groups();
		fclose(fp);
		if (rename(tmpfile, outfile)) {
			lerr("Cannot nodesplit: Failed to create '%s' from temporary file %s: %s", outfile, tmpfile, strerror(errno));
			continue;
		}
		const struct timeval times[2] = {{ipc.info.last_cfg_change, 0}, {ipc.info.last_cfg_change, 0}};
		if (utimes(outfile, times) == -1) {
			lerr("Error in nodesplit: Failed to set mtime of '%s': %s", outfile, strerror(errno));
			continue;
		}
	}

	bitmap_destroy(htrack);
	bitmap_destroy(map.hosts);
	bitmap_destroy(map.commands);
	bitmap_destroy(map.timeperiods);
	bitmap_destroy(map.contacts);
	bitmap_destroy(map.contactgroups);
	bitmap_destroy(map.hostgroups);

	return OK;
}
