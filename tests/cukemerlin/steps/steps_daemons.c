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
	GTree *files;
} StepsDaemons;

typedef struct StepsDaemonProcess_ {
	GPid pid;
} StepsDaemonProcess;

static StepsDaemonProcess *dproc_new(const gchar *cmdline, GTree *name_map);
static void dproc_destroy(StepsDaemonProcess *dproc);

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);
STEP_DEF(step_config_file);
STEP_DEF(step_start_daemon);

CukeStepEnvironment steps_daemons = {
	.tag = "daemons",
	.begin_scenario = step_begin_scenario,
	.end_scenario = step_end_scenario,

	.definitions = {
		{ "^I have config file (.*)$", step_config_file },
		{ "^I start daemon (.*)$", step_start_daemon },
		{ NULL, NULL }
	}
};

STEP_BEGIN(step_begin_scenario) {
	StepsDaemons *stps = g_malloc0(sizeof(StepsDaemons));
	stps->processes = g_ptr_array_new_with_free_func(
		(GDestroyNotify) dproc_destroy);
	stps->files = g_tree_new_full((GCompareDataFunc) g_strcmp0, NULL, g_free,
		g_free);
	return stps;
}
STEP_END(step_end_scenario) {
	StepsDaemons *stps = (StepsDaemons *) scenario;
	g_tree_destroy(stps->files);
	g_ptr_array_unref(stps->processes);
	g_free(stps);
}
STEP_DEF(step_config_file) {
	StepsDaemons *stps = (StepsDaemons *) scenario;

	gchar *fileref = NULL;
	gchar *content = NULL;
	gchar *filename = NULL;
	GError *error = NULL;
	gint fd = 0;

	if (!jsonx_locate(args, 'a', 0, 's', &fileref)
		|| !jsonx_locate(args, 'a', 1, 's', &content)) {
		return 0;
	}

	fd = g_file_open_tmp(NULL, &filename, &error);
	if (fd < 0) {
		g_warning("Error creating tmp file %s: %s", fileref, error->message);
		g_error_free(error);
		return 0;
	}
	write(fd, content, strlen(content));
	close(fd);

	g_message("Created file %s: %s", fileref, filename);
	g_tree_insert(stps->files, g_strdup(fileref), filename);
	return 1;
}
STEP_DEF(step_start_daemon) {
	StepsDaemons *stps = (StepsDaemons *) scenario;
	StepsDaemonProcess *dproc;

	gchar *cmdline = NULL;
	if (!jsonx_locate(args, 'a', 0, 's', &cmdline)) {
		return 0;
	}
	dproc = dproc_new(cmdline, stps->files);
	g_ptr_array_add(stps->processes, dproc);

	return 1;
}

static StepsDaemonProcess *dproc_new(const gchar *cmdline, GTree *name_map) {
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

	/* Replace filenames, if those are defined in config file steps above */
	for (i = 0; argv[i] != NULL; i++) {
		gchar *replacement = g_tree_lookup(name_map, argv[i]);
		if (replacement) {
			g_free(argv[i]);
			argv[i] = g_strdup(replacement);
		}
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
