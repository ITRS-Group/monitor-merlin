#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "config.h"
#include "logging.h"
#include "ipc.h"
#include "configuration.h"
#include "shared.h"
#include "sql.h"
#include "script-helpers.h"

static int import_running;

static void log_child_output(const char *prefix, char *buf)
{
	char *eol, *sol;

	if (!buf || !*buf) {
		lwarn("%s: ", prefix);
		return;
	}

	sol = buf;
	do {
		eol = strchr(sol, '\n');
		if (eol)
			*eol = 0;
		lwarn("%s: %s", prefix, sol);
		sol = eol + 1;
	} while (eol);
}

static void log_child_result(wproc_result *wpres, const char *fmt, ...)
{
	int status;
	char *name;
	va_list ap;

	if (!wpres || !fmt)
		return;

	va_start(ap, fmt);
	if (vasprintf(&name, fmt, ap) < 0) {
		name = strdup(fmt);
	}
	va_end(ap);

	status = wpres->wait_status;

	if (WIFEXITED(status)) {
		if (!WEXITSTATUS(status)) {
			linfo("%s finished successfully", name);
		} else {
			lwarn("%s exited with return code %d", name, WEXITSTATUS(status));
			lwarn("command: %s", wpres->command);
			log_child_output("stdout", wpres->outstd);
			log_child_output("stderr", wpres->outerr);
		}
	} else {
		if (WIFSIGNALED(status)) {
			lerr("%s was terminated by signal %d. %s core dump was produced",
			     name, WTERMSIG(status), WCOREDUMP(status) ? "A" : "No");
		} else {
			lerr("%s was shut down by an unknown source", name);
		}
		lerr("command: %s", wpres->command);
		log_child_output("stdout", wpres->outstd);
		log_child_output("stderr", wpres->outerr);
	}
}

static void handle_import_finished(wproc_result *wpres, void *arg, int flags)
{
	if (arg && flags) {
		/* it never is */
		lwarn("The impossible happened\n");
	}
	import_running = 0;
	log_child_result(wpres, "import");
}

static void handle_csync_finished(wproc_result *wpres, void *arg, int flags)
{
	const char *what = "push";
	merlin_child *child = (merlin_child *)arg;

	if (flags) {
		lwarn("handle_csync_finished() flags: %d", flags);
	}
	if (child == &child->node->csync.fetch)
		what = "fetch";
	log_child_result(wpres, "CSYNC: oconf %s to %s %s", what,
					 node_type(child->node), child->node->name);
}

/*
 * Run the import script
 */
int import_objects(char *cfg, char *cache)
{
	char *cmd;
	int ret;

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

	ret = asprintf(&cmd, "%s --nagios-cfg='%s' "
			 "--db-type='%s' --db-name='%s' --db-user='%s' --db-pass='%s' --db-host='%s' --db-conn_str='%s'",
			 import_program, cfg,
			 sql_db_type(), sql_db_name(), sql_db_user(), sql_db_pass(), sql_db_host(), sql_db_conn_str());

	if (ret > 0 && cache && *cache) {
		char *cmd2 = cmd;
		ret = asprintf(&cmd, "%s --cache='%s'", cmd2, cache);
		free(cmd2);
	}

	if (ret > 0 && sql_db_port()) {
		char *cmd2 = cmd;
		ret = asprintf(&cmd, "%s --db-port='%u'", cmd2, sql_db_port());
		free(cmd2);
	}

	if (ret < 0) {
		lerr("Failed to run import program (asprintf failed): %s",
			 strerror(errno));
		return -1;
	}

	wproc_run_callback(cmd, 300, handle_import_finished, NULL, NULL);
	import_running = 1;
	free(cmd);

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
 *
 * @param node The node to push to/fetch from
 * @param tdelta The timestamp delta on the configuration.
 *   < 0 indicates we should prefer pushing.
 *   > 0 indicates we should prefer fetching.
 */
void csync_node_active(merlin_node *node, int tdelta)
{
	time_t now;
	int val = 0;
	merlin_confsync *cs = NULL;
	merlin_child *child = NULL;

	ldebug("CSYNC: %s: Checking...", node->name);
	/* bail early if we have no push/fetch configuration */
	cs = &node->csync;
	if (!cs->push.cmd && !cs->fetch.cmd) {
		ldebug("CSYNC: %s: No config sync configured.", node->name);
		node_disconnect(node, "Disconnecting from %s, as config can't be synced", node->name);
		return;
	}

	if (!(node->flags & MERLIN_NODE_CONNECT) && !node->csync.configured) {
		if (node->type == MODE_POLLER || (node->type == MODE_PEER && tdelta < 0)) {
			ldebug("CSYNC: %s node %s configured with 'connect = no'.",
				   node_type(node), node->name);
		}
		return;
	}

	if (node->type == MODE_MASTER) {
		if (cs->fetch.cmd && strcmp(cs->fetch.cmd, "no")) {
			child = &cs->fetch;
			ldebug("CSYNC: We'll try to fetch");
		} else {
			ldebug("CSYNC: Refusing to push to a master node");
		}
	} else if (node->type == MODE_POLLER) {
		if (cs->push.cmd && strcmp(cs->push.cmd, "no")) {
			child = &cs->push;
			ldebug("CSYNC: We'll try to push");
		} else {
			ldebug("CSYNC: Should have pushed, but push not configured for %s", node->name);
		}
	} else {
		if (val < 0) {
			if (cs->push.cmd && strcmp(cs->push.cmd, "no")) {
				child = &cs->push;
				ldebug("CSYNC: We'll try to push");
			} else {
				ldebug("CSYNC: Should have pushed, but push not configured for %s", node->name);
			}
		} else if (val > 0) {
			if (cs->fetch.cmd && strcmp(cs->fetch.cmd, "no")) {
				child = &cs->fetch;
				ldebug("CSYNC: We'll try to fetch");
			} else {
				ldebug("CSYNC: Should have fetched, but fetch not configured for %s", node->name);
			}
		}
	}

	if (!child) {
		ldebug("CSYNC: No action required for %s", node->name);
		return;
	}

	if (child->is_running) {
		ldebug("CSYNC: '%s' already running for %s", child->cmd, node->name);
		return;
	}

	now = time(NULL);
	if (node->csync_last_attempt >= now - 30) {
		ldebug("CSYNC: Config sync attempted %lu seconds ago. Waiting at least %lu seconds",
		       now - node->csync_last_attempt, 30 - (now - node->csync_last_attempt));
		return;
	}

	node->csync_num_attempts++;
	linfo("CSYNC: triggered against %s node %s; val: %d; command: [%s]",
	      node_type(node), node->name, val, child->cmd);
	node->csync_last_attempt = now;
	child->node = node;
	/* special case this for testing */
	if (strcmp(child->cmd, ":")) {
		child->is_running = 1;
		wproc_run_callback(child->cmd, 600, handle_csync_finished, child, NULL);
	}
}
