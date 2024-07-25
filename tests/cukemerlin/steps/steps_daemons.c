#include "steps_daemons.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <base/jsonx.h>
#include <errno.h>
#include <sys/wait.h>

/* TODO: For kill() call, which should be converted to glib-style */
#include <sys/types.h>
#include <signal.h>

/* TODO: For write and close, use something more glib */
#include <unistd.h>

typedef struct StepsDaemons_ {
	GPtrArray *processes;
} StepsDaemons;

typedef struct StepsDaemonProcess_ {
	GPid pid;
} StepsDaemonProcess;

static StepsDaemonProcess *dproc_new(const gchar *cmdline);
static void dproc_destroy(StepsDaemonProcess *dproc);
static gboolean waitpid_timeout(int pid, unsigned int timeout_sec);
static gboolean proc_run(const gchar *cmdline);

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);
STEP_DEF(step_start_daemon);
STEP_DEF(step_start_command);

CukeStepEnvironment steps_daemons = {
	.tag = "daemons",
	.begin_scenario = step_begin_scenario,
	.end_scenario = step_end_scenario,

	.definitions = {
		{ "^I start daemon (.*)$", step_start_daemon },
		{ "^I start command (.*)$", step_start_command },
		{ NULL, NULL }
	}
};

STEP_BEGIN(step_begin_scenario) {
	StepsDaemons *stps = g_malloc0(sizeof(StepsDaemons));
	stps->processes = g_ptr_array_new_with_free_func(
		(GDestroyNotify) dproc_destroy);
	return stps;
}
STEP_END(step_end_scenario) {
	StepsDaemons *stps = (StepsDaemons *) scenario;
	g_ptr_array_unref(stps->processes);
	g_free(stps);
}
STEP_DEF(step_start_daemon) {
	StepsDaemons *stps = (StepsDaemons *) scenario;
	StepsDaemonProcess *dproc;

	gchar *cmdline = NULL;
	if (!jsonx_locate(args, 'a', 0, 's', &cmdline)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	dproc = dproc_new(cmdline);
	if (dproc == NULL) {
		STEP_FAIL("Failed while starting daemon");
		return;
	} else {
		g_ptr_array_add(stps->processes, dproc);
	}

	STEP_OK;
}
STEP_DEF(step_start_command) {
	gchar *cmdline = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &cmdline)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	if (proc_run(cmdline) != 0) {
		STEP_FAIL("Failed while running command");
		return;
	}

	STEP_OK;
}

static StepsDaemonProcess *dproc_new(const gchar *cmdline) {
	StepsDaemonProcess *dproc = g_malloc0(sizeof(StepsDaemonProcess));
	GSpawnFlags flags = G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH;
	gchar **argv = NULL;
	GError *error = NULL;

	if (!g_shell_parse_argv(cmdline, NULL, &argv, &error)) {
		g_warning("Parsing command line %s: %s", cmdline, error->message);
		dproc_destroy(dproc);
		g_error_free(error);
		g_strfreev(argv);
		return NULL;
	}

	if (!g_spawn_async(NULL, argv, NULL, flags, NULL, NULL,
		&dproc->pid, &error)) {
		g_warning("Error starting daemon %s: %s", cmdline, error->message);
		g_strfreev(argv);
		dproc_destroy(dproc);
		g_error_free(error);
		return NULL;
	}
	g_strfreev(argv);

	g_message("Started %s (pid: %d)", cmdline, dproc->pid);

	return dproc;
}
static void dproc_destroy(StepsDaemonProcess *dproc) {
	if (dproc == NULL)
		return;

	if (dproc->pid) {
		/* TODO: Make this glib-ish */
		g_message("Killing %d", dproc->pid);
		kill(dproc->pid, SIGTERM);
		g_spawn_close_pid(dproc->pid);

		g_message("Waiting for %d to stop", dproc->pid);
		if (!waitpid_timeout(dproc->pid, 60)) {
			g_message("%d didn't stop. Trying SIGKILL", dproc->pid);
			kill(dproc->pid, SIGKILL);
			if (!waitpid_timeout(dproc->pid, 60)) {
				g_message("%d didn't stop now either, exit(1)", dproc->pid);
				/* We have really waited for the process to exit
				 * but it doesn't want to. So, to not have
				 * daemons live between scenarios, we'll exit
				 * here instead.
				 */
				exit(1);
			}
		}
	}
	g_free(dproc);
}
static gboolean waitpid_timeout(int pid, unsigned int timeout_sec) {
	int rc = 0, status = 0, i = 0, err = 0;

	for (i = 0; i < timeout_sec; i++) {
		rc = waitpid(pid, &status, WNOHANG);
		if (rc == -1) {
			err = errno;
			g_error("waitpid returned error %s (%d)", strerror(err), err);
			return FALSE;
		} else if (rc > 0 && (WIFEXITED(status) || WIFSIGNALED(status))) {
			return TRUE;
		} else {
			sleep(1);
		}
	}

	return FALSE;
}
static gint proc_run(const gchar *cmdline) {
	gchar **argv = NULL;
	GError *error = NULL;
	GSpawnFlags flags = G_SPAWN_SEARCH_PATH;
	gint rc = 1;

	if (!g_shell_parse_argv(cmdline, NULL, &argv, &error)) {
		g_warning("Parsing command line %s: %s", cmdline, error->message);
		g_error_free(error);
		g_strfreev(argv);
		return 1;
	}

	if (!g_spawn_sync(NULL, argv, NULL, flags, NULL, NULL,
		NULL, NULL, &rc, &error)) {
		g_warning("Error running %s: %s", cmdline, error->message);
		g_error_free(error);
		g_strfreev(argv);
		return rc;
	}
	g_strfreev(argv);

	g_message("Ran %s with exit status %d", cmdline, rc);


	return rc;
}
