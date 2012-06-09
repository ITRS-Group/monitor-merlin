#define _GNU_SOURCE
#include <signal.h>
#include "sql.h"
#include "daemonize.h"
#include "daemon.h"

static const char *progname;
static const char *pidfile, *merlin_user;
static char *import_program;
unsigned short default_port = 15551;
unsigned int default_addr = 0;
static int importer_pid;
static char *merlin_conf;
static merlin_confsync csync;
static int num_children;
static int killing;
int db_log_reports = 1;
int db_log_notifications = 1;

static void usage(char *fmt, ...)
	__attribute__((format(printf,1,2)));

static void dump_core(int sig)
{
	kill(getpid(), SIGSEGV);
	exit(1);
}

static void usage(char *fmt, ...)
{
	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
	}

	printf("Usage: %s -c <config-file> [-d] [-h]\n\n", progname);

	exit(1);
}

void db_mark_node_inactive(merlin_node *node)
{
	int node_id;

	if (!use_database)
		return;

	node_id = node == &ipc ? 0 : node->id + 1;
	sql_query("UPDATE program_status "
	          "SET is_running = 0 "
	          "WHERE instance_id = %d",
	          node_id);
}

/* node connect/disconnect handlers */
static int node_action_handler(merlin_node *node, int prev_state)
{
	switch (node->state) {
	case STATE_CONNECTED:
		/*
		 * If we've received the timestamp marker from our module,
		 * We  need to send our own timestamp to the other end so
		 * they know how to sort us in case we're a peer to them.
		 */
		if (ipc.info.start.tv_sec && ipc_is_connected(0)) {
			node_send_ctrl_active(node, CTRL_GENERIC, &ipc.info, 100);
		}
		break;

	case STATE_PENDING:
	case STATE_NEGOTIATING:
	case STATE_NONE:
		/* only send INACTIVE if we haven't already */
		if (prev_state == STATE_CONNECTED) {
			db_mark_node_inactive(node);
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
		if (sql_is_connected(1)) {
			sql_query("UPDATE program_status SET "
			          "is_running = 1, last_alive = %lu "
			          "WHERE instance_id = 0", time(NULL));
		}

		for (i = 0; i < num_nodes; i++) {
			merlin_node *node = node_table[i];
			if (node->state == STATE_CONNECTED && node->info.start.tv_sec > 0)
				ipc_send_ctrl_active(node->id, &node->info);
			else
				ipc_send_ctrl_inactive(node->id);

			/*
			 * we mustn't notify our connected nodes that the module once
			 * again came online, since the module is supposed to create
			 * that event itself
			 */
		}
		break;

	case STATE_PENDING:
	case STATE_NEGOTIATING:
	case STATE_NONE:
		/* if ipc wasn't connected before, we return early */
		if (prev_state != STATE_CONNECTED)
			return 0;

		/* make sure the gui knows the module isn't running any more */
		db_mark_node_inactive(&ipc);

		/* also tell our peers and masters */
		for (i = 0; i < num_masters + num_peers; i++) {
			merlin_node *node = node_table[i];
			node_send_ctrl_inactive(node, CTRL_GENERIC, 100);
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
		uint vi;

		if (!prefixcmp(c->name, "database")) {
			use_database = 1;
			for (vi = 0; vi < c->vars; vi++) {
				struct cfg_var *v = c->vlist[vi];
				if (!strcmp(v->key, "log_report_data")) {
					db_log_reports = strtobool(v->value);
				} else if (!prefixcmp(v->key, "log_notification")) {
					db_log_notifications = strtobool(v->value);
				} else {
					sql_config(v->key, v->value);
				}
			}
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

		if (!node->sain.sin_port)
			node->sain.sin_port = htons(default_port);

		node->action = node_action_handler;

		node->ioc.ioc_bufsize = MERLIN_IOC_BUFSIZE;
		node->ioc.ioc_buf = malloc(node->ioc.ioc_bufsize);
		if (!node->ioc.ioc_buf) {
			lerr("Failed to malloc(%lu) for io cache for node %s. Aborting",
				 node->ioc.ioc_bufsize, node->name);
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

/*
 * if the import isn't done yet waitpid() will return 0
 * and we won't touch importer_pid at all.
 */
static void reap_child_process(void)
{
	int status, pid, i;

	if (!num_children)
		return;

	pid = waitpid(-1, &status, WNOHANG);
	if (pid < 0) {
		if (errno == ECHILD) {
			/* no child running. Just reset */
			num_children = importer_pid = 0;
		} else {
			/* some random error. log it */
			lerr("waitpid(-1...) failed: %s", strerror(errno));
		}

		return;
	}

	/* child may not be done yet */
	if (!pid)
		return;

	/* we reaped an actual child, so decrement the counter */
	num_children--;

	/* looks like we reaped some helper we spawned */
	linfo("Child with pid %d successfully reaped", pid);

	if (pid == importer_pid) {
		if (WIFEXITED(status)) {
			if (!WEXITSTATUS(status)) {
				linfo("import program finished. Resuming normal operations");
			} else {
				lwarn("import program exited with return code %d", WEXITSTATUS(status));
			}
		} else {
			lerr("import program stopped or killed. That's a Bad Thing(tm)");
		}
		/* successfully reaped, so reset and resume */
		importer_pid = 0;
		ipc_send_ctrl(CTRL_RESUME, CTRL_GENERIC);
		return;
	}

	/* not the importer program, so it must be an oconf push or fetch */
	if (pid == csync.push.pid) {
		linfo("CSYNC: global push finished. Resuming");
		csync.push.pid = 0;
		return;
	} else if (pid == csync.fetch.pid) {
		linfo("CSYNC: global fetch finished. Resuming");
		csync.fetch.pid = 0;
		return;
	}

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		if (!node->csync)
			continue;

		if (pid == node->csync->push.pid) {
			linfo("CSYNC: push finished for %s", node->name);
			node->csync->push.pid = 0;
			return;
		} else if (pid == node->csync->fetch.pid) {
			linfo("CSYNC: fetch finished from %s", node->name);
			node->csync->fetch.pid = 0;
			return;
		}
	}
}

/*
 * Run a program, stashing the child pid in *pid.
 * Since it's not supposed to run all that often, we don't care a
 * whole lot about performance and lazily run all commands through
 * /bin/sh for argument handling
 */
static void run_program(char *what, char *cmd, int *prog_id)
{
	char *args[4] = { "sh", "-c", cmd, NULL };
	int pid;

	linfo("Executing %s command '%s'", what, cmd);
	pid = fork();
	if (!pid) {
		/*
		 * child runs the command. if execvp() returns, that means it
		 * failed horribly and that we're basically screwed
		 */
		execv("/bin/sh", args);
		lerr("execv() failed: %s", strerror(errno));
		exit(1);
	}
	if (pid < 0) {
		lerr("Skipping %s due to failed fork(): %s", what, strerror(errno));
		return;
	}
	/*
	 * everything went ok, so update prog_id if passed
	 * and increment num_children
	 */
	if (prog_id)
		*prog_id = pid;
	num_children++;
}

/*
 * import objects and status from objects.cache and status.log,
 * respecively
 */
static int import_objects_and_status(char *cfg, char *cache, char *status)
{
	char *cmd;
	int result = 0;

	/* don't bother if we're not using a datbase */
	if (!use_database)
		return 0;

	/* ... or if an import is already in progress */
	if (importer_pid) {
		lwarn("Import already in progress. Ignoring import event");
		return 0;
	}

	if (!import_program) {
		lerr("No import program specified. Ignoring import event");
		return 0;
	}

	asprintf(&cmd, "%s --nagios-cfg='%s' "
			 "--db-type='%s' --db-name='%s' --db-user='%s' --db-pass='%s' --db-host='%s' --db-conn_str='%s'",
			 import_program, cfg,
			 sql_db_type(), sql_db_name(), sql_db_user(), sql_db_pass(), sql_db_host(), sql_db_conn_str());
	if (cache && *cache) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --cache=%s", cmd2, cache);
		free(cmd2);

		if (status && *status) {
			cmd2 = cmd;
			asprintf(&cmd, "%s --status-log=%s", cmd2, status);
			free(cmd2);
		}
	}

	if (sql_db_port()) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --db-port='%u'", cmd2, sql_db_port());
		free(cmd2);
	}

	run_program("import", cmd, &importer_pid);
	free(cmd);

	/*
	 * If the import program started successfully, we
	 * ask the module to stall events until it's done
	 */
	if (importer_pid > 0) {
		ipc_send_ctrl(CTRL_STALL, CTRL_GENERIC);
	}

	return result;
}

/* nagios.cfg, objects.cache (optional) and status.log (optional) */
static int read_nagios_paths(merlin_event *pkt)
{
	char *nagios_paths_arena;
	char *npath[3] = { NULL, NULL, NULL };
	uint i;
	size_t offset = 0;

	if (!use_database)
		return 0;

	nagios_paths_arena = malloc(pkt->hdr.len);
	if (!nagios_paths_arena)
		return -1;
	memcpy(nagios_paths_arena, pkt->body, pkt->hdr.len);

	for (i = 0; i < ARRAY_SIZE(npath) && offset < pkt->hdr.len; i++) {
		npath[i] = nagios_paths_arena + offset;
		offset += strlen(npath[i]) + 1;
	}

	import_objects_and_status(npath[0], npath[1], npath[2]);
	free(nagios_paths_arena);
	/*
	 * we don't need to do this until we're merging the reports-module
	 * into merlin
	 */
	 /* prime_object_states(&hosts, &services); */

	return 0;
}

/*
 * Compares *node's info struct and returns:
 * 0 if node's config is same as ours (we should do nothing)
 * > 0 if node's config is newer than ours (we should fetch)
 * < 0 if node's config is older than ours (we should push)
 *
 * If hashes don't match but config is exactly the same
 * age, we instead return:
 * > 0 if node started after us (we should fetch)
 * < 0 if node started before us (we should push)
 *
 * If all of the above are identical, we return the hash delta.
 * This should only happen rarely, but it will ensure that not
 * both sides try to fetch or push at the same time.
 */
static int csync_config_cmp(merlin_node *node)
{
	int mtime_delta;

	ldebug("CSYNC: %s: Comparing config", node->name);
	if (!ipc.info.last_cfg_change) {
		/*
		 * if our module is inactive, we can't know anything so we
		 * can't do anything, and we can't fetch the last config
		 * change time, since it might be being changed as we speak.
		 */
		ldebug("CSYNC: %s: Our module is inactive, so can't check", node->name);
		return 0;
	}

	/*
	 * All peers must have identical configuration
	 */
	if (node->type == MODE_PEER) {
		int hash_delta;
		hash_delta = memcmp(node->info.config_hash, ipc.info.config_hash, 20);
		if (!hash_delta) {
			ldebug("CSYNC: %s: hashes match. No sync required", node->name);
			return 0;
		}
	}

	/* For non-peers, we simply move on from here. */
	mtime_delta = node->info.last_cfg_change - ipc.info.last_cfg_change;
	if (mtime_delta || node->type != MODE_PEER) {
		ldebug("CSYNC: %s: mtime_delta: %d", node->name, mtime_delta);
		return mtime_delta;
	}

	/*
	 * Error path. This node is a peer, but we have a hash mismatch
	 * and matching mtimes. Unusual, to say the least. Either way,
	 * we can't really do anything except warn about it and get
	 * on with things. This will only happen when someone manages
	 * to save the config exactly the same second on both nodes.
	 */
	lerr("CSYNC: %s: Can't determine confsync action", node->name);
	lerr("CSYNC: %s: hash mismatch but mtime matches", node->name);
	lerr("CSYNC: %s: User intervention required.", node->name);

	return 0;
}

/*
 * executed when a node comes online and reports itself as
 * being active. This is where we run the configuration sync
 * if any is configured
 *
 * Note that the 'push' and 'fetch' options in the configuration
 * are simply guidance names. One could configure them in reverse
 * if one wanted, or make them boil noodles for the IT staff or
 * paint a skateboard blue for all Merlin cares. It will just
 * assume that things work out just fine so long as the config
 * is (somewhat) in sync.
 */
void csync_node_active(merlin_node *node)
{
	int val = 0;
	merlin_confsync *cs = NULL;
	merlin_child *child = NULL;

	ldebug("CSYNC: %s: Checking...", node->name);
	/* bail early if we have no push/fetch configuration */
	cs = node->csync ? node->csync : &csync;
	if (!cs->push.cmd && !cs->fetch.cmd) {
		ldebug("CSYNC: %s: No config sync configured.", node->name);
		return;
	}

	val = csync_config_cmp(node);
	if (!val)
		return;
	/*
	 * The most common setup is that configuration is done on a master
	 * node and then pushed to the pollers, so if a master has older
	 * config than we do, a node-specific "push" command is required
	 * to make us push to that master.
	 * This is to prevent normal setup behaviour from engaging in
	 * pingpong action with config-files when config-files take more
	 * than 1 second to generate
	 */
	if (val < 0 && node->type == MODE_MASTER && cs == &csync) {
		ldebug("CSYNC: Refusing to run global sync to a master node");
		return;
	}

	if (val < 0) {
		if (cs->push.cmd) {
			child = &cs->push;
			ldebug("CSYNC: We'll try to push");
		} else {
			ldebug("CSYNC: Should have pushed, but push not configured for %s", node->name);
		}
	} else if (val > 0) {
		if (cs->fetch.cmd) {
			child = &cs->fetch;
			ldebug("CSYNC: We'll try to fetch");
		} else {
			ldebug("CSYNC: Should have fetched, but fetch not configured for %s", node->name);
		}
	}

	if (!child) {
		ldebug("CSYNC: No action required for %s", node->name);
		return;
	}

	if (child->pid) {
		ldebug("CSYNC: '%s' already running for %s, or globally", child->cmd, node->name);
		return;
	}

	ldebug("CSYNC: node %s; val: %d; sync-command: [%s]", node->name, val, child->cmd);
	run_program("csync", child->cmd, &child->pid);
	if (child->pid > 0) {
		ldebug("CSYNC: command has pid %d", child->pid);
	} else {
		child->pid = 0;
	}
}


static int handle_ipc_event(merlin_event *pkt)
{
	int result = 0;
	unsigned int i;

	if (pkt->hdr.type == CTRL_PACKET) {
		switch (pkt->hdr.code) {
		case CTRL_PATHS:
			read_nagios_paths(pkt);
			return 0;

		case CTRL_ACTIVE:
			/*
			 * handle_ctrl_active() should basically never return
			 * anything but 0 or 1 from our module. If it does, it
			 * will already have logged everything the user needs
			 * to know, so we can just return without further
			 * actions, even if the info returned is the same as
			 * we already have, but if no problems were found we
			 * forward the packet to the network anyway.
			 */
			result = handle_ctrl_active(&ipc, pkt);
			if (result < 0) {
				/* ipc is incompatible with us. weird */
				return 0;
			}

			/*
			 * info checks out, so we forward it. If ipc resent same
			 * info, we won't do the config sync check
			 */
			if (result >= 1) {
				break;
			}

			/*
			 * info checks out and is new. Check to see if we
			 * should run any config sync actions.
			 */
			for (i = 0; i < num_nodes; i++) {
				merlin_node *node = node_table[i];
				/* skip nodes that have no start or mtime */
				if (!node->info.start.tv_sec || !node->info.last_cfg_change)
					continue;
				csync_node_active(node_table[i]);
			}

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

	len = node_recv(&ipc, MSG_DONTWAIT);
	if (len < 0)
		return len;

	while ((pkt = node_get_event(&ipc))) {
		events++;
		handle_ipc_event(pkt);
	}
	ldebug("Read %d events in %s from %s", events, human_bytes(len), ipc.name);

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
		sleep(1);
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

static void polling_loop(void)
{
	for (;;) {
		uint i;
		time_t now = time(NULL);

		/*
		 * log the event count. The marker to prevent us from
		 * spamming the logs is in log_event_count() in logging.c
		 */
		ipc_log_event_count();

		/* reap any children that might have finished */
		reap_child_process();

		/*
		 * reap_child_process() resets importer_pid if
		 * the import is completed.
		 * if it's not and at tops 5 seconds have passed,
		 * ask for some more time.
		 */
		if (importer_pid && !(now % 5)) {
			ipc_send_ctrl(CTRL_STALL, CTRL_GENERIC);
		}

		/*
		 * We try accepting inbound connections first. This is kinda
		 * useful since we open the listening network socket before
		 * we launch into the ipc socket code. It's not rare for other
		 * nodes to have initiated connection attempts in that short
		 * time. if they have and are currently waiting for us to just
		 * accept that connection, we can humor them and avoid the
		 * whole socket negotiation thing.
		 */
		while (net_accept_one() >= 0)
			; /* nothing */

		/*
		 * Next we try to connect to all nodes that aren't yet
		 * connected. Quite often we'll run into firewall rules that
		 * say one network can't connect to the other, but not the
		 * other way around, so it's useful to try from both sides
		 */
		for (i = 0; i < num_nodes; i++) {
			merlin_node *node = node_table[i];
			/* try connecting if we're not already */
			if (!net_is_connected(node)) {
				net_try_connect(node);
			}
		}

		/*
		 * io_poll_sockets() is the real worker. It handles network
		 * and ipc based IO and ships inbound events off to their
		 * right destination.
		 */
		io_poll_sockets();

		/*
		 * Try to commit any outstanding queries
		 */
		sql_try_commit(0);
	}
}


static void clean_exit(int sig)
{
	ipc_deinit();
	sql_close();
	net_deinit();

	_exit(!!sig);
}


int main(int argc, char **argv)
{
	int i, result;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	is_module = 0;
	ipc_init_struct();
	gettimeofday(&self.start, NULL);


	/*
	 * Solaris doesn't support MSG_NOSIGNAL, so
	 * we ignore SIGPIPE globally instead
	 */
	signal(SIGPIPE, SIG_IGN);

	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];

		if (*arg != '-') {
			if (!merlin_conf) {
				merlin_conf = arg;
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

		if ((opt = strchr(arg, '=')))
			opt++;
		else if (i < argc - 1)
			opt = argv[i + 1];
		else
			usage("Unknown argument, or argument '%s' requires a parameter", arg);

		i++;
		if (!strcmp(arg, "--config") || !strcmp(arg, "-c")) {
			merlin_conf = opt;
			continue;
		}
		unknown_argument:
		usage("Unknown argument: %s", arg);
	}

	if (!merlin_conf)
		usage("No config-file specified\n");

	if (!grok_config(merlin_conf)) {
		fprintf(stderr, "%s contains errors. Bailing out\n", merlin_conf);
		return 1;
	}

	if (!pidfile)
		pidfile = "/var/run/merlin.pid";

	if (killing)
		return kill_daemon(pidfile);

	if (use_database && !import_program) {
		lwarn("Using database, but no import program configured. Are you sure about this?");
		lwarn("If not, make sure you specify the import_program directive in");
		lwarn("the \"daemon\" section of your merlin configuration file");
	}

	ipc.action = ipc_action_handler;
	result = ipc_init();
	if (result < 0) {
		printf("Failed to initalize ipc socket: %s\n", strerror(errno));
		return 1;
	}
	if (net_init() < 0) {
		printf("Failed to initialize networking: %s\n", strerror(errno));
		return 1;
	}

	if (!debug) {
		if (daemonize(merlin_user, NULL, pidfile, 0) < 0)
			exit(EXIT_FAILURE);

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

	signal(SIGINT, clean_exit);
	signal(SIGTERM, clean_exit);
	signal(SIGPIPE, dump_core);

	sql_init();
	if (use_database) {
		sql_query("TRUNCATE TABLE program_status");
		sql_query("INSERT INTO program_status(instance_id, instance_name, is_running) "
		          "VALUES(0, 'Local Nagios daemon', 0)");
		for (i = 0; i < (int)num_nodes; i++) {
			char *node_name;
			merlin_node *node = noc_table[i];

			sql_quote(node->name, &node_name);
			sql_query("INSERT INTO program_status(instance_id, instance_name, is_running) "
			          "VALUES(%d, %s, 0)", node->id + 1, node_name);
			safe_free(node_name);
		}
	}
	state_init();
	linfo("Merlin daemon %s successfully initialized", merlin_version);
	polling_loop();

	clean_exit(0);

	return 0;
}
