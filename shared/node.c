#include "shared.h"
#include <netdb.h>

static time_t stall_start;

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

merlin_node **noc_table, **poller_table, **peer_table;

static int num_selections;
static node_selection *selection_table;

void node_set_state(merlin_node *node, int state, const char *reason)
{
	int prev_state, add;

	if (!node)
		return;

	if (node->state == state)
		return;

	if (reason) {
		linfo("NODESTATE: %s: %s -> %s: %s", node->name,
		      node_state_name(node->state), node_state_name(state), reason);
	}

	/*
	 * Keep track of active nodes. Setting 'add' to the proper
	 * value means we needn't bother with an insane chain of
	 * if()'s later.
	 * add = +1 if state changes TO 'STATE_CONNECTED'.
	 * add = -1 if state changes FROM 'STATE_CONNECTED'
	 * add remains zero for all other cases
	 */
	if (state == STATE_CONNECTED) {
		add = 1;
		node->connect_time = time(NULL);
		node->csync_num_attempts = 0;
	} else if (node->state == STATE_CONNECTED) {
		add = -1;
	} else {
		add = 0;
	}
	if (node->type == MODE_POLLER)
		self->active_pollers += add;
	else if (node->type == MODE_PEER)
		self->active_peers += add;
	else if (node->type == MODE_MASTER)
		self->active_masters += add;

	prev_state = node->state;
	node->state = state;

	if (node->state == STATE_NEGOTIATING && node != &ipc) {
		ldebug("Sending CTRL_ACTIVE to %s", node->name);
		node_send_ctrl_active(node, CTRL_GENERIC, &ipc.info);
	}

	if (node->state != STATE_CONNECTED && prev_state != STATE_CONNECTED)
		return;

	if (node->action)
		node->action(node, prev_state);

	if (node->state == STATE_CONNECTED && node->sock >= 0) {
		int snd = 0, rcv = 0;
		socklen_t size = sizeof(int);

		/* mark this so we can disconnect nodes that never send data */
		node->last_recv = time(NULL);

		merlin_set_socket_options(node->sock, 224 * 1024);
		getsockopt(node->sock, SOL_SOCKET, SO_SNDBUF, &snd, &size);
		getsockopt(node->sock, SOL_SOCKET, SO_SNDBUF, &rcv, &size);
		ldebug("send / receive buffers are %s / %s for node %s",
			   human_bytes(snd), human_bytes(rcv), node->name);
	}
}

node_selection *node_selection_by_name(const char *name)
{
	int i;

	for (i = 0; i < num_selections; i++) {
		if (!strcmp(name, selection_table[i].name))
			return &selection_table[i];
	}

	return NULL;
}

