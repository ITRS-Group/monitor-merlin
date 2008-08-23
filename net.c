/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright(C) 2005 OP5 AB
 * All rights reserved.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/select.h>
#include <fcntl.h>

extern int h_errno;

#include "redundancy.h"
#include "types.h"
#include "shared.h"
#include "logging.h"
#include "protocol.h"
#include "ipc.h"
#include "io.h"

#define DEF_SOCK_TIMEOUT 50000

extern int def_port;
static int net_sock = -1; /* listening sock descriptor */

#define num_nodes (nocs + pollers + peers)
#define node_table noc_table
static unsigned nocs, pollers, peers;
static struct node *base, **noc_table, **poller_table, **peer_table;
//static struct timeval sock_to = { 0, 0 };
static struct node **selection_table;

static const char *node_state(struct node *node)
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

static const char *node_type(struct node *node)
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


static struct node *add_node_to_list(struct node *node, struct node *list)
{
	node->next = list;
	return node;
}


static struct node *nodelist_by_selection(int sel)
{
	if (sel < 0 || sel > get_num_selections())
		return NULL;

	return selection_table[sel];
}


void create_node_tree(struct node *table, unsigned n)
{
	int i, xnoc, xpeer, xpoll;

	selection_table = calloc(get_num_selections() + 1, sizeof(struct node *));

	for (i = 0; i < n; i++) {
		struct node *node = &table[i];
		int id = get_sel_id(node->hostgroup);
		switch (node->type) {
		case MODE_NOC:
			nocs++;
			break;
		case MODE_POLLER:
			pollers++;
			selection_table[id] = add_node_to_list(node, selection_table[id]);
			break;
		case MODE_PEER:
			peers++;
			break;
		}
	}

	/* this way, we can keep them all linear while each has its own
	 * table and still not waste much memory. pretty nifty, really */
	node_table = calloc(num_nodes, sizeof(struct node *));
	noc_table = node_table;
	peer_table = &node_table[nocs];
	poller_table = &node_table[nocs + peers];

	xnoc = xpeer = xpoll = 0;
	for (i = 0; i < n; i++) {
		struct node *node = &table[i];

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

	for (i = 0; i < n; i++) {
		struct node *node = node_table[i];

		ldebug("node: %s; type: %d", node->name, node->type);
	}

	base = *node_table;
}


int net_resolve(const char *cp, struct in_addr *inp)
{
	return inet_aton(cp, inp);
}


/* do a node lookup based on name *or* ip-address + port */
struct node *find_node(struct sockaddr_in *sain, const char *name)
{
	int i;

	if (sain) for (i = 0; i < num_nodes; i++) {
		struct node *node = node_table[i];
		if (node->sain.sin_addr.s_addr == sain->sin_addr.s_addr)
			return node;
	}
	return NULL;
}


static int net_complete_connection(struct node *node)
{
	int error, fail;
	socklen_t optlen = sizeof(int);

	ldebug("%s %s is %s, with input ready. Trying getsockopt()",
		   node_type(node), node->name, node_state(node));
	error = getsockopt(node->sock, SOL_SOCKET, SO_ERROR, &fail, &optlen);
//	ldebug("getsockopt() returned %d ('%s'). opt is %d",
//		   result, strerror(errno), opt);

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
static void net_disconnect(struct node *node)
{
	close(node->sock);
	node->status = STATE_NONE;
	node->sock = -1;
	node->zread = 0;
}


static int net_try_connect(struct node *node)
{
	struct sockaddr *sa = (struct sockaddr *)&node->sain;
	int result;

	ldebug("net_try_connect() for node '%s'. node is %s",
	       node->name, node_state(node));

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

	if (node->status != STATE_PENDING) {
		sa->sa_family = AF_INET;
		ldebug("Connecting to %s:%d", inet_ntoa(node->sain.sin_addr), ntohs(node->sain.sin_port));

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

		node->status = STATE_PENDING;
	}

	return 0;
}


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
	else
		ldebug("getsockopt() set optval to %d: %s", optval, strerror(optval));

	if (!optval)
		return 1;

	return 0;
}


#ifdef NET_DEBUG
static void print_node_status(struct node *node)
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
static int node_is_connected(struct node *node)
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

	ldebug("net_is_connected() for '%s' (%s:%d) returned %d (%s:%d): %s",
		   node->name, inet_ntoa(node->sain.sin_addr), ntohs(node->sain.sin_port),
		   result, inet_ntoa(sain.sin_addr), ntohs(sain.sin_port),
		   strerror(errno));

	if (result)
		node->status = STATE_CONNECTED;

	return result;
}


