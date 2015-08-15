#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "daemonize.h"
#include "daemon.h"
#include "db_updater.h"
#include "config.h"
#include "logging.h"
#include "ipc.h"
#include "configuration.h"
#include "net.h"
#include "sql.h"
#include "state.h"
#include "shared.h"

static const char *progname;
static const char *pidfile, *merlin_user;
unsigned short default_port = 15551;
unsigned int default_addr = 0;
static merlin_confsync csync;
static int killing;
static int user_sig;
static merlin_nodeinfo merlind;
static int merlind_sig;

static void usage(char *fmt, ...)
	__attribute__((format(printf,1,2)));

static void usage(char *fmt, ...)
{
	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
	}

	printf("Usage: %s -c <config-file> [-d] [-k] [-s] [-h]\n"
		"\t-c|--config   Specify the configuration file name. Unknown, non-flag\n"
		"\t              arguments might also be interprented as the config file.\n"
		"\t-d|--debug    Enter \"debug\" mode - this just means it won't daemonize.\n"
		"\t-s            Don't start. Instead, print if merlin is already running.\n"
		"\t-k|--kill     Don't start. Instead, find a running instance and kill it.\n"
		"\t-h|--help     Print this help text.\n"
		, progname);

	exit(1);
}

/* node connect/disconnect handlers */
static int node_action_handler(merlin_node *node, int prev_state)
{
	switch (node->state) {
	case STATE_PENDING:
	case STATE_NEGOTIATING:
	case STATE_NONE:
		node_disconnect(node, "%s disconnected", node->name);

		/* only send INACTIVE if we haven't already */
		if (prev_state == STATE_CONNECTED) {
			ldebug("Sending IPC control INACTIVE for '%s'", node->name);
			return ipc_send_ctrl(CTRL_INACTIVE, node->id);
		}
	}

	return 1;
}

static int ipc_action_handler(merlin_node *node, int prev_state)
{
	uint i;

	switch (node->state) {
	case STATE_CONNECTED:
		break;

	case STATE_PENDING:
	case STATE_NEGOTIATING:
	case STATE_NONE:
		/* if ipc wasn't connected before, we return early */
		if (prev_state != STATE_CONNECTED)
			return 0;

		/* also tell our peers and masters */
		for (i = 0; i < num_masters + num_peers; i++) {
			merlin_node *n = node_table[i];
			node_send_ctrl_inactive(n, CTRL_GENERIC);
		}
	}

	return 0;
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "port")) {
			char *endp;

			default_port = (unsigned short)strtoul(v->value, &endp, 0);
			if (default_port < 1 || *endp)
				cfg_error(comp, v, "Illegal value for port: %s", v->value);
			continue;
		}
		if (!strcmp(v->key, "address")) {
			unsigned int addr;
			if (inet_pton(AF_INET, v->value, &addr) == 1)
				default_addr = addr;
			else
				cfg_error(comp, v, "Illegal value for address: %s", v->value);
			continue;
		}
		if (!strcmp(v->key, "pidfile")) {
			pidfile = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "merlin_user")) {
			merlin_user = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "import_program")) {
			import_program = strdup(v->value);
			continue;
		}

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->key, v->value))
			continue;

		cfg_error(comp, v, "Unknown variable");
	}

	for (i = 0; i < comp->nested; i++) {
		struct cfg_comp *c = comp->nest[i];

		if (!prefixcmp(c->name, "database")) {
			grok_db_compound(c);
			continue;
		}
		if (!strcmp(c->name, "object_config")) {
			grok_confsync_compound(c, &csync);
			continue;
		}
	}
}

