#define _GNU_SOURCE
#include <signal.h>
#include "sql.h"
#include "daemonize.h"
#include "daemon.h"

extern const char *__progname;

int use_database;
static const char *pidfile, *merlin_user;
static char *import_program;
int default_port = 15551;
static size_t hosts, services;
static int import_running = 0;

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
	/* only NOCs can take over checks */
	if (node->type != MODE_POLLER)
		return 0;

	ldebug("Handling action %d for node '%s'", action, node->name);

	switch (action) {
	case STATE_CONNECTED:
		return ipc_send_ctrl(CTRL_ACTIVE, node->id);
	case STATE_NONE:
		return ipc_send_ctrl(CTRL_INACTIVE, node->id);
	}

	return 1;
}


static void grok_node(struct cfg_comp *c, merlin_node *node)
{
	unsigned int i;

	if (!node)
		return;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!v->value)
			cfg_error(c, v, "Variable must have a value\n");

		if (node->type != MODE_NOC && !strcmp(v->key, "hostgroup")) {
			node->hostgroup = strdup(v->value);
			node->selection = add_selection(node->hostgroup);
		}
		else if (!strcmp(v->key, "address")) {
			if (!net_resolve(v->value, &node->sain.sin_addr))
				cfg_error(c, v, "Unable to resolve '%s'\n", v->value);
		}
		else if (!strcmp(v->key, "port")) {
			node->sain.sin_port = ntohs((unsigned short)atoi(v->value));
			if (!node->sain.sin_port)
				cfg_error(c, v, "Illegal value for port: %s\n", v->value);
		}
		else
			cfg_error(c, v, "Unknown variable\n");
	}
	node->action = node_action_handler;
	node->last_action = -1;
}

static void grok_daemon_compound(struct cfg_comp *comp)
{
	int i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "port")) {
			char *endp;

			default_port = strtoul(v->value, &endp, 0);
			if (default_port < 1 || default_port > 65535 || *endp)
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
		int vi;

		if (!prefixcmp(c->name, "database")) {
			use_database = 1;
			for (vi = 0; vi < c->vars; vi++) {
				struct cfg_var *v = c->vlist[vi];
				sql_config(v->key, v->value);
			}
		}
	}
}

static int grok_config(char *path)
{
	int i, node_i = 0;
	struct cfg_comp *config;
	merlin_node *table;

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

	/* each compound represents either a node or an error. we'll bail
	 * on errors, but it's nice to keep nodes in continuous memory */
	table = calloc(config->nested, sizeof(merlin_node));

	for (i = 0; i < config->nested; i++) {
		struct cfg_comp *c = config->nest[i];
		merlin_node *node;

		if (!prefixcmp(c->name, "module"))
			continue;

		if (!prefixcmp(c->name, "daemon")) {
			grok_daemon_compound(c);
			continue;
		}

		node = &table[node_i++];
		node->name = next_word((char *)c->name);

		if (!prefixcmp(c->name, "poller")) {
			node->type = MODE_POLLER;
			grok_node(c, node);
			if (!node->hostgroup)
				cfg_error(c, NULL, "Missing 'hostgroup' variable\n");
		}
		else if (!prefixcmp(c->name, "peer")) {
			node->type = MODE_PEER;
			grok_node(c, node);
		}
		else if (!prefixcmp(c->name, "noc")) {
			node->type = MODE_NOC;
			grok_node(c, node);
		}
		else
			cfg_error(c, NULL, "Unknown compound type\n");

		if (node->name)
			node->name = strdup(node->name);
		else
			node->name = strdup(inet_ntoa(node->sain.sin_addr));

		node->sock = -1;
	}

	cfg_destroy_compound(config);

	create_node_tree(table, node_i);

	return 1;
}

