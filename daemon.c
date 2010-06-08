#define _GNU_SOURCE
#include <signal.h>
#include "sql.h"
#include "daemonize.h"
#include "daemon.h"

extern const char *__progname;

static const char *pidfile, *merlin_user;
static char *import_program;
unsigned short default_port = 15551;
static int importer_pid;

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

	printf("Usage: %s -c <config-file> [-d] [-h]\n\n", __progname);

	exit(1);
}

/* node connect/disconnect handlers */
static int node_action_handler(merlin_node *node, int action)
{
	/* only masters and peers need to know about state changes */
	if (node->type == MODE_NOC)
		return 0;

	switch (action) {
	case STATE_CONNECTED:
		ldebug("Sending IPC control ACTIVE for '%s'", node->name);
		return ipc_send_ctrl(CTRL_ACTIVE, node->id);
	case STATE_PENDING:
	case STATE_NEGOTIATING:
	case STATE_NONE:
		ldebug("Sending IPC control INACTIVE for '%s'", node->name);
		return ipc_send_ctrl(CTRL_INACTIVE, node->id);
	}

	return 1;
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
				sql_config(v->key, v->value);
			}
		}
	}
}

/* daemon-specific node manipulation */
static void post_process_nodes(void)
{
	int i;

	ldebug("post processing %d masters, %d pollers, %d peers",
	       num_nocs, num_pollers, num_peers);

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];

		if (!node) {
			lerr("node is null. i is %d. num_nodes is %d. wtf?", i, num_nodes);
			continue;
		}

		if (!node->sain.sin_port)
			node->sain.sin_port = htons(default_port);

		node->action = node_action_handler;
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

	node_grok_config(config);
	cfg_destroy_compound(config);
	post_process_nodes();

	return 1;
}

/*
 * if the import isn't done yet waitpid() will return 0
 * and we won't touch importer_pid at all.
 */
static void reap_child_process(void)
{
	int status, pid;

	if (!importer_pid)
		return;

	pid = waitpid(-1, &status, WNOHANG);
	if (pid == importer_pid) {
		if (WIFEXITED(status)) {
			if (!WEXITSTATUS(status)) {
				linfo("import program finished. Resuming normal operations");
			} else {
				lwarn("import program exited with return code %d", WEXITSTATUS(status));
			}
		} else {
			lerr("import program stopped or killed");
		}
		/* successfully reaped, so reset and resume */
		importer_pid = 0;
		ipc_send_ctrl(CTRL_RESUME, CTRL_GENERIC);
	} else if (pid < 0 && errno == ECHILD) {
		/* no child running. Just reset */
		importer_pid = 0;
	} else if (pid < 0) {
		/* some random error. log it */
		lerr("waitpid(-1...) failed: %s", strerror(errno));
	}
}

/*
 * this should only be executed by the child process
 * created in import_objects_and_status()
 */
static void run_import_program(char *cmd)
{
	char *args[4] = { "sh", "-c", cmd, NULL };

	execvp("/bin/sh", args);
	lerr("execvp failed: %s", strerror(errno));
}

/*
 * import objects and status from objects.cache and status.log,
 * respecively
 */
static int import_objects_and_status(char *cfg, char *cache, char *status)
{
	char *cmd;
	int result = 0, pid;

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

	asprintf(&cmd, "%s --nagios-cfg=%s --cache=%s "
			 "--db-name=%s --db-user=%s --db-pass=%s --db-host=%s",
			 import_program, cfg, cache,
			 sql_db_name(), sql_db_user(), sql_db_pass(), sql_db_host());
	if (status && *status) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --status-log=%s", cmd2, status);
		free(cmd2);
	}

	linfo("Executing import command '%s'", cmd);
	pid = fork();
	if (pid < 0) {
		lerr("Skipping import due to failed fork(): %s", strerror(errno));
	} else if (!pid) {
		/*
		 * child runs the actual import. if run_import_program()
		 * returns, execvp() failed and we're basically screwed.
		 */
		run_import_program(cmd);
		exit(1);
	}

	/* mark import as running in parent */
	importer_pid = pid;
	free(cmd);

	/* ask the module to stall events for us until we're done */
	ipc_send_ctrl(CTRL_STALL, CTRL_GENERIC);

	return result;
}