/* daemon-specific node manipulation */
static void post_process_nodes(void)
{
	uint i, x;

	ldebug("post processing %d masters, %d pollers, %d peers",
	       num_masters, num_pollers, num_peers);

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		if (!node) {
			lerr("node is null. i is %d. num_nodes is %d. wtf?", i, num_nodes);
			continue;
		}

		if (!node->csync.configured && csync.push.cmd) {
			if (asprintf(&node->csync.push.cmd, "%s %s", csync.push.cmd, node->name) < 0)
				lerr("CSYNC: Failed to add per-node confsync command for %s", node->name);
			else
				ldebug("CSYNC: Adding per-node sync to %s as: %s\n", node->name, node->csync.push.cmd);
		}

		if (!node->sain.sin_port)
			node->sain.sin_port = htons(default_port);

		node->action = node_action_handler;

		node->bq = nm_bufferqueue_create();
		if (node->bq == NULL) {
			lerr("Failed to create io cache for node %s. Aborting", node->name);
		}

		/*
		 * this lets us support multiple merlin instances on
		 * a single system, but all instances on the same
		 * system will be marked at the same time, so we skip
		 * them on the second pass here.
		 */
		if (node->flags & MERLIN_NODE_FIXED_SRCPORT) {
			continue;
		}

		if (node->sain.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
			node->flags |= MERLIN_NODE_FIXED_SRCPORT;
			ldebug("Using fixed source-port for local %s node %s",
				   node_type(node), node->name);
			continue;
		}
		for (x = i + 1; x < num_nodes; x++) {
			merlin_node *nx = node_table[x];
			if (node->sain.sin_addr.s_addr == nx->sain.sin_addr.s_addr) {
				ldebug("Using fixed source-port for %s node %s",
				       node_type(node), node->name);
				ldebug("Using fixed source-port for %s node %s",
				       node_type(nx), nx->name);
				node->flags |= MERLIN_NODE_FIXED_SRCPORT;
				nx->flags |= MERLIN_NODE_FIXED_SRCPORT;

				if (node->sain.sin_port == nx->sain.sin_port) {
					lwarn("Nodes %s and %s have same ip *and* same port. Voodoo?",
					      node->name, nx->name);
				}
			}
		}
	}
}

static int grok_config(char *path)
{
	uint i;
	struct cfg_comp *config;

	if (!path)
		return 0;

	config = cfg_parse_file(path);
	if (!config)
		return 0;

	for (i = 0; i < config->vars; i++) {
		struct cfg_var *v = config->vlist[i];

		if (!v->value)
			cfg_error(config, v, "No value for option '%s'", v->key);

		if (grok_common_var(config, v))
			continue;

		if (!strcmp(v->key, "port")) {
			default_port = (unsigned short)strtoul(v->value, NULL, 0);
			continue;
		}

		cfg_warn(config, v, "Unrecognized variable\n");
	}

	for (i = 0; i < config->nested; i++) {
		struct cfg_comp *c = config->nest[i];

		if (!prefixcmp(c->name, "daemon")) {
			grok_daemon_compound(c);
			continue;
		}
	}

	/*
	 * if we're supposed to kill a running daemon, ignore
	 * parsing and post-processing nodes. We avoid memory
	 * fragmentation by releasing the config memory before
	 * allocating memory for the nodes.
	 */
	if (!killing) {
		node_grok_config(config);
	}
	cfg_destroy_compound(config);
	if (!killing) {
		post_process_nodes();
	}

	return 1;
}


static int handle_ipc_event(merlin_event *pkt)
{
	int result = 0;

	if (pkt->hdr.type == CTRL_PACKET) {
		switch (pkt->hdr.code) {
		case CTRL_PATHS:
			return 0;

		case CTRL_ACTIVE:
			result = node_compat_cmp(&ipc, pkt);
			if (result) {
				lerr("ipc is incompatible with us. Recent update?");
				node_disconnect(&ipc, "Incompatible node");
				return 0;
			}
			node_set_state(&ipc, STATE_CONNECTED, "Connected");
			memcpy(&ipc.info, pkt->body, sizeof(ipc.info));
			break;

		case CTRL_INACTIVE:
			/* this should really never happen, but forward it if it does */
			memset(&ipc.info, 0, sizeof(ipc.info));
			break;
		default:
			lwarn("forwarding control packet %d to the network",
				  pkt->hdr.code);
			break;
		}
	}

	/*
	 * we must send to the network before we run mrm_db_update(),
	 * since the latter deblockifies the packet and makes it
	 * unusable in network transfers without repacking, but only
	 * if this isn't magically marked as a NONET event
	 */
	if (pkt->hdr.code != MAGIC_NONET)
		result = net_send_ipc_data(pkt);

	/* skip sending control packets to database */
	if (use_database && pkt->hdr.type != CTRL_PACKET)
		result |= mrm_db_update(&ipc, pkt);

	return result;
}

