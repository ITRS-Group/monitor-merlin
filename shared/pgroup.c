#include <stdio.h>
#include <glib.h>

#include "logging.h"
#include "pgroup.h"
#include "node.h"
#include "ipc.h"
#include "shared.h"

/* Tables to locate the correct peer-group by object id */
static merlin_peer_group **host_id2pg;
static merlin_peer_group **service_id2pg;

static merlin_peer_group **peer_group;
static unsigned int num_peer_groups;
bitmap *poller_handled_hosts = NULL;
bitmap *poller_handled_services = NULL;

static void pgroup_reassign_checks(void)
{
	unsigned int i, x;

	/* "our" pgroup handles all checks by default */
	ipc.pgroup->assigned.hosts = num_objects.hosts;
	ipc.pgroup->assigned.services = num_objects.services;

	/* first reset top-level hosts */
	ldebug("Reassigning checks for group ipc");
	for (i = 0; i < ipc.pgroup->active_nodes; i++) {
		merlin_node *node = ipc.pgroup->nodes[i];
		node->assigned.extra.hosts = node->assigned.extra.services = 0;
		node->assigned.current.hosts =
			ipc.pgroup->assign[ipc.pgroup->active_nodes - 1][node->peer_id].hosts;
		node->assigned.current.services =
			ipc.pgroup->assign[ipc.pgroup->active_nodes - 1][node->peer_id].services;
	}
	for (i = 1; i < num_peer_groups; i++) {
		int active;
		merlin_peer_group *pg = peer_group[i];

		active = pg->active_nodes;
		ldebug("Reassigning for peer group %d with %d active nodes",
			   pg->id, active);

		if (!active) {
			if (!(pg->flags & MERLIN_NODE_TAKEOVER)) {
				/* not supposed to reassign checks to "ipc" here */
				continue;
			}
			for (x = 0; x < ipc.pgroup->active_nodes; x++) {
				merlin_node *node = ipc.pgroup->nodes[x];
				ldebug("Dealing with node %s", node->name);
				if (node->state != STATE_CONNECTED)
					continue;
				node->assigned.extra.hosts +=
					pg->inherit[ipc.pgroup->active_nodes - 1][node->peer_id].hosts;
				node->assigned.extra.services +=
					pg->inherit[ipc.pgroup->active_nodes - 1][node->peer_id].services;
				ldebug("  done. Moving on to next node");
			}
			continue;
		}

		ldebug("Peer group is active. Neato");
		/* now deduct the objects from the top-level pgroup */
		ipc.pgroup->assigned.hosts -= pg->assigned.hosts;
		ipc.pgroup->assigned.services -= pg->assigned.services;
		for (x = 0; x < pg->total_nodes; x++) {
			merlin_node *node = pg->nodes[x];

			if (node->state != STATE_CONNECTED) {
				node->assigned.current.hosts = 0;
				node->assigned.current.services = 0;
				continue;
			}
			node->assigned.current.hosts = pg->assign[active - 1][node->peer_id].hosts;
			node->assigned.current.services = pg->assign[active - 1][node->peer_id].services;
		}
	}
}

static int timeval_comp(const struct timeval *a, const struct timeval *b)
{
	if (a == b)
		return 0;

	if (a->tv_sec == b->tv_sec)
		return a->tv_usec - b->tv_usec;

	return a->tv_sec - b->tv_sec;
}

static int cmp_peer(const void *a_, const void *b_)
{
	const merlin_node *a = *(const merlin_node **)a_;
	const merlin_node *b = *(const merlin_node **)b_;

	/* make sure disconnected nodes are sorted last */
	if (a->state != b->state) {
		if (a->state == STATE_CONNECTED)
			return -1;
		if (b->state == STATE_CONNECTED)
			return 1;
	}

	/*
	 * also make sure nodes that haven't sent a CTRL_ACTIVE
	 * are sorted after the ones that have, and discarded in
	 * the id assignment dance
	 */
	if (a->info.start.tv_sec && !b->info.start.tv_sec)
		return -1;
	if (b->info.start.tv_sec && !a->info.start.tv_sec)
		return 1;

	return timeval_comp(&a->info.start, &b->info.start);
}