/* nagios.cfg, objects.cache and (optionally) status.log */
static char *nagios_paths[3] = { NULL, NULL, NULL };
static char *nagios_paths_arena;
static int read_nagios_paths(merlin_event *pkt)
{
	uint i;
	size_t offset = 0;

	if (!use_database)
		return 0;

	if (nagios_paths_arena)
		free(nagios_paths_arena);
	nagios_paths_arena = malloc(pkt->hdr.len);
	if (!nagios_paths_arena)
		return -1;
	memcpy(nagios_paths_arena, pkt->body, pkt->hdr.len);

	/*
	 * reset the path pointers first so we don't ship an
	 * invalid one to the importer function
	 */
	for (i = 0; i < ARRAY_SIZE(nagios_paths); i++) {
		nagios_paths[i] = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(nagios_paths) && offset < pkt->hdr.len; i++) {
		nagios_paths[i] = nagios_paths_arena + offset;
		offset += strlen(nagios_paths[i]) + 1;
	}

	import_objects_and_status(nagios_paths[0], nagios_paths[1], nagios_paths[2]);
	/*
	 * we don't need to do this until we're merging the reports-module
	 * into merlin
	 */
	 /* prime_object_states(&hosts, &services); */

	return 0;
}

static int handle_ipc_event(merlin_event *pkt)
{
	int result = 0;

	if (pkt->hdr.type == CTRL_PACKET) {
		if (pkt->hdr.code == CTRL_PATHS) {
			read_nagios_paths(pkt);
		}
		return 0;
	}

	/*
	 * we must send to the network before we run mrm_db_update(),
	 * since the latter deblockifies the packet and makes it
	 * unusable in network transfers without repacking
	 */
	result = net_send_ipc_data(pkt);

	/* skip sending control packets to database */
	if (use_database && pkt->hdr.type != CTRL_PACKET)
		result |= mrm_db_update(&ipc, pkt);

	return result;
}

static int ipc_reap_events(void)
{
	int ipc_events = 0;
	merlin_event p;

	/*
	 * we expect to get the vast majority of events from the ipc
	 * socket, so make sure we read a bunch of them in one go
	 */
	while (ipc_read_event(&p, 0) > 0) {
		ipc_events++;
		handle_ipc_event(&p);
	}

	return ipc_events;
}

static int io_poll_sockets(void)
{
	fd_set rd, wr;
	int sel_val, ipc_sock, ipc_listen_sock, net_sock, nfound;
	int sockets = 0;
	struct timeval tv = { 2, 0 };

	sel_val = net_sock = net_sock_desc();
	ipc_listen_sock = ipc_listen_sock_desc();
	ipc_sock = ipc_sock_desc();
	sel_val = max(sel_val, max(ipc_sock, ipc_listen_sock));

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	if (ipc_sock >= 0)
		FD_SET(ipc_sock, &rd);
	FD_SET(ipc_listen_sock, &rd);
	if (net_sock >= 0)
		FD_SET(net_sock, &rd);

	sel_val = net_polling_helper(&rd, &wr, sel_val);
	nfound = select(sel_val + 1, &rd, &wr, NULL, &tv);
	if (nfound < 0) {
		lerr("select() returned %d (errno = %d): %s", nfound, errno, strerror(errno));
		sleep(1);
		return -1;
	}

	if (!nfound) {
		check_all_node_activity();
		return 0;
	}

	if (ipc_listen_sock > 0 && FD_ISSET(ipc_listen_sock, &rd)) {
		linfo("Accepting inbound connection on ipc socket");
		ipc_accept();
	} else if (ipc_sock > 0 && FD_ISSET(ipc_sock, &rd)) {
		sockets++;
		ipc_reap_events();
	}

	/* check for inbound connections */
	if (net_sock >= 0 && FD_ISSET(net_sock, &rd)) {
		net_accept_one();
		sockets++;
	}

	sockets += net_handle_polling_results(&rd, &wr);

	return 0;
}

