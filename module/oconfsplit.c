#include "shared.h"
#include "misc.h"
#include "logging.h"
#include "ipc.h"
#include <naemon/naemon.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>
#include <libgen.h>
#include <glib.h>
#include <stdbool.h>

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

static char *poller_config_dir = NULL;

void split_init(void) {
	nm_asprintf(&poller_config_dir, "%s/config/", CACHEDIR);
}

void split_deinit(void) {
	nm_free(poller_config_dir);
	poller_config_dir=NULL;
}

int split_grok_var(const char *var, const char *value) {
	if(0 == strcmp("oconfsplit_dir", var)) {
		char *cfgdir;

		nm_free(poller_config_dir);

		cfgdir = nspath_absolute(merlin_config_file, NULL);
		dirname(cfgdir);
		poller_config_dir = nspath_absolute(value, cfgdir);
		nm_free(cfgdir);
		return 1;
	}
	return 0;
}

static inline void nsplit_cache_command(struct command *cmd)
{
	if (!cmd || bitmap_isset(map.commands, cmd->id))
		return;

	fcache_command(fp, cmd);
	bitmap_set(map.commands, cmd->id);
}

static gboolean set_hostgroup_host(gpointer _name, gpointer _hst, __attribute__((unused)) gpointer user_data)
{
	bitmap_set(map.hosts, ((host *)_hst)->id);
	return FALSE;
}

static int map_hostgroup_hosts(const char *hg_name)
{
	struct hostgroup *hg;

	if (!(hg = find_hostgroup(hg_name))) {
		printf("Failed to locate hostgroup '%s'\n", hg_name);
		return -1;
	}
	g_tree_foreach(hg->members, set_hostgroup_host, NULL);
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


static gboolean copy_relevant_parents(gpointer _name, gpointer _parent, gpointer _duplicate)
{
	host *parent = (host *)_parent;
	host *duplicate = (host *)_duplicate;
	if (bitmap_isset(map.hosts, parent->id)) {
		/* We only want single direction reference while exporting. We remove manually later */
		g_tree_insert(duplicate->parent_hosts, g_strdup(parent->name), parent);
	}
	return FALSE;
}

static gboolean nsplit_cache_host(gpointer _name, gpointer _hst, __attribute__((unused)) gpointer user_data)
{
	struct host const *h = (struct host *)_hst;
	struct host *tmphst;
	struct servicesmember *sm, *sp;
	struct contactsmember *cm;
	struct contactgroupsmember *cgm;
	struct customvariablesmember *cvar;
	objectlist *olist;

	if (bitmap_isset(htrack, h->id)) {
		return FALSE;
	}
	bitmap_set(htrack, h->id);
	nsplit_cache_slaves(h);

	tmphst = create_host(h->name);
	setup_host_variables(tmphst, h->display_name, h->alias, h->address, h->check_period, h-> initial_state, h->check_interval, h->retry_interval, h->max_attempts, h->notification_options, h->notification_interval, h->first_notification_delay, h->notification_period, h->notifications_enabled, h->check_command, h->checks_enabled, h->accept_passive_checks, h->event_handler, h->event_handler_enabled, h->flap_detection_enabled, h->low_flap_threshold, h->high_flap_threshold, h->flap_detection_options, h->stalking_options, h->process_performance_data, h->check_freshness, h->freshness_threshold, h->notes, h->notes_url, h->action_url, h->icon_image, h->icon_image_alt, h->vrml_image, h->statusmap_image, h->x_2d, h->y_2d, h->have_2d_coords, h->x_3d, h->y_3d, h->z_3d, h->have_3d_coords, h->retain_status_information, h->retain_nonstatus_information, h->obsess, h->hourly_value);

	g_tree_foreach(h->parent_hosts, copy_relevant_parents, tmphst);
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
		struct service *tmpsvc = create_service(tmphst, s->description);
		setup_service_variables(tmpsvc, s->display_name, s->check_command, s->check_period, s->initial_state, s->max_attempts, s->accept_passive_checks, s->check_interval, s->retry_interval, s->notification_interval, s->first_notification_delay, s->notification_period, s->notification_options, s->notifications_enabled, s->is_volatile, s->event_handler, s->event_handler_enabled, s->checks_enabled, s->flap_detection_enabled, s->low_flap_threshold, s->high_flap_threshold, s->flap_detection_options, s->stalking_options, s->process_performance_data, s->check_freshness, s->freshness_threshold, s->notes, s->notes_url, s->action_url, s->icon_image, s->icon_image_alt, s->retain_status_information, s->retain_nonstatus_information, s->obsess, s->hourly_value);
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
				add_parent_to_service(tmpsvc, sp->service_ptr);
		}
		fcache_service(fp, s);
		nsplit_cache_slaves(s);
		nsplit_cache_servicedependencies(s->exec_deps);
		nsplit_cache_servicedependencies(s->notify_deps);
		for (olist = s->escalation_list; olist; olist = olist->next) {
			struct serviceescalation *se = (struct serviceescalation *)olist->object_ptr;
			nsplit_cache_timeperiod(se->escalation_period_ptr);
			nsplit_cache_contactgroups(se->contact_groups);
			nsplit_cache_contacts(se->contacts);
			fcache_serviceescalation(fp, se);
		}
		destroy_service(tmpsvc, FALSE);
	}
	/* This is a temporary host, which doesn't have back references... ugh */
	if (tmphst->parent_hosts != NULL) {
		g_tree_unref(tmphst->parent_hosts);
		tmphst->parent_hosts = NULL;
	}
	if (tmphst->child_hosts != NULL) {
		g_tree_unref(tmphst->child_hosts);
		tmphst->child_hosts = NULL;
	}
	free_objectlist(&tmphst->hostgroups_ptr);
	destroy_host(tmphst);
	return FALSE;
}

