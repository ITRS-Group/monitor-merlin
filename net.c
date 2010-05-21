#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>
#include "daemon.h"

static int net_sock = -1; /* listening sock descriptor */

int net_sock_desc(void)
{
	return net_sock;
}


/* do a node lookup based on name *or* ip-address + port */
merlin_node *find_node(struct sockaddr_in *sain, const char *name)
{
	uint i;

	if (sain) for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		if (node->sain.sin_addr.s_addr == sain->sin_addr.s_addr)
			return node;
	}
	return NULL;
}


/*
 * Completes a connection to a node we've attempted to connect to
 */
static int net_complete_connection(merlin_node *node)
{
	int error, fail;
	socklen_t optlen = sizeof(int);

	error = getsockopt(node->sock, SOL_SOCKET, SO_ERROR, &fail, &optlen);

	if (!error && !fail) {
		/* successful connection */
		node->status = STATE_CONNECTED;
		linfo("Successfully completed connection to %s node '%s' (%s:%d)",
		      node_type(node), node->name, inet_ntoa(node->sain.sin_addr),
		      ntohs(node->sain.sin_port));
		node->action(node, node->status);
	}

	return !fail;
}


/*
 * Initiate a connection attempt to a node and mark it as PENDING.
 * Note that since we're using sockets in non-blocking mode (in order
 * to be able to effectively multiplex), the connection attempt will
 * never be completed in this function
 */
static int net_try_connect(merlin_node *node)
{
	struct sockaddr *sa = (struct sockaddr *)&node->sain;
	int result;

	/* create the socket if necessary */
	if (node->sock == -1) {
		struct timeval sock_timeout = { 10, 0 };

		node->sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (node->sock < 0)
			return -1;

		result = setsockopt(node->sock, SOL_SOCKET, SO_RCVTIMEO,
							&sock_timeout, sizeof(sock_timeout));
		result |= setsockopt(node->sock, SOL_SOCKET, SO_SNDTIMEO,
							 &sock_timeout, sizeof(sock_timeout));

		if (result) {
			lerr("Failed to set send/receive timeouts: %s", strerror(errno));
			close(node->sock);
			return -1;
		}
	}

	/* don't try to connect to a node if an attempt is already pending */
	if (node->status != STATE_PENDING) {
		sa->sa_family = AF_INET;
		linfo("Connecting to %s:%d", inet_ntoa(node->sain.sin_addr),
			  ntohs(node->sain.sin_port));

		if (connect(node->sock, sa, sizeof(struct sockaddr_in)) < 0) {
			lerr("connect() failed to node '%s' (%s:%d): %s",
				 node->name, inet_ntoa(node->sain.sin_addr),
				 ntohs(node->sain.sin_port), strerror(errno));

			if (errno == EISCONN) { /* already connected? That's fishy.. */
				node_disconnect(node);
				return -1;
			}
			if (errno != EINPROGRESS && errno != EALREADY)
				return -1;
		}
		else
			linfo("Successfully connected to %s:%d",
			      inet_ntoa(node->sain.sin_addr), ntohs(node->sain.sin_port));

		node->status = STATE_PENDING;
	}

	return 0;
}


/*
 * checks if a socket is connected or not by looking up the ip and port
 * of the remote host.
 * Returns 1 if connected and 0 if not.
 */
static int net_is_connected(int sock)
{
	struct sockaddr_in sain;
	socklen_t slen = sizeof(struct sockaddr_in);
	int optval;

	errno = 0;
	if (getpeername(sock, (struct sockaddr *)&sain, &slen) < 0) {
		switch (errno) {
		case EBADF:
		case ENOBUFS:
		case EFAULT:
		case ENOTSOCK:
			lerr("getpeername(): system error %d: %s", errno, strerror(errno));
			(void)close(sock);
			/* fallthrough */
		case ENOTCONN:
			return 0;
		default:
			lerr("Unknown error(%d): %s", errno, strerror(errno));
			break;
		}

		return 0;
	}

	slen = sizeof(optval);
	if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &optval, &slen) < 0) {
		lerr("getsockopt() failed: %s", strerror(errno));
	}

	if (!optval)
		return 1;

	return 0;
}

/* check if a node is connected.
 * Return 1 if yes and 0 if not */
static int node_is_connected(merlin_node *node)
{
	int result;

	if (!node)
		return 0;

	if (node->sock == -1 || node->status == STATE_NONE) {
		result = net_try_connect(node);
		if (result < 0)
			return 0;
	}

	result = net_is_connected(node->sock);
	if (!result && errno == ENOTCONN)
		node->status = STATE_NONE;

	if (result) {
		if (node->status != STATE_CONNECTED)
			node->action(node, STATE_CONNECTED);

		node->status = STATE_CONNECTED;
	}

	return result;
}


