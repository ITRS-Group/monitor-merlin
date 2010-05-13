#include "shared.h"

static char *binlog_dir = "/opt/monitor/op5/merlin/binlogs";

void node_log_event_count(merlin_node *node, int force)
{
	struct timeval now;
	merlin_event_counter *cnt = &node->events;

	/*
	 * This works like a 'mark' that syslogd produces. We log once
	 * every 60 seconds
	 */
	gettimeofday(&now, NULL);
	if (!force && cnt->last_logged && cnt->last_logged + 60 > now.tv_sec)
		return;

	cnt->last_logged = now.tv_sec;

	linfo("Handled %lld events from/to %s in %s. in: %lld, out: %lld",
	      cnt->read + cnt->sent + cnt->dropped + cnt->logged, node->name,
		  tv_delta(&cnt->start, &now),
	      cnt->read, cnt->sent + cnt->dropped + cnt->logged);
	if (!(cnt->sent + cnt->dropped + cnt->logged))
		return;
	linfo("'%s' event details: read %lld, sent %lld, dropped %lld, logged %lld",
	      node->name, cnt->read, cnt->sent, cnt->dropped, cnt->logged);
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
	node->zread = 0;
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
		node->events.dropped += node->events.logged + 1;
		node->events.logged = 0;
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

	node->events.read++;
	node->last_recv = time(NULL);

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
			node->events.dropped += node->events.logged + 1;
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
		node->events.sent++;
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
