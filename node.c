#include "shared.h"
#include <netdb.h>

merlin_node **noc_table, **poller_table, **peer_table;

static int num_selections;
static node_selection *selection_table;

static char *binlog_dir = "/opt/monitor/op5/merlin/binlogs";

void node_set_state(merlin_node *node, int state)
{
	if (!node)
		return;

	if (node->state == state)
		return;

	if (node->action)
		node->action(node, state);

	if (state == STATE_CONNECTED && node->sock >= 0) {
		int snd, rcv;
		socklen_t size = sizeof(int);

		set_socket_options(node->sock, (int)node->ioc.ioc_bufsize);
		getsockopt(node->sock, SOL_SOCKET, SO_SNDBUF, &snd, &size);
		getsockopt(node->sock, SOL_SOCKET, SO_SNDBUF, &rcv, &size);
		ldebug("send / receive buffers are %s / %s for node %s",
			   human_bytes(snd), human_bytes(rcv), node->name);
	}
	node->state = state;
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

	/*
	 * Sort nodes by type. This way, we can keep them all linear
	 * while each has its own table base pointer and still not waste
	 * much memory. Pretty nifty, really.
	 */
	node_table = calloc(num_nodes, sizeof(merlin_node *));
	noc_table = node_table;
	peer_table = &node_table[num_nocs];
	poller_table = &node_table[num_nocs + num_peers];

	xnoc = xpeer = xpoll = 0;
	for (i = 0; i < n; i++) {
		merlin_node *node = &table[i];

		switch (node->type) {
		case MODE_NOC:
			node->id = xnoc;
			noc_table[xnoc++] = node;
			break;
		case MODE_PEER:
			node->id = num_nocs + xpeer;
			peer_table[xpeer++] = node;
			break;
		case MODE_POLLER:
			node->id = num_nocs + num_peers + xpoll;
			poller_table[xpoll++] = node;
			break;
		}
	}
}

