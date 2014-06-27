#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>
#include "daemon.h"

#define MERLIN_CONNECT_TIMEOUT 20 /* the (hardcoded) connect timeout we use */
#define MERLIN_CONNECT_INTERVAL 5 /* connect interval */

static int net_sock = -1; /* listening sock descriptor */

static unsigned short net_source_port(merlin_node *node)
{
	return ntohs(node->sain.sin_port) + default_port;
}

/* do a node lookup based on name *or* ip-address + port */
merlin_node *find_node(struct sockaddr_in *sain, const char *name)
{
	uint i;
	merlin_node *first = NULL;

	if (!sain)
		return NULL;

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		unsigned short source_port = ntohs(sain->sin_port);
		unsigned short in_port = net_source_port(node);
		if (node->sain.sin_addr.s_addr == sain->sin_addr.s_addr) {
			if (source_port == in_port) {
				/* perfect match */
				ldebug("Inbound connection matches %s exactly (%s:%d)",
				       node->name, inet_ntoa(sain->sin_addr), in_port);
				return node;
			}

			if (!first && !(node->flags & MERLIN_NODE_FIXED_SRCPORT))
				first = node;
		}
	}

	if (first) {
		lwarn("Inbound connection presumably from %s (%s:%d != %s:%d)",
			  first->name,
			  inet_ntoa(sain->sin_addr), ntohs(sain->sin_port),
			  inet_ntoa(first->sain.sin_addr), net_source_port(first));
	}

	return first;
}


/*
 * checks if a socket is connected or not by looking up the ip and port
 * of the remote host.
 * Returns the merlin connection state if connected (CONNECTED vs NEGOTIATING), and 0 if not.
 */
int net_is_connected(merlin_node *node)
{
	struct sockaddr_in sain;
	socklen_t slen;
	int optval = 0, gsoerr = 0, gsores = 0, gpnres = 0, gpnerr = 0;

	if (!node || node->sock < 0)
		return 0;

	if (node->state == STATE_CONNECTED)
		return node->state;

	if (node->state != STATE_PENDING)
		return 0;

	/*
	 * yes, getpeername() actually has to be here, or getsockopt()
	 * won't return errors when we're not yet connected. It's
	 * important that we read the socket error state though, or
	 * some older kernels will maintain the link in SYN_SENT state
	 * more or less indefinitely, so get all the syscalls taken
	 * care of no matter if they actually work or not.
	 */
	errno = 0;
	slen = sizeof(struct sockaddr_in);
	gpnres = getpeername(node->sock, (struct sockaddr *)&sain, &slen);
	gpnerr = errno;
	slen = sizeof(optval);
	gsores = getsockopt(node->sock, SOL_SOCKET, SO_ERROR, &optval, &slen);
	gsoerr = errno;
	if (!gpnres && !gsores && !optval && !gpnerr && !gsoerr) {
		node_set_state(node, STATE_NEGOTIATING, "connect() attempt completed successfully. Negotiating...");
		return node->state;
	}

	if (optval) {
		node_disconnect(node, "connect() to %s node %s failed: %s",
						node_type(node), node->name, strerror(optval));
		return 0;
	}

	if (gsores < 0 && gsoerr != ENOTCONN) {
		node_disconnect(node, "getsockopt(%d) failed for %s node %s: %s",
						node->sock, node_type(node), node->name, strerror(gsoerr));
	}

	if (gpnres < 0 && gpnerr != ENOTCONN) {
		lerr("getpeername(%d) failed for %s: %s",
			 node->sock, node->name, strerror(gpnerr));
	}

	/*
	 * if a connection is in progress, we should be getting
	 * ENOTCONN, but we need to give it time to complete
	 * first. 30 seconds should be enough.
	 */
	if (node->last_conn_attempt + MERLIN_CONNECT_TIMEOUT < time(NULL)) {
		node_disconnect(node, "connect() timed out after %d seconds",
						MERLIN_CONNECT_TIMEOUT);
	}

	return 0;
}


