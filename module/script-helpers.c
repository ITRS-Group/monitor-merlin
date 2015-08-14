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

/*
 * Run the import script
 */
int import_objects(char *cfg, char *cache)
{
	char *cmd;
	int result = 0;

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

	asprintf(&cmd, "%s --nagios-cfg='%s' "
			 "--db-type='%s' --db-name='%s' --db-user='%s' --db-pass='%s' --db-host='%s' --db-conn_str='%s'",
			 import_program, cfg,
			 sql_db_type(), sql_db_name(), sql_db_user(), sql_db_pass(), sql_db_host(), sql_db_conn_str());
	if (cache && *cache) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --cache='%s'", cmd2, cache);
		free(cmd2);
	}

	if (sql_db_port()) {
		char *cmd2 = cmd;
		asprintf(&cmd, "%s --db-port='%u'", cmd2, sql_db_port());
		free(cmd2);
	}

	wproc_run_callback(cmd, 300, handle_import_finished, NULL, NULL);
	import_running = 1;
	free(cmd);

	return result;
}