static gboolean partial_hostgroup(gpointer _name, gpointer _hst, gpointer user_data)
{
	hostgroup *tmphg = (hostgroup *)user_data;
	host *hst = (host *)_hst;
	if (bitmap_isset(map.hosts, hst->id)) {
		/* We only want single direction reference while exporting. We remove manually later */
		g_tree_insert(tmphg->members, g_strdup(hst->name), hst);
	}
	return FALSE;
}

void servicegroup_member_add(service *svc, servicegroup *sg)
{
	add_service_to_servicegroup(sg, svc);
}

int service_cmp(service *a, service *b)
{
	int val;

	assert(a);
	assert(a->host_name);
	assert(a->description);
	assert(b);
	assert(b->host_name);
	assert(b->description);

	/* Sort by hostname first since format of config is hostname first */
	if ((val = strcmp(b->host_name, a->host_name)) == 0) {
		/* The compared services are on the same host, order by description */
		return strcmp(b->description, a->description);
	}
	return val;
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
		g_tree_foreach(hg->members, partial_hostgroup, tmphg);
		if (g_tree_nnodes(tmphg->members) > 0) {
			fcache_hostgroup(fp, tmphg);
		}

		/* Since we only have single direction memberships in the temporary groups, remove without removing back refs */
		g_tree_unref(tmphg->members);
		tmphg->members = NULL;

		/* Destroy host group */
		destroy_hostgroup(tmphg);
	}

	for (sg = servicegroup_list; sg; sg = sg->next) {
		GList *members_list = NULL;
		struct servicesmember *sm;
		struct servicegroup *tmpsg;
		tmpsg = create_servicegroup(sg->group_name, sg->alias, sg->notes, sg->notes_url, sg->action_url);

		/* Add all service group members to a list so we can easily sort them */
		for (sm = sg->members; sm; sm = sm->next) {
			if (bitmap_isset(map.hosts, sm->service_ptr->host_ptr->id)) {
				members_list = g_list_prepend(members_list, sm->service_ptr);
			}
		}
		members_list = g_list_sort(members_list, (GCompareFunc)service_cmp);
		g_list_foreach(members_list, (GFunc)servicegroup_member_add, tmpsg);

		if (tmpsg->members) {
			fcache_servicegroup(fp, tmpsg);
		}

		destroy_servicegroup(tmpsg, FALSE);
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
		if ((comma = strchr(grp, ',')))
			* comma = 0;
		hg = find_hostgroup(grp);
		fcache_hostgroup(fp, hg);
		bitmap_set(map.hostgroups, hg->id);
		g_tree_foreach(hg->members, nsplit_cache_host, NULL);
	} while (grp);

	return 0;
}

int split_config(void)
{
	unsigned int i;

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
		char *outfile, *tmp_file;
		int fd;
		struct timeval times[2] = {{0,0}, {0,0}};
		blk_SHA_CTX ctx;

		node = poller_table[i];
		if (asprintf(&tmp_file, "%s%s.cfg.XXXXXX", poller_config_dir, node->name) == -1) {
			lerr("Cannot nodesplit: there was an error generating temporary file name: %s", strerror(errno));
			continue;
		}
		if (asprintf(&outfile, "%s%s.cfg", poller_config_dir, node->name) == -1) {
			lerr("Cannot nodesplit: there was an error generating file name: %s", strerror(errno));
			continue;
		}
		fd = mkstemp(tmp_file);
		if (fd < 0) {
			lerr("Cannot nodesplit: Failed to create temporary file '%s' for writing: %s", tmp_file, strerror(errno));
			continue;
		}
		fp = fdopen(fd, "r+");
		if (!fp) {
			lerr("Cannot nodesplit: Failed to open '%s' for writing: %s", tmp_file, strerror(errno));
			continue;
		}
		linfo("OCONFSPLIT: Writing config for poller %s to '%s'\n", node->name, outfile);

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
		if (host_perfdata_command)
			nsplit_cache_command(find_command(host_perfdata_command));
		if (service_perfdata_command)
			nsplit_cache_command(find_command(service_perfdata_command));
		if (host_perfdata_file_processing_command)
			nsplit_cache_command(find_command(host_perfdata_file_processing_command));
		if (service_perfdata_file_processing_command)
			nsplit_cache_command(find_command(service_perfdata_file_processing_command));

		if (nsplit_cache_stuff(groups) < 0) {
			lerr("Caching for %s failed. Skipping", node->name);
			continue;
		}
		nsplit_partial_groups();
		fclose(fp);
		if (rename(tmp_file, outfile)) {
			lerr("Cannot nodesplit: Failed to create '%s' from temporary file %s: %s", outfile, tmp_file, strerror(errno));
			continue;
		}
		times[0].tv_sec = times[1].tv_sec = ipc.info.last_cfg_change;
		if (utimes(outfile, times) == -1) {
			lerr("Error in nodesplit: Failed to set mtime of '%s': %s", outfile, strerror(errno));
			continue;
		}

		blk_SHA1_Init(&ctx);
		hash_add_file(outfile, &ctx);
		blk_SHA1_Final(node->expected.config_hash, &ctx);
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