merlin_node *node_by_id(uint id)
{
	if (num_nodes && id < num_nodes)
		return node_table[id];

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

static linked_item *add_linked_item(linked_item *list, void *item)
{
	struct linked_item *entry = malloc(sizeof(linked_item));

	if (!entry) {
		lerr("Failed to malloc(%u): %s", sizeof(linked_item), strerror(errno));
		return NULL;
	}

	entry->item = item;
	entry->next_item = list;
	return entry;
}

static int add_one_selection(char *name, merlin_node *node)
{
	int i;
	node_selection *sel = NULL;

	/*
	 * strip trailing spaces. leading ones are stripped in
	 * the caller
	 */
	i = strlen(name);
	while (name[i - 1] == '\t' || name[i - 1] == ' ')
		name[--i] = 0;
	printf("Adding selection '%s' for node '%s'\n", name, node->name);

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

static int add_selection(char *name, merlin_node *node)
{
	for (;;) {
		char *comma;

		if ((comma = strchr(name, ','))) {
			*comma = 0;
		}
		add_one_selection(name, node);
		if (!comma)
			break;
		name = comma + 1;
		while (*name == ' ' || *name == '\t')
			name++;
	}

	return 0;
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
 * Resolve an ip-address or hostname and convert it to a
 * machine-usable 32-bit representation
 */
static int resolve(const char *cp, struct in_addr *inp)
{
	struct addrinfo hints, *rp, *ai = NULL;
	int result;

	/* try simple lookup first and avoid DNS lookups */
	result = inet_aton(cp, inp);
	if (result)
		return 0;

	linfo("Resolving '%s'...", cp);
	/*
	 * we allow only IPv4 for now. Change to AF_UNSPEC when we
	 * want to support IPv6 too, and make the necessary changes
	 * to the merlin_node structure
	 */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = 0;

	result = getaddrinfo(cp, NULL, &hints, &ai);
	if (result < 0) {
		lerr("Failed to lookup '%s': %s", cp, gai_strerror(result));
		freeaddrinfo(ai);
		return -1;
	}

	/*
	 * walk the results. We break at the first result we find
	 * and copy it to inp
	 */
	for (rp = ai; rp; rp = ai->ai_next) {
		if (rp->ai_addr)
			break;
	}

	if (rp) {
		char buf[256]; /* used for inet_ntop() */
		struct sockaddr_in *sain = (struct sockaddr_in *)rp->ai_addr;

		linfo("'%s' resolves to %s", cp,
			  inet_ntop(rp->ai_family, &sain->sin_addr, buf, sizeof(buf)));
		memcpy(inp, &sain->sin_addr, sizeof(*inp));
	}
	freeaddrinfo(ai);

	return rp ? 0 : -1;
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
			num_masters++;
			break;
		case MODE_POLLER:
			num_pollers++;
			break;
		case MODE_PEER:
			num_peers++;
			break;
		}
	}

	/*
	 * Sort nodes by type. This way, we can keep them all linear
	 * while each has its own table base pointer and still not waste
	 * much memory. Pretty nifty, really.
	 */
	node_table = calloc(num_nodes, sizeof(merlin_node *));
	noc_table = node_table;
	peer_table = &node_table[num_masters];
	poller_table = &node_table[num_masters + num_peers];

	xnoc = xpeer = xpoll = 0;
	for (i = 0; i < n; i++) {
		merlin_node *node = &table[i];

		switch (node->type) {
		case MODE_NOC:
			node->id = xnoc;
			noc_table[xnoc++] = node;
			break;
		case MODE_PEER:
			node->id = num_masters + xpeer;
			peer_table[xpeer++] = node;
			break;
		case MODE_POLLER:
			node->id = num_masters + num_peers + xpoll;
			poller_table[xpoll++] = node;
			break;
		}
		if(is_module)
			asprintf(&node->source_name, "Merlin %s %s", node_type(node), node->name);
	}
}

#define MRLN_ADD_NODE_FLAG(word) { MERLIN_NODE_##word, #word }
static struct {
	int code;
	const char *key;
} node_config_flags[] = {
	MRLN_ADD_NODE_FLAG(TAKEOVER),
	MRLN_ADD_NODE_FLAG(CONNECT),
	MRLN_ADD_NODE_FLAG(NOTIFIES),
	MRLN_ADD_NODE_FLAG(FIXED_SRCPORT),
};

static int grok_node_flag(int *flags, const char *key, const char *value)
{
	uint i;
	int set, code = -1;

	set = strtobool(value);
	for (i = 0; i < ARRAY_SIZE(node_config_flags); i++) {
		if (!strcasecmp(key, node_config_flags[i].key)) {
			code = node_config_flags[i].code;
			break;
		}
	}

	if (code == -1) {
		return -1;
	}

	/* set or unset this flag */
	if (set) {
		*flags |= code;
	} else {
		*flags = *flags & ~code;
	}

	return 0;
}

static void grok_node(struct cfg_comp *c, merlin_node *node)
{
	unsigned int i;
	int sel_id = -1;
	char *address = NULL;
	struct cfg_var *address_var = NULL;

	if (!node)
		return;

	switch (node->type) {
	case MODE_NOC:
		node->flags = MERLIN_NODE_DEFAULT_MASTER_FLAGS;
		break;
	case MODE_PEER:
		node->flags = MERLIN_NODE_DEFAULT_PEER_FLAGS;
		break;
	case MODE_POLLER:
		node->flags = MERLIN_NODE_DEFAULT_POLLER_FLAGS;
		break;
	}

	/* some sane defaults */
	node->data_timeout = pulse_interval * 2;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!v->value)
			cfg_error(c, v, "Variable must have a value\n");

		if (node->type != MODE_NOC && (!strcmp(v->key, "hostgroup") || !strcmp(v->key, "hostgroups"))) {
			node->hostgroups = strdup(v->value);
			sel_id = add_selection(v->value, node);
		}
		else if (!strcmp(v->key, "address") || !strcmp(v->key, "host")) {
			address = v->value;
			address_var = v;
		}
		else if (!strcmp(v->key, "port")) {
			node->sain.sin_port = htons((unsigned short)atoi(v->value));
			if (!node->sain.sin_port)
				cfg_error(c, v, "Illegal value for port: %s\n", v->value);
		}
		else if (!strcmp(v->key, "data_timeout")) {
			char *endptr;
			node->data_timeout = (unsigned int)strtol(v->value, &endptr, 10);
			if (*endptr != 0)
				cfg_error(c, v, "Illegal value for data_timeout: %s\n", v->value);
		}
		else if (!strcmp(v->key, "max_sync_attempts")) {
			/* restricting max sync attempts is a terrible idea, don't do anything */
		}
		else if (grok_node_flag(&node->flags, v->key, v->value) < 0) {
			cfg_error(c, v, "Unknown variable\n");
		}
	}

	if (!address)
		address = node->name;

	if (!is_module && resolve(address, &node->sain.sin_addr) < 0)
		cfg_error(c, address_var, "Unable to resolve '%s'\n", address);

	for (i = 0; i < c->nested; i++) {
		struct cfg_comp *comp = c->nest[i];
		if (!strcmp(comp->name, "object_config")) {
			node->csync.configured = 1;
			grok_confsync_compound(comp, &node->csync);
			continue;
		}
		/* this is for 'mon oconf push' only */
		if (!strcmp(comp->name, "sync")) {
			continue;
		}

		cfg_error(comp, NULL, "Unknown compound statement in node object");
	}

	node->last_action = -1;
	if (node->type == MODE_POLLER && sel_id == -1) {
		cfg_error(c, NULL, "Missing 'hostgroup' variable in poller definition\n");
	}
}