/*
 * Initiate a connection attempt to a node and mark it as PENDING.
 * Note that since we're using sockets in non-blocking mode (in order
 * to be able to effectively multiplex), the connection attempt will
 * never be completed in this function
 */
int net_try_connect(merlin_node *node)
{
	int sockopt = 1;
	struct sockaddr *sa = (struct sockaddr *)&node->sain;
	int should_log = 0;
	struct timeval connect_timeout = { MERLIN_CONNECT_TIMEOUT, 0 };
	struct sockaddr_in sain;
	time_t interval = MERLIN_CONNECT_INTERVAL;

	/* don't log obsessively */
	if (node->last_conn_attempt_logged + 30 <= time(NULL)) {
		should_log = 1;
		node->last_conn_attempt_logged = time(NULL);
	}

	if (!(node->flags & MERLIN_NODE_CONNECT)) {
		if (should_log) {
			linfo("Connect attempt blocked by config to %s node %s",
				  node_type(node), node->name);
		}
		return 0;
	}

	/* if it's not yet time to connect, don't even try it */
	if (node->last_conn_attempt + interval > time(NULL)) {
		return 0;
	}

	/* mark the time so we can time it out ourselves if need be */
	node->last_conn_attempt = time(NULL);

	/* create the socket if necessary */
	if (node->sock < 0) {
		node_disconnect(node, "struct reset (no real disconnect)");
		node->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (node->sock < 0) {
			lerr("Failed to obtain socket for node %s: %s", node->name, strerror(errno));
			lerr("Aborting connection attempt to %s", node->name);
			return -1;
		}
	}

	/* Check if a pending attempt was successfully completed */
	if (net_is_connected(node))
		return 0;

	sa->sa_family = AF_INET;
	if (should_log) {
		linfo("Connecting to %s %s@%s:%d", node_type(node), node->name,
		      inet_ntoa(node->sain.sin_addr),
		      ntohs(node->sain.sin_port));
	}

	(void)setsockopt(node->sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(int));
	if (node->flags & MERLIN_NODE_FIXED_SRCPORT) {
		ldebug("Using fixed source port for %s node %s",
			   node_type(node), node->name);
		/*
		 * first we bind() to a local port calculated by our own
		 * listening port + the target port.
		 */
		sain.sin_family = AF_INET;
		sain.sin_port = htons(net_source_port(node));
		sain.sin_addr.s_addr = 0;
		if (bind(node->sock, (struct sockaddr *)&sain, sizeof(sain))) {
			lerr("Failed to bind() outgoing socket for node %s to port %d: %s",
				 node->name, ntohs(sain.sin_port), strerror(errno));
		}
	}

	if (fcntl(node->sock, F_SETFL, O_NONBLOCK) < 0) {
		lwarn("Failed to set socket for %s non-blocking: %s", node->name, strerror(errno));
	}
	if (setsockopt(node->sock, SOL_SOCKET, SO_RCVTIMEO,
	               &connect_timeout, sizeof(connect_timeout)) < 0)
	{
		ldebug("Failed to set receive timeout for node %s: %s",
		       node->name, strerror(errno));
	}
	if (setsockopt(node->sock, SOL_SOCKET, SO_SNDTIMEO,
	               &connect_timeout, sizeof(connect_timeout)) < 0)
	{
		ldebug("Failed to set send timeout for node %s: %s",
		       node->name, strerror(errno));
	}

	if (connect(node->sock, sa, sizeof(struct sockaddr_in)) < 0) {
		if (errno == EISCONN && net_is_connected(node)) {
			return 0;
		}
		else if (errno == EINPROGRESS) {
			node_set_state(node, STATE_PENDING, "Connecting");
		}
		else if (errno == EALREADY) {
			node_set_state(node, STATE_PENDING, "connect() already in progress");
		} else {
			/* connection error */
			if (should_log) {
				node_disconnect(node, "connect() failed to %s node '%s' (%s:%d): %s",
								node_type(node), node->name,
								inet_ntoa(node->sain.sin_addr),
								ntohs(node->sain.sin_port),
								strerror(errno));
			} else {
				node_disconnect(node, NULL);
			}
			return -1;
		}
	}

	return 0;
}

