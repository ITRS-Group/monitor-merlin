#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>
#include "daemon.h"

#define DEF_SOCK_TIMEOUT 50000

static int net_sock = -1; /* listening sock descriptor */

#define num_nodes (num_nocs + num_pollers + num_peers)
#define node_table noc_table
static unsigned num_nocs, num_pollers, num_peers;
static merlin_node *base, **noc_table, **poller_table, **peer_table;
static merlin_node **selected_nodes;

int net_sock_desc(void)
{
	return net_sock;
}

static inline const char *node_state(merlin_node *node)
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

static const char *node_type(merlin_node *node)
{
	switch (node->type) {
	case MODE_NOC:
		return "NOC";
	case MODE_PEER:
		return "peer";
	case MODE_POLLER:
		return "poller";
	}

	return "Unknown node-type";
}


static merlin_node *add_node_to_list(merlin_node *node, merlin_node *list)
{
	node->next = list;
	return node;
}


static merlin_node *nodelist_by_selection(int sel)
{
	if (sel < 0 || sel > get_num_selections())
		return NULL;

	return selected_nodes[sel];
}


/*
 * creates the node-table, with fanout indices at the various
 * different types of nodes. This allows us to iterate over
 * all the nodes or a particular subset of them using the same
 * table, which is quite handy.
 */