static void polling_loop(void)
{
	for (;;) {
		time_t now = time(NULL);

		/*
		 * log the event count. The marker to prevent us from
		 * spamming the logs is in log_event_count() in logging.c
		 */
		ipc_log_event_count();

		/* check if an import in progress is done yet */
		if (importer_pid) {
			reap_child_process();

			/*
			 * reap_child_process() resets importer_pid if
			 * the import is completed.
			 * if it's not and at tops 15 seconds have passed,
			 * ask for some more time.
			 */
			if (importer_pid && !(now % 15)) {
				ipc_send_ctrl(CTRL_STALL, CTRL_GENERIC);
			}
		}

		while (net_accept_one() >= 0)
			; /* nothing */

		io_poll_sockets();
	}
}


static void clean_exit(int sig)
{
	ipc_deinit();
	sql_close();
	net_deinit();

	_exit(!!sig);
}


static int on_ipc_connect(void)
{
	int i;

	if (use_database) {
		sql_reinit();
		sql_query("UPDATE %s.program_status SET "
		          "is_running = 1, last_alive = %lu "
		          "WHERE instance_id = 0", sql_db_name(), time(NULL));
	}

	for (i = 0; i < num_nodes; i++) {
		merlin_node *node = node_table[i];
		if (node->state == STATE_CONNECTED)
			ipc_send_ctrl(CTRL_ACTIVE, node->id);
		else
			ipc_send_ctrl(CTRL_INACTIVE, node->id);
	}

	return 0;
}

static int on_ipc_disconnect(void)
{
	/* make sure the gui knows the module isn't running any more */
	sql_query("UPDATE %s.program_status SET is_running = 0 "
			  "WHERE instance_id = 0", sql_db_name());
	return 0;
}

int main(int argc, char **argv)
{
	int i, result, stop = 0;
	char *config_file = NULL;

	is_module = 0;
	gettimeofday(&merlin_start, NULL);

	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];

		if (*arg != '-') {
			if (!config_file) {
				config_file = arg;
				continue;
			}
			goto unknown_argument;
		}

		if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			usage(NULL);
		if (!strcmp(arg, "-k") || !strcmp(arg, "--kill")) {
			stop = 1;
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
			config_file = opt;
			continue;
		}
		unknown_argument:
		usage("Unknown argument: %s", arg);
	}

	if (!config_file)
		usage("No config-file specified\n");

	if (!grok_config(config_file)) {
		fprintf(stderr, "%s contains errors. Bailing out\n", config_file);
		return 1;
	}

	if (use_database && !import_program) {
		fprintf(stderr, "Using database, but no import program configured\n");
		fprintf(stderr, "Make sure you specify the import_program directive in\n");
		fprintf(stderr, "the \"daemon\" section of your merlin configuration file\n");
		exit(EXIT_FAILURE);
	}

	if (!pidfile)
		pidfile = "/var/run/merlin.pid";

	if (stop)
		return kill_daemon(pidfile);

	if (use_database) {
		mrm_ipc_set_connect_handler(on_ipc_connect);
		mrm_ipc_set_disconnect_handler(on_ipc_disconnect);
	}

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
		sql_query("TRUNCATE program_status");
		sql_query("INSERT INTO program_status(instance_id, instance_name, is_running) "
				  "VALUES(0, 'Local Nagios daemon', 0)");
		for (i = 0; i < num_nodes; i++) {
			char *node_name;
			merlin_node *node = noc_table[i];

			sql_quote(node->name, &node_name);
			sql_query("INSERT INTO program_status(instance_id, instance_name, is_running) "
					  "VALUES(%d, %s, 0)", node->id + 1, node_name);
			safe_free(node_name);
		}
	}
	linfo("Merlin daemon %s successfully initialized", merlin_version);
	polling_loop();

	clean_exit(0);

	return 0;
}