/* con is the one that might be in a connection attempt
 * lis is the one we found with accept. */
static int net_negotiate_socket(struct node *node, int lis)
{
	fd_set rd, wr;
	int result, con = node->sock, sel = con;
	struct timeval tv = { 0, 50 };
	struct sockaddr_in csain, lsain;

	if (con == -1)
		return lis;

	if (con == -1)
		return lis;

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
		ldebug("select in negotiate_socket() returned %d: %s",
			   result, strerror(errno));
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


static int net_accept_one(void)
{
	int sock;
	struct node *node;
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
		struct node *node = node_table[i];
		net_disconnect(node);
		free(node);
	}

	free(node_table);

	return 0;
}


int net_init(void)
{
	int result, sockopt = 1;
	struct sockaddr_in sain, inbound;
	struct sockaddr *sa = (struct sockaddr *)&sain;
	socklen_t addrlen = sizeof(inbound);

	sain.sin_addr.s_addr = 0;
	sain.sin_port = ntohs(def_port);
	sa->sa_family = AF_INET;

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

	int i;
	for (i = 0; i < num_nodes; i++) {
		struct node *node = node_table[i];

		ldebug("node->name: %s; node->sock: %d; node->type: %d; node->hostgroup: %s",
			   node->name, node->sock, node->type, node->hostgroup);
	}

	return 0;
}


/* send a specific packet to a specific host */
static int net_sendto(struct node *node, const void *data, size_t len)
{
	int result;

	linfo("sending %d bytes to %s node '%s' (%s:%d). sock is %d",
		  len, node_type(node), node->name, inet_ntoa(node->sain.sin_addr),
		  ntohs(node->sain.sin_port), node->sock);

	if (!data || !len)
		return 0;

	if (!node_is_connected(node)) {
		linfo("node '%s' is not connected, so not sending", node->name);
		return -1;
	}

	node->last_sent = time(NULL);
	result = io_send_all(node->sock, data, len);

	return result;
}


#define set_inactive(node) ipc_send_ctrl(CTRL_INACTIVE, node->selection)
#define set_active(node) ipc_send_ctrl(CTRL_ACTIVE, node->selection)

#define pulse_interval 15
static void check_node_activity(struct node *node)
{
	time_t now = time(NULL);

	/* pollers don't care about nocs (XXX: What about peers?) */
	if (!is_noc)
		return;

	if (node->sock == -1 || node->status != STATE_CONNECTED)
		return;

	if (node->last_recv && node->last_recv < now - (pulse_interval * 2))
		set_inactive(node);
}


static void net_input(struct node *node)
{
	struct proto_hdr hdr;
	int len;

	linfo("Data available from %s '%s' (%s)", node_type(node),
		  node->name, inet_ntoa(node->sain.sin_addr));

	errno = 0;
	len = io_recv_all(node->sock, &hdr, sizeof(hdr));
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

	if (len != sizeof(hdr)) {
		lwarn("Malformatted mrm header from %s (%d bytes, expected %d): %s",
			  inet_ntoa(node->sain.sin_addr), len, sizeof(hdr), strerror(errno));
		return;
	}

	/* We read something the size of an mrm packet header */
	ldebug("Read %d bytes from %s", len, inet_ntoa(node->sain.sin_addr));
	ldebug("message; protocol: %u, type: %u, len: %d",
		   hdr.protocol, hdr.type, hdr.len);

	if (!node->last_recv)
		set_active(node);

	node->last_recv = time(NULL);

	if (hdr.type == CTRL_PACKET) {
		switch (hdr.len) {
		case CTRL_PULSE:
			/* noop. we've already updated the last_recv time */
			break;
		default:
			ipc_write(&hdr, sizeof(hdr), 0);
			break;
		}
	}
	else if (!hdr.len) {
		lwarn("%s '%s' claims empty body, but packet type isn't CONTROL",
		      node_type(node), node->name);
	}
	else {
		int buflen, result;
		void *buf;

		buflen = sizeof(hdr) + hdr.len;
		buf = malloc(buflen);
		memcpy(buf, &hdr, sizeof(hdr));

		len = io_recv_all(node->sock, buf + sizeof(hdr), hdr.len);
		result = ipc_write(buf, buflen, 0);
		if (result != buflen) {
			lerr("ipc_write() returned %d, expected %d: %s",
			      result, buflen, strerror(errno));
		}

		free(buf);
	}
}


