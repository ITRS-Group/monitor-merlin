#include "shared.h"

merlin_node **noc_table, **poller_table, **peer_table;

static int num_selections;
static node_selection *selection_table;

static char *binlog_dir = "/opt/monitor/op5/merlin/binlogs";

node_selection *node_selection_by_name(const char *name)
{
	int i;

	for (i = 0; i < num_selections; i++) {
		if (!strcmp(name, selection_table[i].name))
			return &selection_table[i];
	}

	return NULL;
}

/*
 * Returns the (list of) merlin node(s) associated
 * with a particular selection id, or null if the
 * id is invalid
 */
linked_item *nodes_by_sel_id(int sel)
{
	if (sel < 0 || sel > get_num_selections())
		return NULL;

	return selection_table[sel].nodes;
}

char *get_sel_name(int index)
{
	if (index < 0 || index >= num_selections)
		return NULL;

	return selection_table[index].name;
}

int get_sel_id(const char *name)
{
	int i;

	if (!num_selections || !name)
		return -1;

	for (i = 0; i < num_selections; i++) {
		if (!strcmp(name, selection_table[i].name))
			return selection_table[i].id;
	}

	return -1;
}

int get_num_selections(void)
{
	return num_selections;
}

static int add_selection(char *name, merlin_node *node)
{
	int i;
	node_selection *sel = NULL;

	/* if this selection is already added, just add the node to it */
	for (i = 0; i < num_selections; i++) {
		if (!strcmp(name, selection_table[i].name)) {
			sel = &selection_table[i];
			break;
		}
	}

	if (!sel) {
		selection_table = realloc(selection_table, sizeof(selection_table[0]) * (num_selections + 1));
		sel = &selection_table[num_selections];
		sel->id = num_selections;
		sel->name = strdup(name);
		sel->nodes = NULL;
		num_selections++;
	}
	sel->nodes = add_linked_item(sel->nodes, node);

	return sel->id;
}

/*
 * Return a (list of) merlin node(s) associated
 * with a particular selection name, or null if
 * the selection name is invalid
 */
linked_item *nodes_by_sel_name(const char *name)
{
	return nodes_by_sel_id(get_sel_id(name));
}


/*
 * FIXME: should also handle hostnames
 */
static int resolve(const char *cp, struct in_addr *inp)
{
	return inet_aton(cp, inp);
}


/*
 * creates the node-table, with fanout indices at the various
 * different types of nodes. This allows us to iterate over
 * all the nodes or a particular subset of them using the same
 * table, which is quite handy.
 */
static void create_node_tree(merlin_node *table, unsigned n)
{
	uint i, xnoc, xpeer, xpoll;

	for (i = 0; i < n; i++) {
		merlin_node *node = &table[i];
		switch (node->type) {
		case MODE_NOC:
			num_nocs++;
			break;
		case MODE_POLLER:
			num_pollers++;
			break;
		case MODE_PEER:
			num_peers++;
			break;
		}
	}

	/* this way, we can keep them all linear while each has its own
	 * table and still not waste much memory. pretty nifty, really */
	node_table = calloc(num_nodes, sizeof(merlin_node *));
	noc_table = node_table;
	peer_table = &node_table[num_nocs];
	poller_table = &node_table[num_nocs + num_peers];

	xnoc = xpeer = xpoll = 0;
	for (i = 0; i < n; i++) {
		merlin_node *node = &table[i];
		node->id = i;

		switch (node->type) {
		case MODE_NOC:
			noc_table[xnoc++] = node;
			break;
		case MODE_PEER:
			peer_table[xpeer++] = node;
			break;
		case MODE_POLLER:
			poller_table[xpoll++] = node;
			break;
		}
	}
}

static void grok_node(struct cfg_comp *c, merlin_node *node)
{
	unsigned int i;

	if (!node)
		return;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!v->value)
			cfg_error(c, v, "Variable must have a value\n");

		if (node->type != MODE_NOC && !strcmp(v->key, "hostgroup")) {
			node->hostgroup = strdup(v->value);
			node->selection = add_selection(node->hostgroup, node);
		}
		else if (!strcmp(v->key, "address") || !strcmp(v->key, "host")) {
			if (!resolve(v->value, &node->sain.sin_addr))
				cfg_error(c, v, "Unable to resolve '%s'\n", v->value);
		}
		else if (!strcmp(v->key, "port")) {
			node->sain.sin_port = htons((unsigned short)atoi(v->value));
			if (!node->sain.sin_port)
				cfg_error(c, v, "Illegal value for port: %s\n", v->value);
		}
		else
			cfg_error(c, v, "Unknown variable\n");
	}
	node->last_action = -1;
}

