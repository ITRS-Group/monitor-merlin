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

static inline void nsplit_cache_command(struct command *cmd)
{
	if (!cmd || bitmap_isset(map.commands, cmd->id))
		return;

	fcache_command(fp, cmd);
	bitmap_set(map.commands, cmd->id);
}

static int set_hostgroup_host(void *_hst, __attribute__((unused)) void *user_data)
{
	bitmap_set(map.hosts, ((host *)_hst)->id);
	return 0;
}

static int map_hostgroup_hosts(const char *hg_name)
{
	struct hostgroup *hg;

	if (!(hg = find_hostgroup(hg_name))) {
		printf("Failed to locate hostgroup '%s'\n", hg_name);
		return -1;
	}
	rbtree_traverse(hg->members, set_hostgroup_host, NULL, rbinorder);
	return 0;
}

static inline void nsplit_cache_timeperiod(struct timeperiod *tp)
{
	if (tp && !bitmap_isset(map.timeperiods, tp->id)) {
		struct timeperiodexclusion *exc;
		bitmap_set(map.timeperiods, tp->id);
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
		fcache_contact(fp, c);
	}
}

static void nsplit_cache_contactgroups(contactgroupsmember *cm)
{
	for (; cm; cm = cm->next) {
		struct contactgroup *cg = cm->group_ptr;
		if (bitmap_isset(map.contactgroups, cg->id))
			continue;
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


static int copy_relevant_parents(void *_parent, void *_duplicate)
{
	host *parent = (host *)_parent;
	host *duplicate = (host *)_duplicate;
	if (bitmap_isset(map.hosts, parent->id))
		add_parent_to_host(duplicate, parent);
	return 0;
}

static void nsplit_cache_host(struct host *h)
{
	struct host *tmphst;
	struct servicesmember *sm, *sp;
	struct contactsmember *cm;
	struct contactgroupsmember *cgm;
	struct customvariablesmember *cvar;
	objectlist *olist;

	if (bitmap_isset(htrack, h->id)) {
		return;
	}
	bitmap_set(htrack, h->id);
	nsplit_cache_slaves(h);

	tmphst = create_host(h->name, h->display_name, h->alias, h->address, h->check_period, h-> initial_state, h->check_interval, h->retry_interval, h->max_attempts, h->notification_options, h->notification_interval, h->first_notification_delay, h->notification_period, h->notifications_enabled, h->check_command, h->checks_enabled, h->accept_passive_checks, h->event_handler, h->event_handler_enabled, h->flap_detection_enabled, h->low_flap_threshold, h->high_flap_threshold, h->flap_detection_options, h->stalking_options, h->process_performance_data, h->check_freshness, h->freshness_threshold, h->notes, h->notes_url, h->action_url, h->icon_image, h->icon_image_alt, h->vrml_image, h->statusmap_image, h->x_2d, h->y_2d, h->have_2d_coords, h->x_3d, h->y_3d, h->z_3d, h->have_3d_coords, h->should_be_drawn, h->retain_status_information, h->retain_nonstatus_information, h->obsess, h->hourly_value);

	rbtree_traverse(h->parent_hosts, copy_relevant_parents, tmphst, rbinorder);
	for (cm = h->contacts; cm; cm = cm->next)
		add_contact_to_host(tmphst, cm->contact_name);
	for (cgm = h->contact_groups; cgm; cgm = cgm->next)
		add_contactgroup_to_host(tmphst, cgm->group_name);
	for (cvar = h->custom_variables; cvar; cvar = cvar->next)
		add_custom_variable_to_host(tmphst, cvar->variable_name, cvar->variable_value);

	fcache_host(fp, tmphst);
	nsplit_cache_hostdependencies(h->exec_deps);
	nsplit_cache_hostdependencies(h->notify_deps);

	for (olist = h->escalation_list; olist; olist = olist->next) {
		struct hostescalation *he = (struct hostescalation *)olist->object_ptr;
		nsplit_cache_timeperiod(he->escalation_period_ptr);
		nsplit_cache_contactgroups(he->contact_groups);
		nsplit_cache_contacts(he->contacts);
		fcache_hostescalation(fp, he);
	}

	for (sm = h->services; sm; sm = sm->next) {
		struct service *s = sm->service_ptr;
		struct service *tmpsvc = create_service(s->host_name, s->description, s->display_name, s->check_period, s->initial_state, s->max_attempts, s->accept_passive_checks, s->check_interval, s->retry_interval, s->notification_interval, s->first_notification_delay, s->notification_period, s->notification_options, s->notifications_enabled, s->is_volatile, s->event_handler, s->event_handler_enabled, s->check_command, s->checks_enabled, s->flap_detection_enabled, s->low_flap_threshold, s->high_flap_threshold, s->flap_detection_options, s->stalking_options, s->process_performance_data, s->check_freshness, s->freshness_threshold, s->notes, s->notes_url, s->action_url, s->icon_image, s->icon_image_alt, s->retain_status_information, s->retain_nonstatus_information, s->obsess, s->hourly_value);
		for (cm = s->contacts; cm; cm = cm->next)
			add_contact_to_service(tmpsvc, cm->contact_name);
		for (cgm = s->contact_groups; cgm; cgm = cgm->next)
			add_contactgroup_to_service(tmpsvc, cgm->group_name);
		for (cvar = s->custom_variables; cvar; cvar = cvar->next)
			add_custom_variable_to_service(tmpsvc, cvar->variable_name, cvar->variable_value);
		nsplit_cache_slaves(tmpsvc);
		/* remove cross-host service parents, if any */
		for (sp = s->parents; sp; sp = sp->next) {
			if (bitmap_isset(map.hosts, sp->service_ptr->host_ptr->id))
				add_parent_service_to_service(tmpsvc, sp->service_ptr->host_name, sp->service_ptr->description);
		}
		fcache_service(fp, s);
		nsplit_cache_servicedependencies(s->exec_deps);
		nsplit_cache_servicedependencies(s->notify_deps);
		for (olist = s->escalation_list; olist; olist = olist->next) {
			struct serviceescalation *se = (struct serviceescalation *)olist->object_ptr;
			nsplit_cache_timeperiod(se->escalation_period_ptr);
			nsplit_cache_contactgroups(se->contact_groups);
			nsplit_cache_contacts(se->contacts);
			fcache_serviceescalation(fp, se);
		}
		destroy_service(tmpsvc);
	}
	destroy_host(tmphst);
}

static int partial_hostgroup(void *_hst, void *user_data)
{
	hostgroup *tmphg = (hostgroup *)user_data;
	host *hst = (host *)_hst;
	if (bitmap_isset(map.hosts, hst->id))
		add_host_to_hostgroup(tmphg, hst);
	return 0;
}

static int nsplit_partial_groups(void)
{
	struct hostgroup *hg;
	struct servicegroup *sg;

	for (hg = hostgroup_list; hg; hg = hg->next) {
		struct hostgroup *tmphg;

		if (bitmap_isset(map.hostgroups, hg->id)) {
			continue;
		}
		tmphg = create_hostgroup(hg->group_name, hg->alias, hg->notes, hg->notes_url, hg->action_url);
		rbtree_traverse(tmphg->members, partial_hostgroup, tmphg, rbinorder);
		if (tmphg->members) {
			fcache_hostgroup(fp, tmphg);
		}
		destroy_hostgroup(tmphg);
	}

	for (sg = servicegroup_list; sg; sg = sg->next) {
		struct servicesmember *sm;
		struct servicegroup *tmpsg;
		tmpsg = create_servicegroup(sg->group_name, sg->alias, sg->notes, sg->notes_url, sg->action_url);
		for (sm = sg->members; sm; sm = sm->next) {
			if (bitmap_isset(map.hosts, sm->service_ptr->host_ptr->id)) {
				add_service_to_servicegroup(tmpsg, sm->host_name, sm->service_description);
			}
		}
		if (sg->members) {
			fcache_servicegroup(fp, tmpsg);
		}
		destroy_servicegroup(tmpsg);
	}
	return 0;
}

static int nsplit_cache_host_in_group(void *hst, __attribute__((unused)) void *user_data)
{
	nsplit_cache_host((host *)hst);
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
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		hg = find_hostgroup(grp);
		fcache_hostgroup(fp, hg);
		bitmap_set(map.hostgroups, hg->id);
		rbtree_traverse(hg->members, nsplit_cache_host_in_group, NULL, rbinorder);
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