void node_grok_config(struct cfg_comp *config)
{
	uint i;
	int node_i = 0;
	static merlin_node *table = NULL;

	if (!config)
		return;

	/*
	 * We won't have more nodes than there are compounds, so we
	 * happily waste a bit to make up for the other valid compounds
	 * so we can keep nodes linear in memory
	 */
	if (table)
		free(table);
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
			node->flags = MERLIN_NODE_DEFAULT_POLLER_FLAGS;
			grok_node(c, node);
		} else if (!prefixcmp(c->name, "peer")) {
			node->type = MODE_PEER;
			node->flags = MERLIN_NODE_DEFAULT_PEER_FLAGS;
			grok_node(c, node);
		} else if (!prefixcmp(c->name, "noc") || !prefixcmp(c->name, "master")) {
			node->type = MODE_NOC;
			node->flags = MERLIN_NODE_DEFAULT_MASTER_FLAGS;
			grok_node(c, node);
		} else
			cfg_error(c, NULL, "Unknown compound type\n");

		if (node->name)
			node->name = strdup(node->name);
		else
			node->name = strdup(inet_ntoa(node->sain.sin_addr));

		node->sock = -1;
		memset(&node->info, 0, sizeof(node->info));
	}

	create_node_tree(table, node_i);
}

void node_log_event_count(merlin_node *node, int force)
{
	struct timeval now;
	merlin_node_stats *s = &node->stats;
	unsigned long long b_in, b_out, e_in, e_out;
	const char *dura;

	/*
	 * This works like a 'mark' that syslogd produces. We log once
	 * every 60 seconds
	 */
	gettimeofday(&now, NULL);
	if (!force && s->last_logged && s->last_logged + 60 > now.tv_sec)
		return;

	s->last_logged = now.tv_sec;
	dura = tv_delta(&self->start, &now);

	b_in = s->bytes.read;
	b_out = s->bytes.sent + s->bytes.logged + s->bytes.dropped;
	e_in = s->events.read;
	e_out = s->events.sent + s->events.logged + s->events.dropped;
	ldebug("Handled %llu events from/to %s in %s. in: %llu, out: %llu",
		  e_in + e_out, node->name, dura, e_in, e_out);
	ldebug("Handled %s from/to %s in %s. in: %s, out: %s",
		  human_bytes(b_in + b_out), node->name, dura,
		  human_bytes(b_in), human_bytes(b_out));
	if (!e_out)
		return;
	ldebug("%s events/bytes: read %llu/%s, sent %llu/%s, dropped %llu/%s, logged %llu/%s, logsize %u/%s",
	      node->name, e_in, human_bytes(b_in),
		  s->events.sent, human_bytes(s->bytes.sent),
		  s->events.dropped, human_bytes(s->bytes.dropped),
		  s->events.logged, human_bytes(s->bytes.logged),
		  binlog_entries(node->binlog), human_bytes(binlog_size(node->binlog)));
}