void pgroup_assign_peer_ids(merlin_peer_group *pg)
{
	uint i;

	if (!pg)
		return;

	/* sort peerid_table with earliest started first */
	ldebug("Sorting peer id table for peer-group %d with %d nodes",
		   pg->id, pg->total_nodes);
	qsort(pg->nodes, pg->total_nodes, sizeof(merlin_node *), cmp_peer);
	pg->active_nodes = 0;
	ldebug("Done sorting");

	/*
	 * this could be done with a binary search, but since we expect
	 * fewer than 10 peers in each tier and we still have to walk all
	 * the ones with a start-time higher than ours it's not really
	 * worth the complexity
	 */
	ldebug("pg: Assining peer ids. Order:");
	for (i = 0; i < pg->total_nodes; i++) {
		merlin_node *node = pg->nodes[i];

		/*
		 * we must assign peer_id using i here, in case we sort multiple
		 * times. Otherwise we'll only ever increase the peer_id and
		 * end up with all peers having the same id.
		 */
		node->peer_id = i;
		ldebug("pg:   %.1d: %s (%s)", node->peer_id, node->name, node_state_name(node->state));
		if (node == &ipc || (node->state == STATE_CONNECTED)) {
			pg->active_nodes++;
		}
	}
	ldebug("pg:   Active nodes: %u", pg->active_nodes);

	ldebug("Reassigning checks");
	pgroup_reassign_checks();
	if (pg == ipc.pgroup) {
		ipc.info.peer_id = ipc.peer_id;
		linfo("We're now peer #%d out of %d active ones",
			  ipc.peer_id, pg->active_nodes);
		linfo("Handling %u host and %u service checks",
			  ipc.assigned.current.hosts, ipc.assigned.current.services);
		ipc.info.host_checks_handled = ipc.assigned.current.hosts;
		ipc.info.service_checks_handled = ipc.assigned.current.services;
	}
}

static merlin_peer_group *pgroup_create(char *hostgroups)
{
	merlin_peer_group *pg, **ary;

	if (!(pg = calloc(1, sizeof(*pg))))
		return NULL;

	if (!(ary = realloc(peer_group, (num_peer_groups + 1) * sizeof(merlin_peer_group *)))) {
		free(pg);
		return NULL;
	}
	peer_group = ary;

	pg->hostgroups = hostgroups;
	pg->id = num_peer_groups++;
	peer_group[pg->id] = pg;

	return pg;
}

/*
 * cshgs = comma-separated hostgroup string
 * mnemonic names can go fuck themselves
 */
static merlin_peer_group *pgroup_get_by_cshgs(char *hgs)
{
	unsigned int i;

	for (i = 0; i < num_peer_groups; i++) {
		if (!peer_group[i]->hostgroups)
			continue;

		if (!strcmp(peer_group[i]->hostgroups, hgs))
			return peer_group[i];
	}

	return pgroup_create(hgs);
}

static void pgroup_alloc_counters(merlin_peer_group *pg)
{
	unsigned int i;

	pg->host_map = bitmap_create(num_objects.hosts);
	pg->service_map = bitmap_create(num_objects.services);
	pg->alloc = max(num_peers + 1, pg->total_nodes);
	pg->assign = calloc(pg->alloc, sizeof(void *));
	pg->inherit = calloc(pg->alloc, sizeof(void *));
	for (i = 0; i < pg->alloc; i++) {
		pg->assign[i] = calloc(i + 1, sizeof(**pg->assign));
		pg->inherit[i] = calloc(i + 1, sizeof(**pg->inherit));
	}
}

static void pgroup_destroy(merlin_peer_group *pg)
{
	int i;

	bitmap_destroy(pg->host_map);
	bitmap_destroy(pg->service_map);
	for (i = 0; i < max(pg->total_nodes, num_peers); i++) {
		free(pg->assign[i]);
		free(pg->inherit[i]);
	}
	free(pg->assign);
	free(pg->inherit);
	free(pg->host_id_table);
	free(pg->service_id_table);
	free(pg->hostgroups);
}

