/*
 * Functions for controlling nagios' behaviour. Add more as needed.
 */

#include "module.h"
#include "slist.h"
#include "nagios/nagios.h"

static time_t stall_start;
static merlin_node **peerid_table;
static int active_peers, peer_id;
static slist *host_sl, *service_sl;
extern sched_info scheduling_info;
extern host *host_list;
extern service *service_list;

/* return number of stalling seconds remaining */
#define STALL_TIMER 20
int is_stalling(void)
{
	/* stall_start is set to 0 when we stop stalling */
	if (stall_start && stall_start + STALL_TIMER > time(NULL))
		return (stall_start + STALL_TIMER) - time(NULL);

	stall_start = 0;
	return 0;
}

/*
 * we use setter functions for these, so we can start stalling
 * after having sent the paths without having to implement the
 * way we mark and unmark stalling in multiple places
 */
void ctrl_stall_start(void)
{
	stall_start = time(NULL);
}

void ctrl_stall_stop(void)
{
	stall_start = 0;
}

static int host_cmp(const void *a_, const void *b_)
{
	const host *a = *(const host **)a_;
	const host *b = *(const host **)b_;

	return strcmp(a->name, b->name);
}

static int service_cmp(const void *a_, const void *b_)
{
	const service *a = *(const service **)a_;
	const service *b = *(const service **)b_;
	int result;

	result = strcmp(a->host_name, b->host_name);
	return result ? result : strcmp(a->description, b->description);
}

static int should_run_check(slist *sl, const void *key)
{
	int pos;

	if (!active_peers || !sl)
		return 1;

	pos = slist_find_pos(sl, key);

	/* this should never happen */
	if (pos < 0) {
		return -1;
	}

	return (pos % (active_peers + 1)) == peer_id;
}

int ctrl_should_run_host_check(char *host_name)
{
	host h;
	h.name = host_name;
	return should_run_check(host_sl, &h);
}

int ctrl_should_run_service_check(char *host_name, char *desc)
{
	service s;
	s.host_name = host_name;
	s.description = desc;
	return should_run_check(service_sl, &s);
}

static void create_host_table(void)
{
	host *h;

	if (!num_peers)
		return;

	linfo("Creating sorted host table for %d hosts",
		  scheduling_info.total_hosts);
	host_sl = slist_init(scheduling_info.total_hosts, host_cmp);
	for (h = host_list; h; h = h->next)
		slist_add(host_sl, h);
	slist_sort(host_sl);
}

static void create_service_table(void)
{
	service *s;

	if (!num_peers)
		return;

	linfo("Creating sorted service table for %d services",
		  scheduling_info.total_services);
	service_sl = slist_init(scheduling_info.total_services, service_cmp);
	for (s = service_list; s; s = s->next)
		slist_add(service_sl, s);
	slist_sort(service_sl);
}

void ctrl_create_object_tables(void)
{
	create_host_table();
	create_service_table();
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

	if (a->state != b->state) {
		if (a->state == STATE_CONNECTED)
			return -1;
		if (b->state == STATE_CONNECTED)
			return 1;
	}

	return timeval_comp(&a->start, &b->start);
}

static void assign_peer_ids(void)
{
	int i, inc = 0;

	if (!num_peers)
		return;

	if (!peerid_table) {
		peerid_table = malloc(num_peers * sizeof(merlin_node *));
		for (i = 0; i < num_peers; i++) {
			peerid_table[i] = peer_table[i];
		}
	}

	ldebug("Sorting peerid_table with %d entries", num_peers);
	qsort(peerid_table, num_peers, sizeof(merlin_node *), cmp_peer);
	active_peers = 0;
	peer_id = -1;
	for (i = 0; i < num_peers; i++) {
		int result;
		merlin_node *node = peerid_table[i];

		node->peer_id += inc;
		if (node->state == STATE_CONNECTED)
			active_peers++;

		/* already adding +1, so move on */
		if (inc)
			continue;

		result = timeval_comp(&merlin_start, &node->start);
		if (result < 0) {
			continue;
		}

		if (!result) {
			lerr("%s started the same microsecond as us. Yea right...",
				 node->name);
			continue;
		}

		/*
		 * The peers after this one in the list were started after us,
		 * so we take over this peer's id and start adding 1 to the peer
		 * ids
		 */
		peer_id = node->peer_id;
		inc = 1;
		node->peer_id += inc;
	}

	if (peer_id == -1)
		peer_id = active_peers;

	linfo("We're now peer #%d out of %d active ones", peer_id,
		  active_peers + 1);
	linfo("Handling roughly %d host and %d service checks",
		  scheduling_info.total_hosts / (active_peers + 1),
		  scheduling_info.total_services / (active_peers + 1));
}

/*
 * Marks a (poller) node as active or inactive
 */
static void set_node_state(int id, int state)
{
	merlin_node *node;
	int old_state;

	/* make sure we don't access outside the node_table */
	if (id < 0 || id >= num_nodes)
		return;

	node = node_table[id];
	if (state == STATE_NONE) {
		memset(&node->start, 0, sizeof(node->start));
	}

	old_state = node->state;
	node->state = state;
	if (node->type == MODE_PEER && state != old_state) {
		assign_peer_ids();
	}
}

/*
 * Handles merlin control events inside the module. Control events
 * that relate to cross-host communication only never reaches this.
 */
void handle_control(merlin_event *pkt)
{
	char *sel_name, *node_name = NULL;
	merlin_node *node;

	if (!pkt) {
		lerr("handle_control() called with NULL packet");
		return;
	}

	sel_name = get_sel_name(pkt->hdr.selection);
	if (pkt->hdr.selection < num_nodes)
		node_name = node_table[pkt->hdr.selection]->name;
	if (node_name && (pkt->hdr.code == CTRL_INACTIVE || pkt->hdr.code == CTRL_ACTIVE)) {
		linfo("Received control packet code %d for %s node %s",
			  pkt->hdr.code,
			  node_type(node_table[pkt->hdr.selection]), node_name);
	} else {
		if (pkt->hdr.selection == CTRL_GENERIC)
			linfo("Received general control packet, code %d", pkt->hdr.code);
		else {
			if (!sel_name) {
				lwarn("Received control packet code %d for invalid selection", pkt->hdr.code);
			} else {
				linfo("Received control packet code %d for selection '%s'",
					  pkt->hdr.code, sel_name);
			}
		}
	}

	switch (pkt->hdr.code) {
	case CTRL_INACTIVE:
		set_node_state(pkt->hdr.selection, STATE_NONE);
		break;
	case CTRL_ACTIVE:
		node = node_table[pkt->hdr.selection];
		memcpy(&node->start, &pkt->body, sizeof(struct timeval));
		ldebug("node %s started %lu.%lu", node->name, node->start.tv_sec, node->start.tv_usec);
		set_node_state(pkt->hdr.selection, STATE_CONNECTED);
		break;
	case CTRL_STALL:
		ctrl_stall_start();
		break;
	case CTRL_RESUME:
		ctrl_stall_stop();
		break;
	case CTRL_STOP:
		linfo("Received (and ignoring) CTRL_STOP event. What voodoo is this?");
		break;
	default:
		lwarn("Unknown control code: %d", pkt->hdr.code);
	}
}