void node_grok_config(struct cfg_comp *config)
{
	uint i;
	int node_i = 0;
	merlin_node *table;

	if (!config)
		return;

	/*
	 * We won't have more nodes than there are compounds, so we
	 * happily waste a bit to make up for the other valid compounds
	 * so we can keep nodes linear in memory
	 */
	table = calloc(config->nested, sizeof(merlin_node));

	for (i = 0; i < config->nested; i++) {
		struct cfg_comp *c = config->nest[i];
		merlin_node *node;

		if (!prefixcmp(c->name, "module") || !prefixcmp(c->name, "test"))
			continue;

		if (!prefixcmp(c->name, "daemon"))
			continue;

		node = &table[node_i++];
		memset(node, 0, sizeof(*node));
		node->sock = -1;
		node->name = next_word((char *)c->name);

		if (!prefixcmp(c->name, "poller") || !prefixcmp(c->name, "slave")) {
			node->type = MODE_POLLER;
			grok_node(c, node);
			if (!node->hostgroup)
				cfg_error(c, NULL, "Missing 'hostgroup' variable\n");
		} else if (!prefixcmp(c->name, "peer")) {
			node->type = MODE_PEER;
			grok_node(c, node);
		} else if (!prefixcmp(c->name, "noc") || !prefixcmp(c->name, "master")) {
			node->type = MODE_NOC;
			grok_node(c, node);
		} else
			cfg_error(c, NULL, "Unknown compound type\n");

		if (node->name)
			node->name = strdup(node->name);
		else
			node->name = strdup(inet_ntoa(node->sain.sin_addr));

		node->sock = -1;
	}

	create_node_tree(table, node_i);
}

void node_log_event_count(merlin_node *node, int force)
{
	struct timeval now;
	merlin_node_stats *s = &node->stats;

	/*
	 * This works like a 'mark' that syslogd produces. We log once
	 * every 60 seconds
	 */
	gettimeofday(&now, NULL);
	if (!force && s->last_logged && s->last_logged + 60 > now.tv_sec)
		return;

	s->last_logged = now.tv_sec;

	linfo("Handled %lld events from/to %s in %s. in: %lld, out: %lld",
	      s->events.read + s->events.sent + s->events.dropped + s->events.logged, node->name,
		  tv_delta(&s->start, &now),
	      s->events.read, s->events.sent + s->events.dropped + s->events.logged);
	if (!(s->events.sent + s->events.dropped + s->events.logged))
		return;
	linfo("'%s' event details: read %lld, sent %lld, dropped %lld, logged %lld",
	      node->name, s->events.read, s->events.sent, s->events.dropped, s->events.logged);
}

const char *node_state(merlin_node *node)
{
	switch (node->status) {
	case STATE_NONE:
		return "not connected";
	case STATE_PENDING:
		return "awaiting response";
	case STATE_NEGOTIATING:
		return "negotiating precedence";
	case STATE_CONNECTED:
		return "connected";
	}

	return "Unknown state (decidedly odd)";
}

const char *node_type(merlin_node *node)
{
	switch (node->type) {
	case MODE_LOCAL:
		return "local ipc";
	case MODE_NOC:
		return "master";
	case MODE_PEER:
		return "peer";
	case MODE_POLLER:
		return "poller";
	}

	return "Unknown node-type";
}

/* close down the connection to a node and mark it as down */
void node_disconnect(merlin_node *node)
{
	node_log_event_count(node, 1);
	/* avoid spurious close() errors while strace/valgrind debugging */
	if (node->sock >= 0)
		close(node->sock);
	node->status = STATE_NONE;
	if (node->action)
		node->action(node, node->status);
	node->last_recv = 0;
	node->sock = -1;
}

static int node_binlog_add(merlin_node *node, merlin_event *pkt)
{
	int result;

	if (!node->binlog) {
		char *path;

		asprintf(&path, "%s/%s.%s.binlog",
				 binlog_dir, is_module ? "module" : "daemon", node->name);
		linfo("Creating binary backlog for %s. On-disk location: %s",
			  node->name, path);

		/* 10MB in memory, 100MB on disk */
		node->binlog = binlog_create(path, 10 << 20, 100 << 20, BINLOG_UNLINK);
		if (!node->binlog) {
			lerr("Failed to create binary backlog for %s: %s",
				 node->name, strerror(errno));
			return -1;
		}
		free(path);
	}

	result = binlog_add(node->binlog, pkt, packet_size(pkt));
	if (result < 0) {
		binlog_wipe(node->binlog, BINLOG_UNLINK);
		/* XXX should mark node as unsynced here */
		node->stats.events.dropped += node->stats.events.logged + 1;
		node->stats.bytes.dropped += node->stats.bytes.logged + packet_size(pkt);
		node->stats.events.logged = 0;
		node->stats.bytes.logged = 0;
	} else {
		node->stats.events.logged++;
		node->stats.bytes.logged += packet_size(pkt);
	}

	node_log_event_count(node, 0);

	return result;
}

/*
 * Reads one event from the given socket into the given merlin_event
 * structure. Returns < 0 on errors, 0 when no data is available and
 * the length of the data read when there is.
 */