const char *node_state(merlin_node *node)
{
	switch (node->state) {
	case STATE_NONE:
		return "not connected";
	case STATE_PENDING:
		return "awaiting response";
	case STATE_NEGOTIATING:
		return "negotiating version and capabilities";
	case STATE_CONNECTED:
		return "connected";
	}

	return "Unknown state (decidedly odd)";
}

const char *node_type(merlin_node *node)
{
	switch (node->type) {
	case MODE_LOCAL:
		return "local";
	case MODE_NOC:
		return "master";
	case MODE_PEER:
		return "peer";
	case MODE_POLLER:
		return "poller";
	case MODE_INTERNAL:
		return "internal";
	}

	return "Unknown node-type";
}

/* close down the connection to a node and mark it as down */
void node_disconnect(merlin_node *node, const char *fmt, ...)
{
	va_list ap;
	char *reason = NULL;

	if (node->state == STATE_CONNECTED)
		node_log_event_count(node, 1);

	if (fmt) {
		va_start(ap, fmt);
		vasprintf(&reason, fmt, ap);
		va_end(ap);
	}
	node_set_state(node, STATE_NONE, reason);
	if (reason)
		free(reason);
	node->last_recv = 0;

	/* avoid spurious close() errors while strace/valgrind debugging */
	if (node->sock >= 0)
		close(node->sock);
	node->sock = -1;

	/* csync checks only run on reconnect if node->info isn't "identical", so reset it */
	if (node != &ipc)
		memset(&(node->info), 0, sizeof(node->info));

	iocache_reset(node->ioc);
}