/*
 * Negotiate which socket to use for communication when the remote
 * host has accepted a connection attempt from us while we have
 * accepted one from the remote host. This shouldn't happen very
 * often, but if it does we must make sure both ends agree on one
 * socket to use.
 * con is the one that might be in a connection attempt
 * lis is the one we found with accept.
 */
static int net_negotiate_socket(merlin_node *node, int lis)
{
	int con;
	struct sockaddr_in lissain, consain;
	socklen_t slen = sizeof(struct sockaddr_in);

	linfo("Negotiating socket for %s %s", node_type(node), node->name);
	con = node->sock;

	/* we prefer the socket with the lowest ip-address */
	if (getsockname(lis, (struct sockaddr *)&lissain, &slen) < 0) {
		lerr("negotiate: getsockname(%d, ...) failed: %s",
			 lis, strerror(errno));
		close(lis);
		return con;
	}

	if (getpeername(con, (struct sockaddr *)&consain, &slen) < 0) {
		lerr("negotiate: getpeername(%d, ...) failed: %s",
			 con, strerror(errno));
		close(con);
		return lis;
	}

	ldebug("negotiate. lis(%d): %s:%d", lis,
		   inet_ntoa(lissain.sin_addr), ntohs(lissain.sin_port));
	ldebug("negotiate. con(%d): %s:%d", con,
		   inet_ntoa(consain.sin_addr), ntohs(consain.sin_port));

	if (lissain.sin_addr.s_addr > consain.sin_addr.s_addr) {
		ldebug("negotiate: con has lowest ip. using that");
		close(lis);
		return con;
	}
	if (consain.sin_addr.s_addr > lissain.sin_addr.s_addr) {
		ldebug("negotiate: lis has lowest ip. using that");
		close(con);
		return lis;
	}

	/*
	 * this will happen if multiple merlin instances run
	 * on the same server, such as when we're testing
	 * things. In that case, let the portnumber decide
	 * the tiebreak
	 */
	if (lissain.sin_port > consain.sin_port) {
		ldebug("negotiate: con has lowest port. using that");
		close(lis);
		return con;
	}
	if (consain.sin_port > lissain.sin_port) {
		ldebug("negotiate: lis has lowest port. using that");
		close(con);
		return lis;
	}

	ldebug("negotiate: con and lis are equal. killing both");
	node->last_conn_attempt_logged = 0;
	node_disconnect(node, "socket negotiation failed");
	close(lis);

	return -1;
}


/*
 * Accept an inbound connection from a remote host
 * Returns 0 on success and -1 on errors
 */
int net_accept_one(void)
{
	int sock;
	merlin_node *node;
	struct sockaddr_in sain;
	socklen_t slen = sizeof(struct sockaddr_in);

	/*
	 * we get called from polling_loop(). If so, check for readability
	 * to see if anyone has connected and, if not, return early
	 */
	if (!io_read_ok(net_sock, 0))
		return -1;

	sock = accept(net_sock, (struct sockaddr *)&sain, &slen);
	if (sock < 0) {
		lerr("accept() failed: %s", strerror(errno));
		return -1;
	}

	node = find_node(&sain, NULL);
	linfo("%s connected from %s:%d. Current state is %s",
		  node ? node->name : "An unregistered node",
		  inet_ntoa(sain.sin_addr), ntohs(sain.sin_port),
		  node ? node_state_name(node->state) : "unknown");
	if (!node) {
		close(sock);
		return 0;
	}

	switch (node->state) {
	case STATE_NEGOTIATING: /* ending up here is probably a bug */
		/* fallthrough */
	case STATE_CONNECTED: case STATE_PENDING:
		/* if node->sock >= 0, we must negotiate which one to use */
		node->sock = net_negotiate_socket(node, sock);
		break;

	case STATE_NONE:
		/*
		 * we must close it unconditionally or we'll leak fd's
		 * for reconnecting nodes that were previously connected
		 */
		node_disconnect(node, "fd leak prevention for connecting nodes");
		node->sock = sock;
		break;

	default:
		lerr("%s %s has an unknown status", node_type(node), node->name);
		break;
	}

	node_set_state(node, STATE_NEGOTIATING, "Inbound connection accepted. Negotiating protocol version");

	return sock;
}