/*
 * Negotiate which socket to use for communication when the remote
 * host has accepted a connection attempt from us while we have
 * accepted one from the remote host. This shouldn't happen very
 * often, but if it does we must make sure both ends agree on one
 * socket to use.
 * con is the one that might be in a connection attempt
 * lis is the one we found with accept. */
static int net_negotiate_socket(merlin_node *node, int lis)
{
	fd_set rd, wr;
	int result, con = node->sock, sel = con;
	struct timeval tv = { 0, 50 };

	if (con == -1)
		return lis;

	if (lis == -1)
		return con;

	/* fds are real sockets. check if both are connected */
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(lis, &rd);
	FD_SET(lis, &wr);
	FD_SET(con, &rd);
	FD_SET(con, &wr);

	if (lis > con)
		sel = lis;

	result = select(sel + 1, &rd, &wr, NULL, &tv);
	if (result < 0) {
		close(lis);
		close(con);
		node->status = STATE_NONE;
		node->action(node, node->status);
		return -1;
	}

	if (!result || (!FD_ISSET(con, &rd) && !FD_ISSET(lis, &rd))) {
		/* nothing to read on any socket, so pick one arbitrarily */
		close(lis);
		return con;
	}

	if (FD_ISSET(lis, &rd) && FD_ISSET(con, &rd)) {
		lerr("negotiation: listening and connecting sockets are viable for reading, but status of node isn't \"connected\"");
		close(lis);
		return con;
	}

	/* we may have to complete the connection first */
	if (FD_ISSET(con, &wr)) {
		net_complete_connection(node);
	}
	else {
		if (FD_ISSET(con, &rd) && !FD_ISSET(lis, &rd)) {
			close(lis);
			return con;
		}
		if (FD_ISSET(lis, &rd) && !FD_ISSET(con, &rd)) {
			close(con);
			return lis;
		}
	}

	if (net_is_connected(con) && net_is_connected(lis)) {
		node_disconnect(node);
		node->status = STATE_CONNECTED;
		node->sock = lis;
		node->action(node, node->status);
		return lis;
	}

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

	sock = accept(net_sock, (struct sockaddr *)&sain, &slen);
	if (sock < 0) {
		lerr("accept() failed: %s", strerror(errno));
		return -1;
	}

	node = find_node(&sain, NULL);
	if (!node) {
		linfo("'%s' is not a registered node", inet_ntoa(sain.sin_addr));
		close(sock);
		return -1;
	}

	linfo("%s node '%s' connected from %s : %d",
		  node_type(node), node->name,
		  inet_ntoa(sain.sin_addr), ntohs(sain.sin_port));

	switch (node->status) {
	case STATE_PENDING:
		node->sock = net_negotiate_socket(node, sock);
		break;

	case STATE_NEGOTIATING: /* this should *NEVER EVER* happen */
		lerr("Aieee! Negotiating connection with one attempting inbound. Bad Thing(tm)");
		/* fallthrough */
	case STATE_CONNECTED:
		close(sock);
		return 0;

	case STATE_NONE:
		/* we must close it unconditionally or we'll leak fd's
		 * for reconnecting nodes that were previously connected */
		node_disconnect(node);
		node->sock = sock;
		break;

	default:
		lerr("%s %s has an unknown status", node_type(node), node->name);
		break;
	}

	node->status = STATE_CONNECTED;
	node->action(node, node->status);
	node->last_sent = node->last_recv = time(NULL);

	return sock;
}


/* close all sockets and release the memory used by
 * static global vars for networking purposes */
int net_deinit(void)
{
	uint i;

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		node_disconnect(node);
		free(node);
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

	sain.sin_addr.s_addr = 0;
	sain.sin_port = htons(default_port);
	sain.sin_family = AF_INET;

	net_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (net_sock < 0)
		return -1;

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

	node_is_connected(node);

	return node_send_event(node, pkt, 100);
}


/*
 * If a node hasn't been heard from in pulse_interval x 2 seconds,
 * we mark it as no longer connected and send a CTRL_INACTIVE event
 * to the module, signalling that our Nagios should, potentially,
 * take over checks for the awol poller
 */
static void check_node_activity(merlin_node *node)
{
	time_t now = time(NULL);

	/* we only bother about pollers, so return early if we have none */
	if (!num_pollers)
		return;

	if (node->sock == -1 || node->status != STATE_CONNECTED)
		return;

	if (node->last_recv && node->last_recv < now - (pulse_interval * 2))
		node->action(node, STATE_NONE);
}


/*
 * Passes an event from a remote node to the broker module,
 * any and all nocs and the database handling routines
 */