void create_node_tree(merlin_node *table, unsigned n)
{
	int i, xnoc, xpeer, xpoll;

	selected_nodes = calloc(get_num_selections() + 1, sizeof(merlin_node *));

	for (i = 0; i < n; i++) {
		merlin_node *node = &table[i];
		int id = get_sel_id(node->hostgroup);
		switch (node->type) {
		case MODE_NOC:
			num_nocs++;
			break;
		case MODE_POLLER:
			num_pollers++;
			selected_nodes[id] = add_node_to_list(node, selected_nodes[id]);
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

	base = *node_table;
}


/*
 * FIXME: should also handle hostnames
 */
int net_resolve(const char *cp, struct in_addr *inp)
{
	return inet_aton(cp, inp);
}


/* do a node lookup based on name *or* ip-address + port */
merlin_node *find_node(struct sockaddr_in *sain, const char *name)
{
	int i;

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
	}

	return !fail;
}


/* close down the connection to a node and mark it as down */
static void net_disconnect(merlin_node *node)
{
	close(node->sock);
	node->status = STATE_NONE;
	node->sock = -1;
	node->zread = 0;
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
				net_disconnect(node);
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
static int net_is_connected(int sock, struct sockaddr_in *sain)
{
	socklen_t slen = sizeof(struct sockaddr_in);
	int optval;

	errno = 0;
	if (getpeername(sock, (struct sockaddr *)sain, &slen) < 0) {
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


#ifdef NET_DEBUG
static void print_node_status(merlin_node *node)
{
	printf("%s node '%s' (%s : %d) is %s. socket is %d",
		   node_type(node), node->name, inet_ntoa(node->sain.sin_addr),
		   ntohs(node->sain.sin_port), node_state(node), node->sock);
}
#endif


static void print_node_status_list(void)
{
#ifdef NET_DEBUG
	int i;
	putchar('\n');
	putchar('\n');
	for (i = 0; i < num_nodes; i++)
		print_node_status(node_table[i]);
	putchar('\n');
	putchar('\n');
#endif
}


/* check if a node is connected.
 * Return 1 if yes and 0 if not */
static int node_is_connected(merlin_node *node)
{
	struct sockaddr_in sain;
	int result;

	if (!node)
		return 0;

	if (node->sock == -1 || node->status == STATE_NONE) {
		result = net_try_connect(node);
		if (result < 0)
			return 0;
	}

	result = net_is_connected(node->sock, &sain);
	if (!result && errno == ENOTCONN)
		node->status = STATE_NONE;

	if (result)
		node->status = STATE_CONNECTED;

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
	struct sockaddr_in csain, lsain;

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

	if (net_is_connected(con, &csain) && net_is_connected(lis, &lsain)) {
		net_disconnect(node);
		node->status = STATE_CONNECTED;
		node->sock = lis;
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
		net_disconnect(node);
		node->sock = sock;
		break;

	default:
		lerr("%s %s has an unknown status", node_type(node), node->name);
		break;
	}

	node->status = STATE_CONNECTED;
	node->last_sent = node->last_recv = time(NULL);

	return sock;
}


/* close all sockets and release the memory used by
 * static global vars for networking purposes */
int net_deinit(void)
{
	int i;

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		net_disconnect(node);
		free(node);
	}

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

	sain.sin_addr.s_addr = 0;
	sain.sin_port = ntohs(default_port);
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
	int result;

	if (!pkt || !node) {
		lerr("net_sendto() called with neither node nor pkt");
		return -1;
	}

	ldebug("sending %zu bytes to %s node '%s' (%s:%d). sock is %d",
	       packet_size(pkt), node_type(node), node->name,
	       inet_ntoa(node->sain.sin_addr),
	       ntohs(node->sain.sin_port), node->sock);

	if (!node_is_connected(node)) {
		linfo("node '%s' is not connected, so not sending", node->name);
		return -1;
	}

	node->last_sent = time(NULL);
	result = io_send_all(node->sock, pkt, packet_size(pkt));

	return result;
}


/*
 * If a node hasn't been heard from in pulse_interval x 2 seconds,
 * we mark it as no longer connected and send a CTRL_INACTIVE event
 * to the module, signalling that our Nagios should, potentially,
 * take over checks for the awol poller
 */
#define set_inactive(node) ipc_send_ctrl(CTRL_INACTIVE, node->selection)
#define set_active(node) ipc_send_ctrl(CTRL_ACTIVE, node->selection)
static void check_node_activity(merlin_node *node)
{
	time_t now = time(NULL);

	/* we only bother about pollers, so return early if we have none */
	if (!num_pollers)
		return;

	if (node->sock == -1 || node->status != STATE_CONNECTED)
		return;

	if (node->last_recv && node->last_recv < now - (pulse_interval * 2))
		set_inactive(node);
}


/*
 * Passes an event from a remote node to the broker module,
 * any and all nocs and the database handling routines
 */
static int handle_network_event(merlin_node *node, merlin_event *pkt)
{
	if (node->type == MODE_POLLER && num_nocs) {
		int i;

		linfo("Passing on event from poller %s to %d nocs",
			  node->name, num_nocs);

		for (i = 0; i < num_nocs; i++) {
			merlin_node *noc = noc_table[i];
			net_sendto(noc, pkt);
		}
	}

	ipc_send_event(pkt);
	mrm_db_update(pkt);
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

	linfo("Data available from %s '%s' (%s)", node_type(node),
		  node->name, inet_ntoa(node->sain.sin_addr));

	errno = 0;
	len = proto_read_event(node->sock, &pkt);
	if (len < 0) {
		lerr("read() from %s node %s failed: %s",
			 node_type(node), node->name, strerror(errno));
		switch (errno) {
		case EAGAIN:
		case EINTR:
			return;

		case ENOTCONN: /* Not connected */
		case EBADF: /* Bad file-descriptor */
		case ECONNRESET: /* Connection reset by peer */
		default:
			net_disconnect(node);
			break;
		}
		return;
	}

	if (!len) {
		node->zread++;
		if (node->zread > 5)
			net_disconnect(node);
		return;
	}

	/* We read something the size of an mrm packet header */
	ldebug("Read %d bytes from %s. protocol: %u, type: %u, len: %d",
		   len, inet_ntoa(node->sain.sin_addr),
		   pkt.hdr.protocol, pkt.hdr.type, pkt.hdr.len);

	if (!node->last_recv)
		set_active(node);

	node->last_recv = time(NULL);

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
	int i;

	if (!num_nodes)
		return 0;

	if (num_pollers && pkt->hdr.selection != 0xffff) {
		merlin_node *node = nodelist_by_selection(pkt->hdr.selection);

		if (!node) {
			lerr("No matching selection for id %d", pkt->hdr.selection);
			return -1;
		}
		for (; node; node = node->next)
			net_sendto(node, pkt);
	}

	if (num_nocs + num_peers) {
		ldebug("Sending to %u nocs and %u peers", num_nocs, num_peers);

		for (i = 0; i < num_nocs + num_peers; i++) {
			merlin_node *node = node_table[i];

			net_sendto(node, pkt);
		}
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
	int i;

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

	return sel_val;
}


/*
 * Handles polling results from a previous (successful) select(2)
 * This is where new connections are handled and network input is
 * scheduled for reading
 */
#define READ_OK 1
#define WRITE_OK 2
int net_handle_polling_results(fd_set *rd, fd_set *wr)
{
	int sockets = 0;
	int i;

	/* loop the nodes and see which ones have sent something */
	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		/* skip obviously bogus sockets */
		if (node->sock < 0)
			continue;

		/* new connections go first */
		if (node->status == STATE_PENDING && FD_ISSET(node->sock, wr)) {
			printf("node socket %d is ready for writing\n", node->sock);
			sockets++;
			if (net_complete_connection(node)) {
				net_disconnect(node);
			}
			continue;
		}

		/* handle input, and missing input. All nodes should send
		 * a pulse at least once in a while, so we know it's still OK.
		 * If they fail to do that, we may have to take action. */
		if (FD_ISSET(node->sock, rd)) {
			int result;

			printf("node socket %d is ready for reading\n", node->sock);
			sockets++;
			do {
				/* read all available events */
				net_input(node);
				result = io_poll_read(node->sock, 50);
			} while (result > 0);

			continue;
		}

		check_node_activity(node);
	}

	return sockets;
}

void check_all_node_activity(void)
{
	int i;

	/* make sure we always check activity level among the nodes */
	for (i = 0; i < num_nodes; i++)
		check_node_activity(node_table[i]);
}

/* poll for INBOUND socket events and completion of pending connections */
int net_poll(void)
{
	fd_set rd, wr;
	struct timeval to = { 1, 0 }, start, end;
	int i, sel_val = 0, nfound = 0;
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
	print_node_status_list();

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
