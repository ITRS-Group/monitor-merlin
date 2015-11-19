#include "steps_daemons.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <base/jsonx.h>

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

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);
STEP_DEF(step_start_daemon);

CukeStepEnvironment steps_daemons = {
	.tag = "daemons",
	.begin_scenario = step_begin_scenario,
	.end_scenario = step_end_scenario,

	.definitions = {
		{ "^I start daemon (.*)$", step_start_daemon },
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
	g_ptr_array_add(stps->processes, dproc);

	STEP_OK;
}

static StepsDaemonProcess *dproc_new(const gchar *cmdline) {
	StepsDaemonProcess *dproc = g_malloc0(sizeof(StepsDaemonProcess));
	gchar **argv = NULL;
	GError *error = NULL;
	gint i;

	if (!g_shell_parse_argv(cmdline, NULL, &argv, &error)) {
		g_warning("Parsing command line %s: %s", cmdline, error->message);
		dproc_destroy(dproc);
		g_error_free(error);
		g_strfreev(argv);
		return NULL;
	}

	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
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
	}
	g_free(dproc);
}