static int pgroup_add_node(merlin_peer_group *pg, merlin_node *node)
{
	merlin_node **ary;

	ldebug("Adding node '%s' to peer group %d", node->name, pg->id);
	pg->flags |= (node->flags & PGROUP_NODE_FLAGS);
	ary = realloc(pg->nodes, (pg->total_nodes + 1) * sizeof(merlin_node *));
	pg->nodes = ary;
	pg->nodes[pg->total_nodes++] = node;
	node->pgroup = pg;

	return 0;
}

/*
 * returns 0 on OK, > 0 on warnings and < 0 on errors
 */
static int pgroup_validate(merlin_peer_group *pg)
{
	unsigned int i;

	for (i = 0; i < pg->total_nodes; i++) {
		merlin_node *node = pg->nodes[i];
		if ((node->flags & PGROUP_NODE_FLAGS) != pg->flags) {
			lwarn("Warning: %s node %s doesn't share group flags with the rest of the nodes in its group",
			      node_type(node), node->name);
			return 1;
		}
	}

	return 0;
}

/*
 * This really can't be done better, since we don't iterate over all
 * hosts and services in sorted order anywhere else
 */
static int pg_create_id_convtables(merlin_peer_group *pg)
{
	unsigned int i, x = 0;
	const size_t entry_size = sizeof(pg->host_id_table[0]);

	pg->host_id_table = malloc(entry_size * num_objects.hosts);
	pg->service_id_table = malloc(entry_size * num_objects.services);
	if (!pg->host_id_table || !pg->service_id_table) {
		lerr("Failed to allocate host and/or service id conversion tables. Expecting segfault later.");
		/* what the hell do we do here? */
		return -1;
	}
	memset(pg->host_id_table, 0xff, entry_size * num_objects.hosts);
	memset(pg->service_id_table, 0xff, entry_size * num_objects.services);

	for (i = 0; i < num_objects.hosts; i++) {
		unsigned int n, peer_id;

		if (!bitmap_isset(pg->host_map, i)) {
			continue;
		}

		pg->host_id_table[i] = x;
		host_id2pg[i] = pg;

		for (n = 0; n < pg->alloc; n++) {
			/*
			 * the poller won't have the same id's as we do,
			 * so we use the counter to get object id
			 */
			peer_id = x % (n + 1);
			pg->assign[n][peer_id].hosts++;
			pg->inherit[n][i % (n + 1)].hosts++;
		}
		x++;
	}
	for (x = 0, i = 0; i < num_objects.services; i++) {
		unsigned int n, peer_id;

		if (!bitmap_isset(pg->service_map, i))
			continue;

		pg->service_id_table[i] = x;
		service_id2pg[i] = pg;

		for (n = 0; n < pg->alloc; n++) {
			peer_id = x % (n + 1);
			pg->assign[n][peer_id].services++;
			pg->inherit[n][i % (n + 1)].services++;
		}
		x++;
	}

	return 0;
}

static gboolean pgroup_hgroup_mapper(gpointer _name, gpointer _hst, gpointer user_data)
{
	servicesmember *sm;
	host *h = (host *)_hst;
	merlin_peer_group *pg = (merlin_peer_group *)user_data;

	/*
	 * if the host is already in this selection, such as
	 * from overlapping hostgroups assigned to a poller group,
	 * we just move on (this also ensures we don't double-count
	 * services).
	 */
	if (bitmap_isset(pg->host_map, h->id)) {
		ldebug("  Host %d (%s) is already in this group", h->id, h->name);
		return FALSE;
	}
	bitmap_set(pg->host_map, h->id);

	/*
	 * if it's not ours but another poller handles it, we
	 * need to warn about it so we can perform a more
	 * exact check later
	 */
	if (bitmap_isset(poller_handled_hosts, h->id)) {
		ldebug("Host '%s' is handled by two different poller groups!", h->name);
		pg->overlapping++;
	}
	bitmap_set(poller_handled_hosts, h->id);

	pg->assigned.hosts++;

	for (sm = h->services; sm; sm = sm->next) {
		service *s = sm->service_ptr;

		bitmap_set(pg->service_map, s->id);
		bitmap_set(poller_handled_services, s->id);
		pg->assigned.services++;
	}

	return FALSE;
}

static void map_pgroup_hgroup(merlin_peer_group *pg, hostgroup *hg)
{
	ldebug("Mapping hostgroup '%s' to peer group %d", hg->group_name, pg->id);
	g_tree_foreach(hg->members, pgroup_hgroup_mapper, pg);
}

