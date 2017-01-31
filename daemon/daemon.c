#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "daemonize.h"
#include "db_updater.h"
#include "config.h"
#include "logging.h"
#include "ipc.h"
#include "configuration.h"
#include "sql.h"
#include "state.h"
#include "shared.h"
#include "db_updater.h"
#include <unistd.h>

static const char *progname;
static const char *pidfile, *merlin_user;
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

static void grok_daemon_compound(struct cfg_comp *comp)
{
	uint i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->key, "pidfile")) {
			pidfile = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "merlin_user")) {
			merlin_user = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "import_program")) {
			/* ignored */
			lwarn("daemon config: import_program is deprecated and no longer used");
			continue;
		}

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->key, v->value))
			continue;

		/* handled by the module now adays */
		if (!strcmp(v->key, "port") || !strcmp(v->key, "address"))
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

		if (!strcmp(v->key, "port") || !strcmp(v->key, "address"))
			continue;

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

	return 1;
}


static int handle_ipc_event(merlin_event *pkt)
{
	/* get out asap if we're not using a database */
	if (!use_database)
		return 0;

	/* Skip uninteresting events, but warn coders about them */
	if (!daemon_wants(pkt->hdr.type)) {
		ldebug("Received %s packet, which I don't want", callback_name(pkt->hdr.type));
		return 0;
	}

	return mrm_db_update(&ipc, pkt);
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
		if (pkt->hdr.type != CTRL_PACKET) {
			handle_ipc_event(pkt);
		} else {
			switch (pkt->hdr.code) {
			case CTRL_PATHS:
				break;

			case CTRL_ACTIVE:
				if (node_compat_cmp(&ipc, pkt)) {
					lerr("ipc is incompatible with us. Recent update?");
					node_disconnect(&ipc, "Incompatible node");
					break;
				}
				node_set_state(&ipc, STATE_CONNECTED, "Connected");
				memcpy(&ipc.info, pkt->body, sizeof(ipc.info));
				break;

			case CTRL_INACTIVE:
				/* our naemon instance might be restarting */
				memset(&ipc.info, 0, sizeof(ipc.info));
				break;
			default:
				break;
			}
		}

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
		if (user_sig & (1 << SIGUSR1))
			dump_daemon_nodes();

		/*
		 * log the event count. The marker to prevent us from
		 * spamming the logs is in log_event_count() in logging.c
		 */
		ipc_log_event_count();

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

	log_init();
	ipc.action = NULL;
	result = ipc_init();
	if (result < 0) {
		lerr("Failed to initalize ipc socket: %s\n", strerror(errno));
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