static int handle_network_event(merlin_node *node, merlin_event *pkt)
{
	if (node->type == MODE_POLLER && num_nocs) {
		uint i;

		linfo("Passing on event from poller %s to %d nocs",
			  node->name, num_nocs);

		for (i = 0; i < num_nocs; i++) {
			merlin_node *noc = noc_table[i];
			net_sendto(noc, pkt);
		}
	}

	/* not all packets get delivered to the merlin module */
	switch (pkt->hdr.type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		mrm_db_update(pkt);
		return 0;

	default:
		/*
		 * IMPORTANT NOTE:
		 * It's absolutely vital that we send the event to the
		 * ipc socket *before* we ship it off to the db_update
		 * function, since the db_updpate function deblockify()'s
		 * the event, which makes unusable for sending to the
		 * ipc (or, indeed, anywhere else) afterwards.
		 */
		ipc_send_event(pkt);
		mrm_db_update(pkt);
		return 0;
	}
	return 0;
}


/*
 * Reads input from a particular node and ships it off to
 * the "handle_network_event()" routine up above
 */
static void net_input(merlin_node *node)
{
	merlin_event pkt;
	int len;

	errno = 0;
	len = node_read_event(node, &pkt, 0);
	/* errors are handled in node_read_event() */
	if (len <= 0)
		return;

	/* We read something the size of an mrm packet header */

	if (pkt.hdr.type == CTRL_PACKET && pkt.hdr.code == CTRL_PULSE) {
		/* noop. we've already updated the last_recv time */
		return;
	}

	handle_network_event(node, &pkt);
	return;
}


/*
 * Sends an event read from the ipc socket to the appropriate nodes
 */
int net_send_ipc_data(merlin_event *pkt)
{
	uint i;

	if (!num_nodes)
		return 0;

	if (num_pollers && pkt->hdr.selection != 0xffff) {
		linked_item *li = nodes_by_sel_id(pkt->hdr.selection);

		if (!li) {
			lerr("No matching selection for id %d", pkt->hdr.selection);
			return -1;
		}
		for (; li; li = li->next_item)
			net_sendto((merlin_node *)li->item, pkt);
	}

	for (i = 0; i < num_nocs + num_peers; i++) {
		net_sendto(node_table[i], pkt);
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

		if (!node_is_connected(node) && node->status != STATE_PENDING)
			continue;

		if (node->status == STATE_PENDING)
			FD_SET(node->sock, wr);
		else
			FD_SET(node->sock, rd);

		if (node->sock > sel_val)
			sel_val = node->sock;
	}

	return max(sel_val, net_sock);
}


/*
 * Handles polling results from a previous (successful) select(2)
 * This is where new connections are handled and network input is
 * scheduled for reading
 */
int net_handle_polling_results(fd_set *rd, fd_set *wr)
{
	int sockets = 0;
	uint i;

	/* loop the nodes and see which ones have sent something */
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		/* skip obviously bogus sockets */
		if (node->sock < 0)
			continue;

		/* new connections go first */
		if (node->status == STATE_PENDING && FD_ISSET(node->sock, wr)) {
			sockets++;
			if (net_complete_connection(node)) {
				node_disconnect(node);
			}
			continue;
		}

		/* handle input, and missing input. All nodes should send
		 * a pulse at least once in a while, so we know it's still OK.
		 * If they fail to do that, we may have to take action. */
		if (FD_ISSET(node->sock, rd)) {
			int result;

			sockets++;
			do {
				/* read all available events */
				net_input(node);
				result = io_read_ok(node->sock, 50);
			} while (result > 0);

			continue;
		}

		check_node_activity(node);
	}

	return sockets;
}

void check_all_node_activity(void)
{
	uint i;

	/* make sure we always check activity level among the nodes */
	for (i = 0; i < num_nodes; i++)
		check_node_activity(node_table[i]);
}

/* poll for INBOUND socket events and completion of pending connections */
int net_poll(void)
{
	fd_set rd, wr;
	struct timeval to = { 1, 0 }, start, end;
	uint i;
	int sel_val = 0, nfound = 0;
	int socks = 0;

	printf("net_sock = %d\n", net_sock);
	sel_val = net_sock;

	/* add the rest of the sockets to the fd_set */
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(net_sock, &rd);
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		if (!node_is_connected(node) && node->status != STATE_PENDING)
			continue;

		if (node->status == STATE_PENDING)
			FD_SET(node->sock, &wr);
		else
			FD_SET(node->sock, &rd);

		if (node->sock > sel_val)
			sel_val = node->sock;
	}

	/* wait for input on all connected sockets */
	gettimeofday(&start, NULL);
	nfound = select(sel_val + 1, &rd, &wr, NULL, &to);
	gettimeofday(&end, NULL);

	if (!nfound) {
		return 0;
	}

	/* check for inbound connections first */
	if (FD_ISSET(net_sock, &rd)) {
		net_accept_one();
		socks++;
	}

	assert(nfound == socks);

	return 0;
}