/** FIXME: this is fugly and lacks error checking */
static int import_objects_and_status(char *cfg, char *cache, char *status)
{
	char *cmd = NULL, *cmd2 = NULL;
	int result, pid;

	/* don't bother if we're not using a datbase */
	if (!use_database)
		return 0;

	/* ... or if an import is already in progress */
	if (import_running) {
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
		asprintf(&cmd2, "%s --status-log=%s", cmd, status);
		free(cmd);
		cmd=cmd2;
	}

	linfo("Executing import command '%s'", cmd);
	pid = fork();
	if (pid < 0) {
		lerr("Skipping import due to failed fork(): %s", strerror(errno));
	} else if (!pid) {
		/* child runs import program */
		result = system(cmd);
		free(cmd);
		exit(0);
	}

	/* mark import as running in parent */
	import_running = pid;
	free(cmd);

	return result;
}

/* nagios.cfg, objects.cache and (optionally) status.log */
static char *nagios_paths[3] = { NULL, NULL, NULL };
static char *nagios_paths_arena;
static int read_nagios_paths(merlin_event *pkt)
{
	int i;
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

	result = net_send_ipc_data(pkt);
	if (use_database)
		result |= mrm_db_update(pkt);

	return result;
}

static int max(int a, int b)
{
	return a > b ? a : b;
}

static int ipc_reap_events(int ipc_sock)
{
	int ipc_events = 0;
	merlin_event p;

	/*
	 * we expect to get the vast majority of events from the ipc
	 * socket, so make sure we read a bunch of them in one go
	 */
	while (ipc_read_event(&p) > 0) {
		ipc_events++;
		handle_ipc_event(&p);
	}

	return ipc_events;
}

static int io_poll_sockets(void)
{
	fd_set rd, wr;
	int sel_val, ipc_sock, ipc_listen_sock, net_sock, nfound;
	int net_sel_val, sockets = 0;

	sel_val = net_sock = net_sock_desc();
	ipc_listen_sock = ipc_listen_sock_desc();
	ipc_sock = ipc_sock_desc();
	sel_val = max(sel_val, max(ipc_sock, ipc_listen_sock));

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	if (ipc_sock > 0)
		FD_SET(ipc_sock, &rd);
	FD_SET(ipc_listen_sock, &rd);
	if (net_sock > 0)
		FD_SET(net_sock, &rd);

	net_sel_val = net_polling_helper(&rd, &wr, sel_val);
	sel_val = max(sel_val, net_sel_val);
	nfound = select(sel_val + 1, &rd, &wr, NULL, NULL);
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
		ipc_reap_events(ipc_sock);
	}

	/* check for inbound connections */
	if (FD_ISSET(net_sock, &rd)) {
		net_accept_one();
		sockets++;
	}

	sockets += net_handle_polling_results(&rd, &wr);

	return 0;
}

static void polling_loop(void)
{
	time_t start, stop;

	start = time(NULL);

	for (;;) {
		int status;

		stop = time(NULL);
		if (start + 15 >= stop) {
			ipc_log_event_count();
			start = stop;
		}

		/*
		 * reap any stray child processes.
		 * if the import isn't done yet waitpid() will return 0
		 * and we won't touch import_running at all.
		 */
		if (import_running) {
			int pid = waitpid(-1, &status, WNOHANG);

			if (pid < 0)
				lerr("waitpid() failed: %s", strerror(errno));
			else if (pid == import_running)
				import_running = 0;
		}
		io_poll_sockets();
	}
}


static void clean_exit(int sig)
{
	ipc_deinit();
	net_deinit();

	_exit(!!sig);
}


int main(int argc, char **argv)
{
	int i, result, stop = 0;
	char *config_file = NULL;

	is_module = 0;

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

	if (use_database)
		mrm_ipc_set_connect_handler(sql_reinit);

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
	prime_object_states(&hosts, &services);
	linfo("Primed object states for %zu hosts and %zu services",
		  hosts, services);
	linfo("Merlin daemon %s successfully initialized", merlin_version);
	polling_loop();

	clean_exit(0);

	return 0;
}