/* close all sockets and release the memory used by
 * static global vars for networking purposes */
int net_deinit(void)
{
	uint i;

	for (i = 0; i < num_nodes; i++) {
		node_disconnect(node_table[i], "Deinitializing networking");
	}

	if (node_table)
		free(node_table);

	return 0;
}


/*
 * set up the listening socket (if applicable)
 */
int net_init(void)
{
	int result, sockopt = 1;
	struct sockaddr_in sain, inbound;
	struct sockaddr *sa = (struct sockaddr *)&sain;
	socklen_t addrlen = sizeof(inbound);

	if (!num_nodes)
		return 0;

	sain.sin_addr.s_addr = default_addr;
	sain.sin_port = htons(default_port);
	sain.sin_family = AF_INET;

	net_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (net_sock < 0)
		return -1;

	merlin_set_socket_options(net_sock, 0);

	/* if this fails we can do nothing but try anyway */
	(void)setsockopt(net_sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(int));

	result = bind(net_sock, sa, addrlen);
	if (result < 0)
		return -1;

	result = listen(net_sock, SOMAXCONN);
	if (result < 0)
		return -1;

	return 0;
}

/* send a specific packet to a specific host */
static int net_sendto(merlin_node *node, merlin_event *pkt)
{
	if (!pkt || !node) {
		lerr("net_sendto() called with neither node nor pkt");
		return -1;
	}

	return node_send_event(node, pkt, 100);
}

static int net_sendto_many(merlin_node **ntable, uint num, merlin_event *pkt)
{
	uint i;

	if (!ntable || !pkt || !num || !*ntable)
		return -1;

	for (i = 0; i < num; i++) {
		merlin_node *node = ntable[i];
		net_sendto(node, pkt);
	}

	return 0;
}


/*
 * If a node hasn't been heard from in too long, we mark it as no
 * longer connected and send a CTRL_INACTIVE event to the module,
 * signalling that our Nagios should, potentially, take over checks
 * for the awol node
 */
static void check_node_activity(merlin_node *node)
{
	time_t now = time(NULL);

	if (node->sock == -1 || node->state != STATE_CONNECTED)
		return;

	/* this one's on a reaaaally slow link */
	if (!node->data_timeout)
		return;

	if (node->last_recv < now - node->data_timeout)
		node_disconnect(node, "Too long since last action");
}


/*
 * Passes an event from a remote node to the broker module,
 * any and all nocs and the database handling routines. The
 * exception to this rule is control packets from peers and
 * pollers, which never get forwarded to our masters.
 */