static int pgroup_map_objects(void)
{
	unsigned int i, x;

	for (i = 0; i < num_peer_groups; i++) {
		char *p, *comma;
		struct merlin_peer_group *pg = peer_group[i];

		pgroup_alloc_counters(pg);

		for (p = pg->hostgroups; p; p = comma) {
			hostgroup *hg;
			comma = strchr(p, ',');
			if (comma)
				*comma = 0;
			hg = find_hostgroup(p);
			if (!hg) {
				lerr("Fatal: Hostgroup '%s' not found", p);
				sigshutdown = TRUE;
				return -1;
			}

			map_pgroup_hgroup(pg, hg);
			if (pg->overlapping) {
				lerr("CONFIG ANOMALY: Hostgroup '%s' has %d hosts overlapping with another hostgroup used for poller assigment",
					hg->group_name, pg->overlapping);
			}
			if (pg_create_id_convtables(pg)) {
				lerr("  Failed to create object id conversion tables for pg %d", pg->id);
			}
			if (comma)
				*(comma++) = ',';
			else
				break;
		}
	}

	for (i = 0; i < num_objects.hosts; i++) {
		servicesmember *sm;
		if (bitmap_isset(poller_handled_hosts, i))
			continue;

		for (x = 0; x < num_peers + 1; x++) {
			int peer_id = i % (x + 1);
			ipc.pgroup->assign[x][peer_id].hosts++;
		}
		for (sm = host_ary[i]->services; sm; sm = sm->next) {
			for (x = 0; x < num_peers + 1; x++) {
				int peer_id = sm->service_ptr->id % (x + 1);
				ipc.pgroup->assign[x][peer_id].services++;
			}
		}
	}

	linfo("hosts: %u; services: %u", num_objects.hosts, num_objects.services);
	for (i = 0; i < num_peer_groups; i++) {
		char *p = NULL;
		merlin_peer_group *pg = peer_group[i];
		linfo("peer-group %u", pg->id);
		for (x = 0; x < pg->total_nodes; x++) {
			merlin_node *node = pg->nodes[x];
			char *buf = NULL;
			if (p) {
				nm_asprintf(&buf, "%s, %s", node->name, p);
				free(p);
				p = buf;
			} else {
				p = strdup(node->name);
			}
		}
		linfo("  %d nodes          : %s", pg->total_nodes, p);
		free(p);
		if (pg->hostgroups)
			linfo("  hostgroups: %s", pg->hostgroups);
		linfo("  assigned hosts   : %u", pg->assigned.hosts);
		linfo("  assigned services: %u", pg->assigned.services);
		linfo("  Check/takeover accounting:");
		for (x = 1; x < pg->alloc; x++) {
			unsigned int y;
			linfo("    %d node%s online:", x + 1, x ? "s" : "");
			for (y = 0; y < x + 1; y++) {
				if (x < pg->total_nodes) {
					linfo("      peer %d takes %u hosts, %u services", y,
					      pg->assign[x][y].hosts,
					      pg->assign[x][y].services);
				}
				if (!pg->id)
					continue;
				if (x < ipc.pgroup->total_nodes) {
					linfo("      master peer %d inherits %u hosts, %u services",
					      y, pg->inherit[x][y].hosts,
					      pg->inherit[x][y].services);
				}
			}
		}
	}

	return 0;
}

static int cmpstringp(const void *p1, const void *p2)
{
	return strcmp(*(const char **)p1, *(const char **)p2);
}

/*
 * returns a sorted version of a comma-separated string, with
 * spaces surrounding commas removed
 */
