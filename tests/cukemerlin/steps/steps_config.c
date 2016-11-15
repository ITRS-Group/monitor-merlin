#include "steps_test.h"
#include <glib.h>
#include <glib/gstdio.h>
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
STEP_DEF(step_file_perm);
STEP_DEF(step_dir);
STEP_DEF(step_file_empty);

/* matches and not matches is similar, create a wrapper for those */
static glong file_match_step(gpointer *scenario, const gchar *filename, const gchar *pattern, CukeResponseRef respref);
STEP_DEF(step_file_matches);
STEP_DEF(step_file_not_matches);
STEP_DEF(step_file_matches_count);

CukeStepEnvironment steps_config = {
	.tag = "config",
	.begin_scenario = step_begin_scenario,
	.end_scenario = step_end_scenario,

	.definitions = {
		{ "^I have config file ([^ ]*) with permission ([0-7]+)$", step_file_perm },
		{ "^I have config file ([^ ]*)$", step_file },
		{ "^I have config dir (.*)$", step_dir },
		{ "^I have an empty file (.*)", step_file_empty },
		{ "^file (.*) matches (.*)$", step_file_matches },
		{ "^file (.*) does not match (.*)$", step_file_not_matches },
		{ "^file (.*) has ([0-9]+) lines? matching (.*)$", step_file_matches_count },
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
		g_message("Switched back to: %s from %s", sc->last_dir, sc->current_dir);
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

STEP_DEF(step_file_perm) {
	StepsConfig *sc = (StepsConfig *) scenario;

	gchar *filename = NULL;
	gchar *content = NULL;
	gchar *perm_str = NULL;
	GError *error = NULL;
	gint fd = 0;
	int perm = 0;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 's', &perm_str)
		|| !jsonx_locate(args, 'a', 2, 's', &content)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	perm = (int)g_ascii_strtoull(perm_str, NULL, 8);

	if (!g_file_set_contents(filename, content, strlen(content), &error)) {
		STEP_FAIL("Can't write to config file");
		g_error_free(error);
		return;
	}

	if (g_chmod(filename, perm) != 0) {
		STEP_FAIL("Can't set file permission");
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


static glong file_match_step(gpointer *scenario, const gchar *filename, const gchar *pattern, CukeResponseRef respref) {
	StepsConfig *sc = (StepsConfig *) scenario;

	GIOChannel *fp = NULL;
	GError *error = NULL;
	GIOStatus status;
	gchar *line = NULL;
	gsize terminator = 0;

	glong matching_count = 0;
	GRegex *re = NULL;


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
	while((status = g_io_channel_read_line(fp, &line, NULL, &terminator, &error)) == G_IO_STATUS_NORMAL) {
		g_assert(error == NULL); /* Shouldn't be an error if status is normal */
		g_assert(line != NULL); /* Line should be set if no error */
		line[terminator] = '\0'; /* We don't need no linebreak */
		if(g_regex_match(re, line, 0, NULL))
			matching_count++;
		g_free(line);
	}
	if(error) {
		STEP_FAIL("Error reading file");
		goto cleanup;
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

	return matching_count;
}

STEP_DEF(step_file_matches) {
	gchar *filename = NULL;
	gchar *pattern = NULL;
	glong matching_lines;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 's', &pattern)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	matching_lines = file_match_step(scenario, filename, pattern, respref);
	if(matching_lines > 0) {
		STEP_OK;
	} else {
		STEP_FAIL("No matching line");
	}
}
STEP_DEF(step_file_not_matches) {
	gchar *filename = NULL;
	gchar *pattern = NULL;
	glong matching_lines;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 's', &pattern)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	matching_lines = file_match_step(scenario, filename, pattern, respref);
	if(matching_lines > 0) {
		STEP_FAIL("Line matches");
	} else {
		STEP_OK;
	}
}
STEP_DEF(step_file_matches_count) {
	gchar *filename = NULL;
	gchar *pattern = NULL;
	glong expected_count = -1;
	glong matching_lines;

	if (!jsonx_locate(args, 'a', 0, 's', &filename)
		|| !jsonx_locate(args, 'a', 1, 'l', &expected_count)
		|| !jsonx_locate(args, 'a', 2, 's', &pattern)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	matching_lines = file_match_step(scenario, filename, pattern, respref);
	if(matching_lines != expected_count) {
		gchar *error_line = g_strdup_printf("Matched %d lines, expected %d", matching_lines, expected_count);
		STEP_FAIL(error_line);
		g_free(error_line);
	} else {
		STEP_OK;
	}
}