static int handle_network_event(merlin_node *node, merlin_event *pkt)
{
	uint i;

	if (pkt->hdr.type == CTRL_PACKET) {
		if (pkt->hdr.code == CTRL_ACTIVE) {
			ldebug("Got CTRL_ACTIVE from %s", node->name);
			int result = handle_ctrl_active(node, pkt);

			if (result < 0) {
				if (result == -512) {
					node_disconnect(node, "Incompatible merlin configuration");
				} else if (result == -256) {
					csync_node_active(node);
					node_disconnect(node, "Out of date object configuration");
				} else {
					node_disconnect(node, "Incompatible protocol");
				}
				return 0;
			}

			node_set_state(node, STATE_CONNECTED, "Version and protocol successfully negotiated");
		}
		if (pkt->hdr.code == CTRL_INACTIVE) {
			ldebug("Module @ %s is INACTIVE", node->name);
			db_mark_node_inactive(node);
		}
	} else if (node->state != STATE_CONNECTED) {
		/* yeah, sarry, but if yer nat on the list, yer nat gettin' in */
		return 0;
	} else if (node->type == MODE_POLLER && num_masters) {
		ldebug("Passing on event from poller %s to %d masters",
		       node->name, num_masters);
		net_sendto_many(noc_table, num_masters, pkt);
	} else if (node->type == MODE_MASTER && num_pollers) {
		/*
		 * @todo maybe we should also check if self.peer_id == 0
		 * before we forward events to our pollers. Hmm...
		 */
		if (pkt->hdr.type != NEBCALLBACK_PROGRAM_STATUS_DATA &&
		    pkt->hdr.type != NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA)
		{
			for (i = 0; i < num_pollers; i++) {
				merlin_node *n = poller_table[i];
				net_sendto(n, pkt);
			}
		}
	}

	/*
	 * let the various handler know which node sent the packet
	 */
	pkt->hdr.selection = node->id;

	/* not all packets get delivered to the merlin module */
	switch (pkt->hdr.type) {
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		/*
		 * PROGRAM_STATUS_DATA can't sanely be transferred
		 * CONTACT_NOTIFICATION_METHOD is left as-is, since we by
		 * default want pollers to send notifications for their
		 * respective contacts. This is by customer request, since
		 * sending text-messages across country borders is a lot
		 * more expensive than just buying a GSM device extra for
		 * where one wants to place the poller
		 */
		mrm_db_update(node, pkt);
		return 0;

	/* and not all packets get sent to the database */
	case CTRL_PACKET:
		/* the module doesn't need to know about negotiations */
		if (pkt->hdr.code != CTRL_ACTIVE && pkt->hdr.code != CTRL_INACTIVE)
			break;
		/*@fallthrough@*/
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		return ipc_send_event(pkt);

	case NEBCALLBACK_DOWNTIME_DATA:
	case NEBCALLBACK_COMMENT_DATA:
		/*
		 * These two used to be handled specially here, but
		 * we've moved it to the database update layer instead,
		 * which will discard queries it can't run properly
		 * without bouncing the data against the module.
		 */
	default:
		/*
		 * IMPORTANT NOTE:
		 * It's absolutely vital that we send the event to the
		 * ipc socket *before* we ship it off to the db_update
		 * function, since the db_update function merlin_decode()'s
		 * the event, which makes unusable for sending to the
		 * ipc (or, indeed, anywhere else) afterwards.
		 */
		ipc_send_event(pkt);
		mrm_db_update(node, pkt);
		return 0;
	}

	return 0;
}


/*
 * Reads input from a particular node and ships it off to
 * the "handle_network_event()" routine up above
 */
static int net_input(merlin_node *node)
{
	merlin_event *pkt;
	int len, events = 0;

	errno = 0;
	len = node_recv(node);
	if (len < 0) {
		return 0;
	}
	node->stats.bytes.read += len;
	node->last_recv = time(NULL);

	while ((pkt = node_get_event(node))) {
		events++;
		handle_network_event(node, pkt);
	}
	ldebug("Read %d events in %s from %s node %s",
		   events, human_bytes(len), node_type(node), node->name);

	return events;
}


/*
 * Sends an event read from the ipc socket to the appropriate nodes
 */
