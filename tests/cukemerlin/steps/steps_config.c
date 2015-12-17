#include "steps_test.h"
#include <glib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

/* For umask */
#include <sys/types.h>
#include <sys/stat.h>

typedef struct StepsConfig_ {
	gchar *last_dir;
	gchar *current_dir;

	/*
	 * Since naemon can not not drop privileges, we need to temporary change
	 * umask, so naemon have permission to connect.
	 */
	mode_t umask_store;
} StepsConfig;

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);
STEP_DEF(step_file);
STEP_DEF(step_dir);
STEP_DEF(step_file_empty);
STEP_DEF(step_file_matches);

CukeStepEnvironment steps_config = {
	.tag = "config",
	.begin_scenario = step_begin_scenario,
	.end_scenario = step_end_scenario,

	.definitions = {
		{ "^I have config file (.*)$", step_file },
		{ "^I have config dir (.*)$", step_dir },
		{ "^I have an empty file (.*)", step_file_empty },
		{ "^file (.*) matches (.*)$", step_file_matches },
		{ NULL, NULL }
	}
};

STEP_BEGIN(step_begin_scenario) {
	StepsConfig *sc = g_malloc0(sizeof(StepsConfig));
	GError *error = NULL;
	// TODO: Make this not use tmpnam, and more glib
	sc->current_dir = g_strdup(tmpnam(NULL));

	/*
	 * We need to increase permission to 777 (umask=0), since naemon doesn't
	 * know how not to drop privileges, and thus doesn't have permission to
	 * access our configuration directories in the test environment otherwise.
	 */
	sc->umask_store = umask(0);

	/* let umask handle the permissions, thus 0777 is always correct here */
	if(g_mkdir_with_parents(sc->current_dir, 0777)) {
		g_warning("Error creating directory %s; %s", sc->current_dir, strerror(errno));
	}
	if (sc->current_dir == NULL) {
		g_warning("Can't create temporary directory: %s", error->message);
		g_error_free(error);
	} else {
		sc->last_dir = g_malloc0(PATH_MAX);
		getcwd(sc->last_dir, PATH_MAX);
		chdir(sc->current_dir);
		g_message("Switched to: %s", sc->current_dir);
	}
	return sc;
}

STEP_END(step_end_scenario) {
	StepsConfig *sc = (StepsConfig*) scenario;
	if (sc->last_dir) {
		chdir(sc->last_dir);
		g_message("Switched back to: %s", sc->last_dir);
		g_free(sc->last_dir);
	}
	umask(sc->umask_store);
	g_free(sc->current_dir);
	g_free(sc);
}

STEP_DEF(step_file) {
	StepsConfig *sc = (StepsConfig *) scenario;

	gchar *filename = NULL;
	gchar *content = NULL;
	GError *error = NULL;
	gint fd = 0;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 's', &content)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	if (!g_file_set_contents(filename, content, strlen(content), &error)) {
		STEP_FAIL("Can't write to config file");
		g_error_free(error);
		return;
	}

	STEP_OK;
}

STEP_DEF(step_dir) {
	StepsConfig *sc = (StepsConfig *) scenario;

	gchar *dirname = NULL;
	GError *error = NULL;
	gint fd = 0;

	if (!jsonx_locate(args, 'a', 0, 's', &dirname)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	/* let umask handle the permissions, thus 0777 is always correct here */
	if (0 != g_mkdir(dirname, 0777)) {
		STEP_FAIL("Can't create to config dir");
		return;
	}

	STEP_OK;
}

STEP_DEF(step_file_empty) {
	StepsConfig *sc = (StepsConfig *) scenario;

	gchar *filename = NULL;
	GError *error = NULL;
	gint fd = 0;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	if (!g_file_set_contents(filename, "", 0, &error)) {
		STEP_FAIL("Can't clear file");
		g_error_free(error);
		return;
	}

	STEP_OK;
}

STEP_DEF(step_file_matches) {
	StepsConfig *sc = (StepsConfig *) scenario;

	gchar *filename = NULL;
	gchar *pattern = NULL;

	GIOChannel *fp = NULL;
	GError *error = NULL;
	GIOStatus status;
	gchar *line = NULL;
	gsize terminator = 0;

	gboolean match = FALSE;
	GRegex *re = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 's', &pattern)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	re = g_regex_new(pattern, 0, 0, &error);
	if(error != NULL) {
		STEP_FAIL("Can't compile regex");
		goto cleanup;
	}

	fp = g_io_channel_new_file(filename, "r", &error);
	if(fp == NULL) {
		STEP_FAIL("Can't open file");
		goto cleanup;
	}
	while(!match && (status = g_io_channel_read_line(fp, &line, NULL, &terminator, &error)) == G_IO_STATUS_NORMAL) {
		g_assert(error == NULL); /* Shouldn't be an error if status is normal */
		g_assert(line != NULL); /* Line should be set if no error */
		line[terminator] = '\0'; /* We don't need no linebreak */
		if(g_regex_match(re, line, 0, NULL))
			match = TRUE;
		g_free(line);
	}
	if(error) {
		STEP_FAIL("Error reading file");
		goto cleanup;
	}

	if(match) {
		STEP_OK;
	} else {
		STEP_FAIL("No matching line");
	}

	cleanup: /**/
	if(fp) {
		g_io_channel_shutdown(fp, TRUE, &error);
		g_io_channel_unref(fp);
	}
	if(re)
		g_regex_unref(re);
	if(error)
		g_error_free(error);
}