int node_read_event(merlin_node *node, merlin_event *pkt, int msec)
{
	int len;
	uint result;

	node_log_event_count(node, 0);

	if (msec && (result = io_read_ok(node->sock, msec)) <= 0)
		return 0;

	len = io_recv_all(node->sock, &pkt->hdr, HDR_SIZE);
	if (len != HDR_SIZE) {
		lerr("In %s: Incomplete header read(). Expected %zu, got %d",
			 __func__, HDR_SIZE, len);
		lerr("Sync lost with %s?", node->name);
		node_disconnect(node);
		return -1;
	}

	if (pkt->hdr.protocol != MERLIN_PROTOCOL_VERSION) {
		lerr("Bad protocol version (%d, expected %d)\n",
			 pkt->hdr.protocol, MERLIN_PROTOCOL_VERSION);
		return -1;
	}

	if (!pkt->hdr.len && pkt->hdr.type != CTRL_PACKET) {
		lerr("Non-control packet of type %d with zero size length (this should never happen)", pkt->hdr.type);
		return len;
	}

	node->last_recv = time(NULL);
	node->stats.events.read++;
	node->stats.bytes.read += HDR_SIZE;

	if (!pkt->hdr.len)
		return HDR_SIZE;

	result = io_recv_all(node->sock, pkt->body, pkt->hdr.len);
	if (result != pkt->hdr.len) {
		lwarn("Bogus read in proto_read_event(). got %d, expected %d",
			  result, pkt->hdr.len);
		lwarn("Sync lost with %s?", node->name);
		node_disconnect(node);
		return -1;
	}
	node->stats.bytes.read += pkt->hdr.len;

	return result;
}

/*
 * Send the given event "pkt" to the node "node", or take appropriate
 * actions on the node itself in case sending fails.
 * Returns 0 on success, and < 0 otherwise.
 */
int node_send_event(merlin_node *node, merlin_event *pkt, int msec)
{
	int result;

	node_log_event_count(node, 0);

	pkt->hdr.protocol = MERLIN_PROTOCOL_VERSION;

	if (packet_size(pkt) > TOTAL_PKT_SIZE) {
		lerr("header is invalid, or packet is too large. aborting");
		return -1;
	}

	if (node->sock < 0 || node->status == STATE_NONE) {
		return node_binlog_add(node, pkt);
	}

	/*
	 * msec less than zero means the caller has already polled the
	 * socket, which should also mean it's connected
	 */
	if (msec >= 0 && !io_write_ok(node->sock, msec)) {
		return node_binlog_add(node, pkt);
	}

	/* if binlog has entries, we must send those first */
	if (binlog_has_entries(node->binlog)) {
		merlin_event *temp_pkt;
		uint len;

		linfo("Emptying backlog for %s", node->name);
		while (io_write_ok(node->sock, 500) && !binlog_read(node->binlog, (void **)&temp_pkt, &len)) {
			result = io_send_all(node->sock, temp_pkt, packet_size(temp_pkt));

			/* keep going while we successfully send something */
			if (result == packet_size(temp_pkt))
				continue;

			/*
			 * any other failure means we must kill the connection
			 * and let whatever api (net or ipc) it was that called
			 * us attempt to establish it again
			 */
			node_disconnect(node);

			/*
			 * we can recover from total failures by unread()'ing
			 * the entry we just read and then adding the new entry
			 * to the binlog in the hopes that we'll get a
			 * connection up and running again before it's time to
			 * send more data to this node
			 */
			if (result < 0) {
				if (!binlog_unread(node->binlog, temp_pkt, len)) {
					return node_binlog_add(node, pkt);
				}
			}

			/*
			 * we wrote a partial event or failed to unread the event,
			 * so this node is now out of sync. We must wipe the binlog
			 * and possibly mark this node as being out of sync.
			 */
			binlog_wipe(node->binlog, BINLOG_UNLINK);
			node->stats.events.dropped += node->stats.events.logged + 1;
			node->stats.bytes.dropped += node->stats.events.logged + packet_size(pkt);
			node_log_event_count(node, 0);
			return -1;
		}
	}

	/* binlog may still have entries. If so, add to it and return */
	if (binlog_has_entries(node->binlog))
		return node_binlog_add(node, pkt);

	result = io_send_all(node->sock, pkt, packet_size(pkt));

	/* successfully sent, so add it to the counter and return 0 */
	if (result == packet_size(pkt)) {
		node->stats.events.sent++;
		node->stats.bytes.sent += result;
		node->last_sent = time(NULL);
		return 0;
	}

	/*
	 * zero size writes and write errors get stashed in binlog.
	 * From the callers point of view, this counts as a success.
	 */
	if (result <= 0 && !node_binlog_add(node, pkt))
		return 0;

	/* possibly mark the node as out of sync here */
	return -1;
}

/*
 * Sends a control event of type "type" with selection "selection"
 * to node "node"
 */
int node_send_ctrl(merlin_node *node, int type, int selection, int msec)
{
	merlin_event pkt;

	memset(&pkt.hdr, 0, HDR_SIZE);

	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.len = 0;
	pkt.hdr.code = type;
	pkt.hdr.selection = selection;

	return node_send_event(node, &pkt, msec);
}