static int ipc_reap_events(void)
{
	int len, events = 0;
	merlin_event *pkt;

	node_log_event_count(&ipc, 0);

	len = node_recv(&ipc);
	if (len < 0)
		return len;

	while ((pkt = node_get_event(&ipc))) {
		events++;
		handle_ipc_event(pkt);
		free(pkt);
	}

	return 0;
}

static int io_poll_sockets(void)
{
	fd_set rd, wr;
	int sel_val, ipc_listen_sock, nfound;
	int sockets = 0;
	struct timeval tv = { 2, 0 };
	static time_t last_ipc_reinit = 0;

	/*
	 * Try re-initializing ipc if the module isn't connected
	 * and it was a while since we tried it.
	 */
	if (ipc.sock < 0 && last_ipc_reinit + 5 < time(NULL)) {
		ipc_reinit();
		last_ipc_reinit = time(NULL);
	}

	ipc_listen_sock = ipc_listen_sock_desc();
	sel_val = max(ipc.sock, ipc_listen_sock);

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	if (ipc.sock >= 0)
		FD_SET(ipc.sock, &rd);
	if (ipc_listen_sock >= 0)
		FD_SET(ipc_listen_sock, &rd);

	sel_val = net_polling_helper(&rd, &wr, sel_val);
	if (sel_val < 0)
		return 0;

	nfound = select(sel_val + 1, &rd, &wr, NULL, &tv);
	if (nfound < 0) {
		lerr("select() returned %d (errno = %d): %s", nfound, errno, strerror(errno));
		return -1;
	}

	if (ipc_listen_sock > 0 && FD_ISSET(ipc_listen_sock, &rd)) {
		linfo("Accepting inbound connection on ipc socket");
		ipc_accept();
	} else if (ipc.sock > 0 && FD_ISSET(ipc.sock, &rd)) {
		sockets++;
		ipc_reap_events();
	}

	sockets += net_handle_polling_results(&rd, &wr);

	return 0;
}

static void dump_daemon_nodes(void)
{
	int fd;
	unsigned int i;

	user_sig &= ~(1 << SIGUSR1);

	fd = open("/tmp/merlind.nodeinfo", O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) {
		lerr("USERSIG: Failed to open /tmp/merlind.nodeinfo for dumping: %s", strerror(errno));
		return;
	}

	dump_nodeinfo(&ipc, fd, 0);
	for (i = 0; i < num_nodes; i++)
		dump_nodeinfo(node_table[i], fd, i + 1);
}

static void polling_loop(void)
{
	for (;!merlind_sig;) {
		uint i;

		if (user_sig & (1 << SIGUSR1))
			dump_daemon_nodes();

		/*
		 * log the event count. The marker to prevent us from
		 * spamming the logs is in log_event_count() in logging.c
		 */
		ipc_log_event_count();

		/* When the module is disconnected, we can't validate handshakes,
		 * so any negotiation would need to be redone after the module
		 * has started. Don't even bother.
		 */
		if (ipc.state == STATE_CONNECTED) {
			while (!merlind_sig && net_accept_one() >= 0)
				; /* nothing */

			for (i = 0; !merlind_sig && i < num_nodes; i++) {
				merlin_node *node = node_table[i];
				/* try connecting if we're not already */
				if (!net_is_connected(node) && node->state == STATE_NONE) {
					net_try_connect(node);
				}
			}
		}

		if (merlind_sig)
			return;

		/*
		 * io_poll_sockets() is the real worker. It handles network
		 * and ipc based IO and ships inbound events off to their
		 * right destination.
		 */
		io_poll_sockets();

		if (merlind_sig)
			return;

		/*
		 * Try to commit any outstanding queries
		 */
		sql_try_commit(0);
	}
}