#define MRLN_ADD_NODE_FLAG(word) { MERLIN_NODE_##word, #word }
struct {
	int code;
	const char *key;
} node_config_flags[] = {
	MRLN_ADD_NODE_FLAG(TAKEOVER),
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

	if (code != -1)
		return -1;

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

	if (!node)
		return;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!v->value)
			cfg_error(c, v, "Variable must have a value\n");

		if (node->type != MODE_NOC && !strcmp(v->key, "hostgroup")) {
			sel_id = add_selection(v->value, node);
		}
		else if (!strcmp(v->key, "address") || !strcmp(v->key, "host")) {
			if (resolve(v->value, &node->sain.sin_addr) < 0)
				cfg_error(c, v, "Unable to resolve '%s'\n", v->value);
		}
		else if (!strcmp(v->key, "port")) {
			node->sain.sin_port = htons((unsigned short)atoi(v->value));
			if (!node->sain.sin_port)
				cfg_error(c, v, "Illegal value for port: %s\n", v->value);
		}
		else if (grok_node_flag(&node->flags, v->key, v->value) < 0) {
			cfg_error(c, v, "Unknown variable\n");
		}
	}

	for (i = 0; i < c->nested; i++) {
		struct cfg_comp *comp = c->nest[i];
		if (!strcmp(comp->name, "object_config")) {
			node->csync = calloc(1, sizeof(*node->csync));
			if (!node->csync)
				cfg_error(comp, NULL, "Failed to allocate memory for confsync struct");
			grok_confsync_compound(comp, node->csync);
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
	dura = tv_delta(&self.start, &now);

	b_in = s->bytes.read;
	b_out = s->bytes.sent + s->bytes.logged + s->bytes.dropped;
	e_in = s->events.read;
	e_out = s->events.sent + s->events.logged + s->events.dropped;
	linfo("Handled %llu events from/to %s in %s. in: %llu, out: %llu",
		  e_in + e_out, node->name, dura, e_in, e_out);
	linfo("Handled %s from/to %s in %s. in: %s, out: %s",
		  human_bytes(b_in + b_out), node->name, dura,
		  human_bytes(b_in), human_bytes(b_out));
	if (!e_out)
		return;
	linfo("%s events/bytes: read %llu/%s, sent %llu/%s, dropped %llu/%s, logged %llu/%s",
	      node->name, e_in, human_bytes(b_in),
		  s->events.sent, human_bytes(s->bytes.sent),
		  s->events.dropped, human_bytes(s->bytes.dropped),
		  s->events.logged, human_bytes(s->bytes.logged));
}

const char *node_state(merlin_node *node)
{
	switch (node->state) {
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
	if (node->state == STATE_CONNECTED)
		node_log_event_count(node, 1);

	/* avoid spurious close() errors while strace/valgrind debugging */
	if (node->sock >= 0)
		close(node->sock);
	node_set_state(node, STATE_NONE);
	node->last_recv = 0;
	node->sock = -1;
	node->ioc.ioc_buflen = node->ioc.ioc_offset = 0;
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
 * Read as much data as we possibly can from the node so
 * that whatever parsing code there is can handle it later.
 * All information the caller needs will reside in the
 * nodes own merlin_iocache function, and we return the
 * number of bytes read, or -1 on errors.
 * The io-cache buffer must be allocated before we get
 * to this point, and if the caller wants to poll the
 * socket for input, it'll have to do so itself.
 */
int node_recv(merlin_node *node, int flags)
{
	int to_read, bytes_read;
	merlin_iocache *ioc = &node->ioc;

	if (!node || node->sock < 0) {
		return -1;
	}

	/*
	 * first check if we've managed to read our fill. This
	 * prevents us from ending up in an infinite loop in case
	 * we hit the bufsize with both buflen and offset
	 */
	if (ioc->ioc_offset >= ioc->ioc_buflen)
		ioc->ioc_offset = ioc->ioc_buflen = 0;

	to_read = ioc->ioc_bufsize - ioc->ioc_buflen;
	flags |= MSG_NOSIGNAL;
	bytes_read = recv(node->sock, ioc->ioc_buf + ioc->ioc_buflen, to_read, flags);

	/*
	 * If we read something, update the stat counter
	 * and return. The caller will have to handle the
	 * input as it sees fit
	 */
	if (bytes_read > 0) {
		ioc->ioc_buflen += bytes_read;
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
		lerr("Failed to recv() %d bytes from %s node %s: %s",
		     to_read, node_type(node), node->name, strerror(errno));
		ldebug("sock: %d; buf: %p; buflen: %lu; offset: %lu; bufsize: %lu",
			   node->sock, ioc->ioc_buf, ioc->ioc_buflen, ioc->ioc_offset, ioc->ioc_bufsize);
			   
	}
	node_disconnect(node);
	return -1;
}

/*
 * wraps io_send_all() and adds proper error handling when we run
 * into sending errors. It's up to the caller to poll the socket
 * for writability, or pass the proper flags and ignore errors
 */
int node_send(merlin_node *node, void *data, int len, int flags)
{
	int sent;

	if (!node || node->sock < 0)
		return 0;

	flags |= MSG_NOSIGNAL;
	sent = io_send_all(node->sock, data, len);
	/* success. Should be the normal case */
	if (sent == len) {
		node->stats.bytes.sent += sent;
		node->last_sent = time(NULL);
		return sent;
	}

	if (sent < 0) {
		/* if we would have blocked, we simply return 0 */
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;

		/* otherwise we log the error and disconnect the node */
		lerr("Failed to send(%d, %p, %d, %d) to %s: %s",
			 node->sock, data, len, flags, node->name, strerror(errno));
		node_disconnect(node);
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
	merlin_iocache *ioc = &node->ioc;
	int available;

	/*
	 * if we've read it all, mark the buffer as such so we can
	 * read as much as possible the next time we read. If we don't
	 * do this, we might end up with an infinite loop since it's
	 * possible we run into buflen and offset both being the same
	 * as bufsize, and thus we will issue zero-size reads and
	 * never get any new events from the node.
	 *
	 * This must come before we assign pkt into the ioc->ioc_buf,
	 * since we may otherwise try to read beyond the end of the
	 * buffer.
	 */
	if (ioc->ioc_offset >= ioc->ioc_buflen) {
		ioc->ioc_offset = ioc->ioc_buflen = 0;
		return NULL;
	}

	pkt = (merlin_event *)(ioc->ioc_buf + ioc->ioc_offset);

	/*
	 * If one event lacks a complete header, we mustn't try to access
	 * the event struct, or we'll run headlong into a SIGSEGV when the
	 * start of the header but not the hdr.len part fits between
	 * buf[offset] and buf[buflen].
	 *
	 * We must also check if body of the packet isn't complete in
	 * the buffer.
	 *
	 * When either of those happen, we move the remainder of the buf
	 * to the start of it and set the offsets and counters properly
	 */
	available = ioc->ioc_buflen - ioc->ioc_offset;
	if ((int)HDR_SIZE > available || packet_size(pkt) > available) {
		ldebug("IOC: moving %u bytes from %p to %p. buflen: %lu; bufsize: %lu",
			   available, ioc->ioc_buf + ioc->ioc_offset, ioc->ioc_buf, ioc->ioc_buflen, ioc->ioc_bufsize);
		memmove(ioc->ioc_buf, ioc->ioc_buf + ioc->ioc_offset, available);
		ioc->ioc_buflen = available;
		ioc->ioc_offset = 0;
		return NULL;
	}
	node->stats.events.read++;
	ioc->ioc_offset += packet_size(pkt);
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

	pkt->hdr.protocol = MERLIN_PROTOCOL_VERSION;
	if (is_module) {
		gettimeofday(&pkt->hdr.sent, NULL);
	}

	if (pkt->hdr.type == CTRL_PACKET) {
		ldebug("Sending %s to %s", ctrl_name(pkt->hdr.code), node->name);
		if (pkt->hdr.code == CTRL_ACTIVE) {
			merlin_nodeinfo *info = (merlin_nodeinfo *)&pkt->body;
			ldebug("   start time: %lu.%lu",
			       info->start.tv_sec, info->start.tv_usec);
			ldebug("  config hash: %s", tohex(info->config_hash, 20));
			ldebug(" config mtime: %lu", info->last_cfg_change);
		}
	}

	if (packet_size(pkt) > TOTAL_PKT_SIZE) {
		lerr("header is invalid, or packet is too large. aborting");
		return -1;
	}

	if (node->sock < 0 || node->state == STATE_NONE) {
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

	/* possibly mark the node as out of sync here */
	return -1;
}

int node_send_binlog(merlin_node *node, merlin_event *pkt)
{
	merlin_event *temp_pkt;
	uint len;

	ldebug("Emptying backlog for %s", node->name);
	while (io_write_ok(node->sock, 10) && !binlog_read(node->binlog, (void **)&temp_pkt, &len)) {
		int result;
		if (!temp_pkt || packet_size(temp_pkt) != (int)len ||
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
			  uint32_t len, int msec)
{
	merlin_event pkt;

	if (len > sizeof(pkt.body)) {
		lerr("Attempted to send %u bytes of data when max is %u",
			 len, sizeof(pkt.body));
		bt_scan(NULL, 0);
		return -1;
	}

	memset(&pkt.hdr, 0, HDR_SIZE);

	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.len = len;
	pkt.hdr.code = code;
	pkt.hdr.selection = selection & 0xffff;
	if (data)
		memcpy(&pkt.body, data, len);

	return node_send_event(node, &pkt, msec);
}
