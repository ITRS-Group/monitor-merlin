/*
 * Functions for controlling nagios' behaviour. Add more as needed.
 */

#include "module.h"
#include "slist.h"
#include "nagios/nagios.h"

static time_t stall_start;
static merlin_node **peerid_table;
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

	if (!self.active_peers || !sl)
		return 1;

	pos = slist_find_pos(sl, key);

	/* this should never happen */
	if (pos < 0) {
		return -1;
	}

	return (pos % (self.active_peers + 1)) == self.peer_id;
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

static void assign_peer_ids(void)
{
	uint i, inc = 0;
	uint h_extra, s_extra, h_checks, s_checks;

	if (!peerid_table) {
		peerid_table = malloc(num_peers * sizeof(merlin_node *));
		for (i = 0; i < num_peers; i++) {
			peerid_table[i] = peer_table[i];
		}
	}

	/* sort peerid_table with earliest started first */
	ldebug("Sorting peerid_table with %d entries", num_peers);
	qsort(peerid_table, num_peers, sizeof(merlin_node *), cmp_peer);
	self.active_peers = 0;
	self.peer_id = (int)-1;

	/*
	 * this could be done with a binary search, but since we expect
	 * fewer than 10 peers in each tier and we still have to walk all
	 * the ones with a start-time higher than ours it's not really
	 * worth the complexity
	 */
	for (i = 0; i < num_peers; i++) {
		int result;
		merlin_node *node = peerid_table[i];

		/*
		 * we must assign peer_id using i here, in case we sort multiple
		 * times. Otherwise we'll only ever increase the peer_id and
		 * end up with all peers having the same id.
		 */
		node->peer_id = i + inc;
		if (node->state == STATE_CONNECTED && node->info.start.tv_sec)
			self.active_peers++;

		/* already adding +1, so move on */
		if (inc)
			continue;

		result = timeval_comp(&node->info.start, &self.start);
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
		self.peer_id = node->peer_id;
		inc = 1;
		node->peer_id += inc;
	}

	if (self.peer_id == (int)-1)
		self.peer_id = self.active_peers;

	ipc.peer_id = self.peer_id;

	linfo("We're now peer #%d out of %d active ones", self.peer_id,
		  self.active_peers + 1);
	h_extra = (scheduling_info.total_hosts % (self.active_peers + 1)) > self.peer_id;
	s_extra = (scheduling_info.total_services % (self.active_peers + 1)) > self.peer_id;
	h_checks = (scheduling_info.total_hosts / (self.active_peers + 1));
	s_checks = (scheduling_info.total_services / (self.active_peers + 1));
	self.host_checks_handled = h_checks + h_extra;
	self.service_checks_handled = s_checks + s_extra;
	linfo("Handling %u host and %u service checks",
		  self.host_checks_handled, self.service_checks_handled);
}

/*
 * This gets run whenever we receive a CTRL_ACTIVE or CTRL_INACTIVE
 * packet. node is the originating node. prev_state is the previous
 * state of the node (STATE_CONNECTED fe).
 */
static int node_action(merlin_node *node, int prev_state)
{
	if (node->type == MODE_PEER) {
		assign_peer_ids();
	}

	return 0;
}

void ctrl_set_node_actions(void)
{
	uint i;

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		node->action = node_action;
	}
}


/*
 * Handles merlin control events inside the module. Control events
 * that relate to cross-host communication only never reaches this.
 */
void handle_control(merlin_node *node, merlin_event *pkt)
{
	const char *ctrl;
	if (!pkt) {
		lerr("handle_control() called with NULL packet");
		return;
	}

	ctrl = ctrl_name(pkt->hdr.code);
	linfo("Received control packet code %d (%s) from %s",
		  pkt->hdr.code, ctrl, node ? node->name : "local Merlin daemon");

	/* protect against bogus headers */
	if (!node && (pkt->hdr.code == CTRL_INACTIVE || pkt->hdr.code == CTRL_ACTIVE)) {
		lerr("Received %s with unknown node id %d", ctrl, pkt->hdr.selection);
		return;
	}
	switch (pkt->hdr.code) {
	case CTRL_INACTIVE:
		/*
		 * must memset() node->info before the disconnect handler
		 * so we discard it in the peer id calculation dance if
		 * we get data from it before it sends us a CTRL_ACTIVE
		 * packet
		 */
		memset(&node->info, 0, sizeof(node->info));
		node_set_state(node, STATE_NONE, "Received CTRL_INACTIVE");
		break;
	case CTRL_ACTIVE:
		/*
		 * Only mark the node as connected if the CTRL_ACTIVE packet
		 * checks out properly and the info is new. If it *is* new,
		 * we must re-do the peer assignment thing.
		 */
		if (!handle_ctrl_active(node, pkt)) {
			node_set_state(node, STATE_CONNECTED, "Received CTRL_ACTIVE");
			assign_peer_ids();
		}
		break;
	case CTRL_STALL:
		ctrl_stall_start();
		break;
	case CTRL_RESUME:
		ctrl_stall_stop();
		assign_peer_ids();
		break;
	case CTRL_STOP:
		linfo("Received (and ignoring) CTRL_STOP event. What voodoo is this?");
		break;
	default:
		lwarn("Unknown control code: %d", pkt->hdr.code);
	}
}