int net_send_ipc_data(merlin_event *pkt)
{
	uint i, ntable_stop = num_masters + num_peers;
	linked_item *li;

	if (!num_nodes)
		return 0;

	/*
	 * The module can mark certain packets with a magic destination.
	 * Such packets avoid all other inspection and get sent to where
	 * the module wants us to.
	 */
	if (magic_destination(pkt)) {
		if ((pkt->hdr.selection & DEST_MASTERS) == DEST_MASTERS) {
			for (i = 0; i < num_masters; i++) {
				net_sendto(node_table[i], pkt);
			}
		}
		if ((pkt->hdr.selection & DEST_PEERS) == DEST_PEERS) {
			for (i = 0; i < num_peers; i++) {
				net_sendto(peer_table[i], pkt);
			}
		}
		if ((pkt->hdr.selection & DEST_POLLERS) == DEST_POLLERS) {
			for (i = 0; i < num_pollers; i++) {
				net_sendto(poller_table[i], pkt);
			}
		}

		return 0;
	}

	/*
	 * "normal" packets get sent to all peers and masters, and possibly
	 * a group of, or all, pollers as well
	 */

	/* general control packets are for everyone */
	if (pkt->hdr.selection == CTRL_GENERIC && pkt->hdr.type == CTRL_PACKET) {
		ntable_stop = num_nodes;
	}

	/* Send this to all who should have it */
	for (i = 0; i < ntable_stop; i++) {
		net_sendto(node_table[i], pkt);
	}

	/* if we've already sent to everyone we return early */
	if (ntable_stop == num_nodes || !num_pollers)
		return 0;

	li = nodes_by_sel_id(pkt->hdr.selection);
	if (!li) {
		lerr("No matching selection for id %d", pkt->hdr.selection);
		return -1;
	}

	for (; li; li = li->next_item) {
		net_sendto((merlin_node *)li->item, pkt);
	}

	return 0;
}


/*
 * Populates the fd_set's *rd and *wr with all the connected nodes'
 * sockets.
 * Returns the highest socket descriptor found, so the fd_set's can
 * be passed to select(2)
 */
int net_polling_helper(fd_set *rd, fd_set *wr, int sel_val)
{
	uint i;

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		check_node_activity(node);

		/*
		 * safeguard against bugs in net_is_connected() or any of
		 * the system and library calls it makes. node->sock has to
		 * be >= 0 for FD_SET() not to cause segfaults
		 */
		if (node->sock < 0)
			continue;

		/* the node is connected, so we can poll it for readability */
		FD_SET(node->sock, rd);

		/*
		 * if this node's binlog has entries we check for writability
		 * as well, so we can send it from the outer polling loop.
		 */
		if (net_is_connected(node) == STATE_CONNECTED && binlog_has_entries(node->binlog))
			FD_SET(node->sock, wr);

		if (node->sock > sel_val)
			sel_val = node->sock;
	}

	return sel_val;
}


/*
 * Handles polling results from a previous (successful) select(2)
 * This is where new connections are handled and network input is
 * scheduled for reading
 */
int net_handle_polling_results(fd_set *rd, fd_set *wr)
{
	uint i;

	/* loop the nodes and see which ones have sent something */
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		/* skip obviously bogus sockets */
		if (node->sock < 0)
			continue;

		/* handle new connections first */
		if (FD_ISSET(node->sock, wr)) {
			if (net_is_connected(node) == STATE_CONNECTED) {
				if (binlog_has_entries(node->binlog)) {
					node_send_binlog(node, NULL);
				}
			}
			continue;
		}

		/*
		 * handle input, and missing input. All nodes should send
		 * a pulse at least once in a while, so we know it's still OK.
		 * If they fail to do that, we may have to take action.
		 */
		if (FD_ISSET(node->sock, rd)) {
			net_input(node);
		}
	}

	/* check_node_activity(node); */
	return 0;
}

void check_all_node_activity(void)
{
	uint i;

	/* make sure we always check activity level among the nodes */
	for (i = 0; i < num_nodes; i++)
		check_node_activity(node_table[i]);
}