static int node_binlog_add(merlin_node *node, merlin_event *pkt)
{
	int result;

	/*
	 * we skip stashing some packet types in the binlog. Typically
	 * those that get generated immediately upon reconnect anyway
	 * since they would just cause unnecessary overhead and might
	 * trigger a lot of unnecessary actions if stashed.
	 */
	if (pkt->hdr.type == CTRL_PACKET) {
		if (pkt->hdr.code == CTRL_ACTIVE || pkt->hdr.code == CTRL_INACTIVE)
			return 0;
	}

	if (!node->binlog) {
		char *path = NULL;

		asprintf(&path, "%s/%s.%s.binlog",
				 binlog_dir ? binlog_dir : BINLOGDIR,
				 is_module ? "module" : "daemon",
				 node->name);
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
 * Read as much data as we possibly can from the node so
 * that whatever parsing code there is can handle it later.
 * All information the caller needs will reside in the
 * nodes own merlin_iocache function, and we return the
 * number of bytes read, or -1 on errors.
 * The io-cache buffer must be allocated before we get
 * to this point, and if the caller wants to poll the
 * socket for input, it'll have to do so itself.
 */
int node_recv(merlin_node *node)
{
	int bytes_read;
	iocache *ioc = node->ioc;

	if (!node || node->sock < 0) {
		return -1;
	}

	if (!iocache_capacity(ioc)) {
		/* Cache full, maybe next time?  */
		return 0;
	}

	bytes_read = iocache_read(ioc, node->sock);

	/*
	 * If we read something, update the stat counter
	 * and return. The caller will have to handle the
	 * input as it sees fit
	 */
	if (bytes_read > 0) {
		node->last_action = node->last_recv = time(NULL);
		node->stats.bytes.read += bytes_read;
		return bytes_read;
	}

	/* no real error, but no new data, so return 0 */
	if (errno == EAGAIN || errno == EWOULDBLOCK) {
		ldebug("No input available from %s node %s.", node_type(node), node->name);
		return 0;
	}

	/*
	 * Remote endpoint shut down, or we ran into some random error
	 * we can't handle any other way than disconnecting the node and
	 * letting the write machinery attempt to reconnect later
	 */
	if (bytes_read < 0) {
		lerr("Failed to read from socket %d into %p for %s node %s: %s",
		     node->sock, ioc, node_type(node), node->name, strerror(errno));
	}

	/* zero-read. We've been disconnected for some reason */
	ldebug("bytes_read: %d; errno: %d; strerror(%d): %s",
		   bytes_read, errno, errno, strerror(errno));
	node_disconnect(node, "recv() returned zero");
	return -1;
}

/*
 * wraps io_send_all() and adds proper error handling when we run
 * into sending errors. It's up to the caller to poll the socket
 * for writability, or pass the proper flags and ignore errors
 */
int node_send(merlin_node *node, void *data, unsigned int len, int flags)
{
	merlin_event *pkt = (merlin_event *)data;
	int sent, sd = 0;

	if (!node || node->sock < 0)
		return 0;

	if (len >= HDR_SIZE && pkt->hdr.type == CTRL_PACKET) {
		ldebug("Sending %s to %s", ctrl_name(pkt->hdr.code), node->name);
		if (pkt->hdr.code == CTRL_ACTIVE) {
			merlin_nodeinfo *info = (merlin_nodeinfo *)&pkt->body;
			ldebug("   start time: %lu.%lu",
			       info->start.tv_sec, info->start.tv_usec);
			ldebug("  config hash: %s", tohex(info->config_hash, 20));
			ldebug(" config mtime: %lu", info->last_cfg_change);
		}
	}

	sent = io_send_all(node->sock, data, len);
	/* success. Should be the normal case */
	if (sent == (int)len) {
		node->stats.bytes.sent += sent;
		node->last_action = node->last_sent = time(NULL);
		return sent;
	}

	/*
	 * partial writes and complete failures can only be handled
	 * by disconnecting and re-syncing the stream
	 */
	sd = node->sock;
	node_disconnect(node, "Partial or failed write() (sent=%d; len=%d): %s",
				   sent, len, strerror(errno));

	if (sent < 0) {
		/* if we would have blocked, we simply return 0 */
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		/* otherwise we log the error and disconnect the node */
		lerr("Failed to send(%d, %p, %d, %d) to %s: %s",
			 sd, data, len, flags, node->name, strerror(errno));
		return sent;
	}

	/* partial write. ugh... */
	lerr("Partial send() to %s. %d of %d bytes sent",
		 node->name, sent, len);
	return -1;
}

/*
 * Fetch one event from the node's iocache. If the cache is
 * exhausted, we handle partial events and iocache resets and
 * return NULL
 */
merlin_event *node_get_event(merlin_node *node)
{
	merlin_event *pkt;
	iocache *ioc = node->ioc;

	pkt = (merlin_event *)(iocache_use_size(ioc, HDR_SIZE));

	/*
	 * buffer is empty
	 */
	if (pkt == NULL) {
		return NULL;
	}

	/*
	 * If buffer is smaller than expected, put the header back
	 * and wait for more data
	 */
	if (pkt->hdr.len > iocache_available(ioc)) {
		ldebug("IOC: packet is longer (%i) than remaining data (%lu) from %s - will read more and try again", pkt->hdr.len, iocache_available(ioc), node->name);
		if (iocache_unuse_size(ioc, HDR_SIZE) < 0)
			lerr("IOC: Failed to unuse %d bytes from iocache. Next packet from %s will be invalid\n", HDR_SIZE, node->name);
		return NULL;
	}

	if (pkt->hdr.sig.id != MERLIN_SIGNATURE) {
		lerr("Invalid signature on packet from '%s'. Disconnecting node", node->name);
		node_disconnect(node, "Invalid signature");
		return NULL;
	}
	node->stats.events.read++;
	iocache_use_size(ioc, pkt->hdr.len);
	return pkt;
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

	if (packet_size(pkt) > MAX_PKT_SIZE) {
		lerr("header is invalid, or packet is too large. aborting");
		return -1;
	}

	if (node->sock < 0 || node->state != STATE_CONNECTED) {
		return node_binlog_add(node, pkt);
	}

	/*
	 * msec less than zero means the caller has already polled the
	 * socket, which should also mean it's connected
	 */
	if (msec >= 0 && !io_write_ok(node->sock, msec)) {
		return node_binlog_add(node, pkt);
	}

	if (is_module && is_stalling()) {
		return node_binlog_add(node, pkt);
	}

	/* if binlog has entries, we must send those first */
	if (binlog_has_entries(node->binlog)) {
		node_send_binlog(node, pkt);
	}

	/* binlog may still have entries. If so, add to it and return */
	if (binlog_has_entries(node->binlog))
		return node_binlog_add(node, pkt);

	result = node_send(node, pkt, packet_size(pkt), MSG_DONTWAIT);

	/* successfully sent, so add it to the counter and return 0 */
	if (result == packet_size(pkt)) {
		node->stats.events.sent++;
		return 0;
	}

	/*
	 * zero size writes and write errors get stashed in binlog.
	 * From the callers point of view, this counts as a success.
	 */
	if (result <= 0 && !node_binlog_add(node, pkt))
		return 0;

	/* node_send will have marked the node as out of sync now */
	return -1;
}

int node_send_binlog(merlin_node *node, merlin_event *pkt)
{
	merlin_event *temp_pkt;
	int len;

	ldebug("Emptying backlog for %s (%u entries, %s)", node->name,
		   binlog_num_entries(node->binlog), human_bytes(binlog_available(node->binlog)));
	while (io_write_ok(node->sock, 10) && !binlog_read(node->binlog, (void **)&temp_pkt, &len)) {
		int result;
		if (!temp_pkt || packet_size(temp_pkt) != len ||
		    !len || !packet_size(temp_pkt) || packet_size(temp_pkt) > MAX_PKT_SIZE)
		{
			if (!temp_pkt) {
				lerr("BACKLOG: binlog returned 0 but presented no data");
			} else {
				lerr("BACKLOG: binlog returned a packet claiming to be of size %d", packet_size(temp_pkt));
			}
			lerr("BACKLOG: binlog claims the data length is %u", len);
			lerr("BACKLOG: wiping backlog. %s is now out of sync", node->name);
			binlog_wipe(node->binlog, BINLOG_UNLINK);
			return -1;
		}
		errno = 0;
		result = node_send(node, temp_pkt, packet_size(temp_pkt), MSG_DONTWAIT);

		/* keep going while we successfully send something */
		if (result == packet_size(temp_pkt)) {
			node->stats.events.sent++;
			node->stats.events.logged--;
			node->stats.bytes.logged -= packet_size(temp_pkt);

			/*
			 * binlog duplicates the memory, so we must release it
			 * when we've sent and counted it
			 */
			free(temp_pkt);
			continue;
		}

		if (result == BINLOG_EMPTY || !binlog_num_entries(node->binlog))
			binlog_wipe(node->binlog, BINLOG_UNLINK);

		/*
		 * we can recover from total failures by unread()'ing
		 * the entry we just read and then adding the new entry
		 * to the binlog in the hopes that we'll get a
		 * connection up and running again before it's time to
		 * send more data to this node
		 */
		if (result <= 0) {
			if (!binlog_unread(node->binlog, temp_pkt, len)) {
				if (pkt)
					return node_binlog_add(node, pkt);
				return 0;
			} else {
				free(temp_pkt);
			}
		}

		/*
		 * we wrote a partial event or failed to unread the event,
		 * so this node is now out of sync. We must wipe the binlog
		 * and possibly mark this node as being out of sync.
		 */
		lerr("Wiping binlog for %s node %s", node_type(node), node->name);
		binlog_wipe(node->binlog, BINLOG_UNLINK);
		if (pkt) {
			node->stats.events.dropped += node->stats.events.logged + 1;
			node->stats.bytes.dropped += node->stats.bytes.logged + packet_size(pkt);
		}
		node_log_event_count(node, 0);
		return -1;
	}

	return 0;
}

/*
 * Sends a control event with code "code" and selection "selection"
 * to node "node", packing pkt->body with "data" which must be of
 * size "len".
 */
int node_ctrl(merlin_node *node, int code, uint selection, void *data,
			  uint32_t len)
{
	merlin_event pkt;

	if (len > sizeof(pkt.body)) {
		lerr("Attempted to send %u bytes of data when max is %u",
			 len, sizeof(pkt.body));
		bt_scan(NULL, 0);
		return -1;
	}

	memset(&pkt.hdr, 0, HDR_SIZE);

	pkt.hdr.sig.id = MERLIN_SIGNATURE;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;
	gettimeofday(&pkt.hdr.sent, NULL);
	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.len = len;
	pkt.hdr.code = code;
	pkt.hdr.selection = selection & 0xffff;
	if (data)
		memcpy(&pkt.body, data, len);

	return node_send(node, &pkt, packet_size(&pkt), MSG_DONTWAIT);
}

/*
 * Handles all the subtleties regarding CTRL_ACTIVE packets,
 * which also send a sort of compatibility check along with
 * capabilities and attributes about node.
 * node is in this case the source, for which we want to set
 * the proper info structure. Since CTRL_ACTIVE packets are
 * only ever forwarded from daemon to module and from module
 * to 'hood', and never from network to 'hood', we know this
 * packet originated from the module at 'node'.
 *
 * Returns 0 if everything is fine and dandy and info is new
 * Returns -1 on general muppet errors
 * Returns < -1 if node is incompatible with us.
 * Returns 1 if node is compatible, but info isn't new
 * Returns > 1 if node is compatible but lacks features we have
 */
int handle_ctrl_active(merlin_node *node, merlin_event *pkt)
{
	merlin_nodeinfo *info;
	uint32_t len;
	int ret = 0; /* assume ok. we'll flip it if needed */
	int version_delta = 0;

	if (!node || !pkt)
		return -1;

	info = (merlin_nodeinfo *)&pkt->body;
	len = pkt->hdr.len;

	/* if body len is smaller than the least amount of
	 * data we will check, we're too incompatible to check
	 * for and report incompatibilities, so just bail out
	 * with an error
	 */
	if (len < MERLIN_NODEINFO_MINSIZE) {
		lerr("FATAL: %s: incompatible nodeinfo body size %d. Ours is %d. Required: %d",
			 node->name, len, sizeof(node->info), MERLIN_NODEINFO_MINSIZE);
		lerr("FATAL: Completely incompatible");
		return -128;
	}

	/*
	 * Basic check first, so people know what to expect of the
	 * comparisons below, but if byte_order differs, so may this.
	 */
	version_delta = info->version - MERLIN_NODEINFO_VERSION;
	if (version_delta) {
		lwarn("%s: incompatible nodeinfo version %d. Ours is %d",
			  node->name, info->version, MERLIN_NODEINFO_VERSION);
		lwarn("Further compatibility comparisons may be wrong");
	}

	/*
	 * these two checks should never trigger for the daemon
	 * when node is &ipc unless someone hacks merlin to connect
	 * to a remote site instead of over the ipc socket.
	 * It will happen in net-to-net cases where the two systems
	 * have different wordsize (32-bit vs 64-bit) or byte order
	 * (big vs little endian, fe)
	 * If someone wants to jack in conversion functions into
	 * merlin, the right place to activate them would be here,
	 * setting them as in_handler(pkt) for the node in question
	 * (no out_handler() is needed, since the receiving end will
	 * transform the packet itself).
	 */
	if (info->word_size != COMPAT_WORDSIZE) {
		lerr("FATAL: %s: incompatible wordsize %d. Ours is %d",
			 node->name, info->word_size, COMPAT_WORDSIZE);
		ret -= 4;
	}
	if (info->byte_order != endianness()) {
		lerr("FATAL: %s: incompatible byte order %d. Ours is %d",
		     node->name, info->byte_order, endianness());
		ret -= 8;
	}

	/*
	 * this could potentially happen if someone forgets to
	 * restart either Nagios or Merlin after upgrading either
	 * or both to a newer version and the object structure
	 * version has been bumped. It's quite unlikely though,
	 * but since CTRL_ACTIVE packets are so uncommon we can
	 * afford to waste the extra cycles.
	 */
	if (info->object_structure_version != CURRENT_OBJECT_STRUCTURE_VERSION) {
		lerr("FATAL: %s: incompatible object structure version %d. Ours is %d",
			 node->name, info->object_structure_version, CURRENT_OBJECT_STRUCTURE_VERSION);
		ret -= 16;
	}

	/*
	 * If the remote end has a newer info_version we can be reasonably
	 * sure that everything we want from it is present
	 */
	if (!ret) {
		if (version_delta > 0 && len > sizeof(node->info)) {
			/*
			 * version is greater and struct is bigger. Everything we
			 * need is almost certainly present in that struct
			 */
			lwarn("WARNING: Possible compatibility issues with '%s'", node->name);
			lwarn("  - '%s' nodeinfo version %d; nodeinfo size %d.",
				  node->name, info->version, len);
			lwarn("  - we have nodeinfo version %d; nodeinfo size %d.",
				  self->version, sizeof(node->info));
			len = sizeof(node->info);
		} else if (version_delta < 0 && len < sizeof(node->info)) {
			/*
			 * version is less, and struct is smaller. Update this
			 * place with warnings about what won't work when we
			 * add new things to the info struct, and ignore copying
			 * anything right now
			 */
			lwarn("WARNING: '%s' needs to be updated", node->name);
			ret -= 2;
		} else if (version_delta && len != sizeof(node->info)) {
			/*
			 * version is greater and struct is smaller, or
			 * version is lesser and struct is bigger. Either way,
			 * this should never happen
			 */
			lerr("FATAL: %s: impossible info_version / sizeof(nodeinfo_version) combo",
				 node->name);
			lerr("FATAL: %s: %d / %d; We: %d / %d",
				 node->name, len, info->version, sizeof(node->info), MERLIN_NODEINFO_VERSION);
			ret -= 32;
		}
		if (node->type == MODE_PEER) {
			if (info->configured_peers != ipc.info.configured_peers) {
				lerr("Node %s has a different number of peers from us", node->name);
				ret -= 512;
			} else if (info->configured_masters != ipc.info.configured_masters) {
				lerr("Node %s has a different number of masters from us", node->name);
				ret -= 512;
			} else if (info->configured_pollers != ipc.info.configured_pollers) {
				lerr("Node %s has a different number of pollers from us", node->name);
				ret -= 512;
			}
		} else if (node->type == MODE_POLLER) {
			if (info->configured_masters != ipc.info.configured_peers + 1) {
				lerr("Node %s is a poller, but it has a different number of masters than we have peers", node->name);
				ret -= 512;
			} else if (info->configured_peers > ipc.info.configured_pollers) {
				lerr("Node %s is a poller, but it has more peers than we have pollers", node->name);
				ret -= 512;
			}
		}
		if (!ret && info->last_cfg_change != ipc.info.last_cfg_change) {
			linfo("Node %s's config isn't in sync with ours", node->name);
			ret -= 256;
		}
	}

	if (ret < 0 && ret != -256 && ret != -512) {
		lerr("FATAL: %s; incompatibility code %d. Ignoring CTRL_ACTIVE event",
			 node->name, ret);
		memset(&node->info, 0, sizeof(node->info));
		return ret;
	}

	/* everything seems ok, so handle it properly */


	/* if info isn't new, we return 1 */
	if (!memcmp(&node->info.start, &info->start, sizeof(info->start)) &&
		node->info.last_cfg_change == info->last_cfg_change &&
		!strcmp((char *)node->info.config_hash, (char *)info->config_hash))
	{
		ret = 1;
	}

	/*
	 * otherwise update the node's info and
	 * print some debug logging.
	 */
	memcpy(&node->info, pkt->body, len);
	if (!ret) {
		ldebug("Received CTRL_ACTIVE from %s", node->name);
		ldebug("      version: %u", info->version);
		ldebug("    word_size: %u", info->word_size);
		ldebug("   byte_order: %u", info->byte_order);
		ldebug("object struct: %u", info->object_structure_version);
		ldebug("   start time: %lu.%lu",
			   info->start.tv_sec, info->start.tv_usec);
		ldebug("  config hash: %s", tohex(info->config_hash, 20));
		ldebug(" config mtime: %lu", info->last_cfg_change);
		ldebug("      peer id: %u", node->peer_id);
		ldebug(" self peer id: %u", info->peer_id);
		ldebug(" active peers: %u", info->active_peers);
		ldebug(" confed peers: %u", info->configured_peers);
	}

	return ret;
}