static char *get_sorted_csstr(const char *orig_str)
{
	char *str, *comma, *ret = NULL, **ary, *next;
	unsigned int i = 0, entries = 0, len;

	if (!orig_str || !(str = strdup(orig_str)))
		return NULL;
	len = strlen(orig_str);

	if (!(ary = calloc(len / 2, sizeof(char *)))) {
		free(str);
		free(ret);
		return NULL;
	}

	for (next = str;;) {
		char *p = next;

		while (*p == ',' || *p == ' ' || *p == '\t')
			p++;
		comma = next = strchr(p, ',');
		if (comma) {
			next = comma + 1;
			while (*comma == ',' || *comma == ' ' || *comma == '\t') {
				*comma = 0;
				comma--;
			}
		}

		ary[entries++] = p;
		if (!comma)
			break;
	}

	qsort(ary, entries, sizeof(char *), cmpstringp);
	len = 1;
	for (i = 0; i < entries; i++) {
		len += strlen(ary[i]) + 1;
	}

	if (!(ret = calloc(1, len))) {
		free(str);
		free(ary);
		return NULL;
	}

	for (i = 0; i < entries; i++) {
		if (ret[0])
			ret[strlen(ret)] = ',';
		strcat(ret, ary[i]);
	}

	free(str);
	free(ary);
	return ret;
}

merlin_peer_group *pgroup_by_host_id(unsigned int id)
{
	if (!num_pollers)
		return ipc.pgroup;
	return host_id2pg[id];
}

merlin_peer_group *pgroup_by_service_id(unsigned int id)
{
	if (!num_pollers)
		return ipc.pgroup;
	return service_id2pg[id];
}

static merlin_node *pgroup_node(int type, unsigned int id)
{
	merlin_peer_group *pg = ipc.pgroup;
	unsigned int real_id = id;
	uint32_t *id_table = NULL;

	if (type == HOST_CHECK && host_id2pg && host_id2pg[id]) {
		pg = host_id2pg[id];
		id_table = pg ? pg->host_id_table : NULL;
	} else if (type == SERVICE_CHECK && service_id2pg && service_id2pg[id]) {
		pg = service_id2pg[id];
		id_table = pg ? pg->service_id_table : NULL;
	}

	if (num_pollers && id_table) {
		ldebug("pg: Selected peer-group %d for check id %u", pg->id, id);
		if (pg->active_nodes || !(pg->flags & MERLIN_NODE_TAKEOVER)) {
			real_id = id_table[id];
			ldebug("pg:   real_id=%u", real_id);
		} else {
			ldebug("pg:   no active nodes. Falling back to ipc");
			pg = ipc.pgroup;
		}
	}

	return pg->nodes[assigned_peer(real_id, pg->active_nodes)];
}

merlin_node *pgroup_host_node(unsigned int id)
{
	return pgroup_node(HOST_CHECK, id);
}

merlin_node *pgroup_service_node(unsigned int id)
{
	return pgroup_node(SERVICE_CHECK, id);
}

int pgroup_init(void)
{
	unsigned int i;

	linfo("Initializing peer-groups");
	if (num_pollers) {
		poller_handled_hosts = bitmap_create(num_objects.hosts);
		poller_handled_services = bitmap_create(num_objects.services);
	}

	if (num_pollers) {
		host_id2pg = calloc(sizeof(host_id2pg[0]), num_objects.hosts);
		service_id2pg = calloc(sizeof(service_id2pg[0]), num_objects.services);
		if (!host_id2pg || !service_id2pg) {
			lerr("  Failed to allocate object id2pgroup tables: %m");
			return -1;
		}
	}

	ipc.pgroup = pgroup_create(NULL);
	if (!ipc.pgroup) {
		lerr("  Failed to allocate ipc.pgroup: %m");
		return -1;
	}

	pgroup_add_node(ipc.pgroup, &ipc);
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		if (node->type == MODE_PEER)
			pgroup_add_node(ipc.pgroup, node);
	}
	for (i = 0; i < num_pollers; i++) {
		merlin_node *node = poller_table[i];
		merlin_peer_group *pg;
		char *hgs;

		hgs = get_sorted_csstr(node->hostgroups);
		pg = pgroup_get_by_cshgs(hgs);
		pgroup_add_node(pg, node);
	}

	for (i = 0; i < num_peer_groups; i++) {
		if (pgroup_validate(peer_group[i]) < 0) {
			return -1;
		}
	}
	return pgroup_map_objects();
}

void pgroup_deinit(void)
{
	unsigned int i;

	for (i = 0; i < num_peer_groups; i++)
		pgroup_destroy(peer_group[i]);
	free(peer_group);
	peer_group = NULL;
	bitmap_destroy(poller_handled_hosts);
	bitmap_destroy(poller_handled_services);
	free(host_id2pg);
	free(service_id2pg);
}