int send_ipc_data(const struct proto_hdr *hdr)
{
	int result, len, i;
	void *buf;

	len = hdr->len + sizeof(*hdr);

	buf = malloc(len);
	if (!buf)
		return -1;

	memcpy(buf, hdr, sizeof(*hdr));

	result = ipc_read(buf + sizeof(*hdr), hdr->len, 0);
	if (result != hdr->len) {
		lerr("Protocol heder claims body size is %d, but ipc_read returned %d: %s",
			 hdr->len, result, strerror(errno));
		return -1;
	}

	if (is_noc && hdr->selection != 0xffff) {
		struct node *node = nodelist_by_selection(hdr->selection);

		ldebug("Sending to nodes by selection '%s'", get_sel_name(hdr->selection));
		for (; node; node = node->next)
			net_sendto(node, buf, len);
	}
	else {
		ldebug("Sending to all %d nocs and peers", nocs + peers);

		for (i = 0; i < nocs + peers; i++) {
			struct node *node = node_table[i];

			net_sendto(node, buf, len);
		}
	}

	free(buf);

	return 0;
}


#define READ_OK 1
#define WRITE_OK 2
/* poll for INBOUND socket events and completion of pending connections */
int net_poll(void)
{
	fd_set rd, wr;
	struct timeval to = { 1, 0 }, start, end;
	int i, sel_val = 0, nfound = 0;
	int socks = 0;

	sel_val = net_sock;

	/* add the rest of the sockets to the fd_set */
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(net_sock, &rd);
	for (i = 0; i < num_nodes; i++) {
		struct node *node = node_table[i];

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
	ldebug("select(%d, ...) returned %d in %lu.%lu seconds",
		   sel_val + 1, nfound,
		   end.tv_sec - start.tv_sec, end.tv_usec - start.tv_usec);
	print_node_status_list();

	if (!nfound) {
		/* make sure we always check activity level among the nodes */
		for (i = 0; i < num_nodes; i++)
			check_node_activity(node_table[i]);

		return 0;
	}

	/* check for inbound connections first */
	if (FD_ISSET(net_sock, &rd)) {
		ldebug("network socket is ready for reading (inbound connection)");
		net_accept_one();
		socks++;
	}

	/* loop the nodes and see which ones have sent something */
	for (i = 0; i < num_nodes; i++) {
		struct node *node = node_table[i];

		/* skip obviously bogus sockets */
		if (node->sock < 0)
			continue;

		/* new connections go first */
		if (node->status == STATE_PENDING && FD_ISSET(node->sock, &wr)) {
			socks++;
			if (net_complete_connection(node)) {
				ldebug("Failed to complete connection to node '%s'", node->name);
				net_disconnect(node);
			}
			continue;
		}
		/* handle input, and missing input. All nodes should send
		 * a pulse at least once in a while, so we know it's still OK.
		 * If they fail to do that, we may have to take action. */
		if (FD_ISSET(node->sock, &rd)) {
			socks++;
			net_input(node);
			continue;
		}

		check_node_activity(node);
	}

	if (nfound != socks)
		ldebug("nfound: %d; socks: %d; MISMATCH!", nfound, socks);

	return 0;
}