static void clean_exit(int sig)
{
	if (sig) {
		lwarn("Caught signal %d. Shutting down", sig);
	}

	ipc_deinit();
	sql_close();
	net_deinit();
	log_deinit();
	daemon_shutdown();

	if (!sig || sig == SIGINT || sig == SIGTERM)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}

static void merlind_sighandler(int sig)
{
	merlind_sig = sig;
}

static void sigusr_handler(int sig)
{
	user_sig |= 1 << sig;
}

int merlind_main(int argc, char **argv)
{
	int i, result, status = 0;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	is_module = 0;
	self = &merlind;
	ipc_init_struct();
	gettimeofday(&merlind.start, NULL);

	/*
	 * Solaris doesn't support MSG_NOSIGNAL, so
	 * we ignore SIGPIPE globally instead
	 */
	signal(SIGPIPE, SIG_IGN);

	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];

		if (*arg != '-') {
			if (!merlin_config_file) {
				merlin_config_file = arg;
				continue;
			}
			goto unknown_argument;
		}

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			usage(NULL);
		if (!strcmp(arg, "-k") || !strcmp(arg, "--kill")) {
			killing = 1;
			continue;
		}
		if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
			debug++;
			continue;
		}
		if (!strcmp(arg, "-s")) {
			status = 1;
			continue;
		}

		if ((opt = strchr(arg, '=')))
			opt++;
		else if (i < argc - 1)
			opt = argv[i + 1];
		else
			usage("Unknown argument, or argument '%s' requires a parameter", arg);

		i++;
		if (!strcmp(arg, "--config") || !strcmp(arg, "-c")) {
			merlin_config_file = opt;
			continue;
		}
		unknown_argument:
		usage("Unknown argument: %s", arg);
	}

	if (!merlin_config_file)
		usage("No config-file specified\n");

	merlin_config_file = nspath_absolute(merlin_config_file, NULL);
	if (!grok_config(merlin_config_file)) {
		fprintf(stderr, "%s contains errors. Bailing out\n", merlin_config_file);
		return 1;
	}

	if (!pidfile)
		pidfile = PKGRUNDIR "/merlin.pid";

	if (killing)
		return kill_daemon(pidfile);

	if (status)
		return daemon_status(pidfile);

	if (use_database && !import_program) {
		lwarn("Using database, but no import program configured. Are you sure about this?");
		lwarn("If not, make sure you specify the import_program directive in");
		lwarn("the \"daemon\" section of your merlin configuration file");
	}

	log_init();
	ipc.action = ipc_action_handler;
	result = ipc_init();
	if (result < 0) {
		lerr("Failed to initalize ipc socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (net_init() < 0) {
		lerr("Failed to initialize networking: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!debug) {
		if (daemonize(merlin_user, NULL, pidfile, 0) < 0) {
			lerr("Failed to daemonize. Exiting\n");
			exit(EXIT_FAILURE);
		}

		/*
		 * we'll leak these file-descriptors, but that
		 * doesn't really matter as we just want accidental
		 * output to go somewhere where it'll be ignored
		 */
		fclose(stdin);
		open("/dev/null", O_RDONLY);
		fclose(stdout);
		open("/dev/null", O_WRONLY);
		fclose(stderr);
		open("/dev/null", O_WRONLY);
	}

	signal(SIGINT, merlind_sighandler);
	signal(SIGTERM, merlind_sighandler);
	signal(SIGUSR1, sigusr_handler);
	signal(SIGUSR2, sigusr_handler);

	sql_init();
	state_init();
	linfo("Merlin daemon " PACKAGE_VERSION " successfully initialized");
	polling_loop();
	state_deinit();
	clean_exit(0);

	return 0;
}
