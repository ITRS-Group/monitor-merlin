#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "cukesocket.h"
#include "json.h"

static gchar *opt_bind_address = "0.0.0.0";
static gint opt_bind_port = 98989;

static GMainLoop *mainloop = NULL;


static gpointer step_begin_scenario(void);
static void step_end_scenario(gpointer *scenario);
static gint step_fail(gpointer *scenario, JsonNode *args);
static gint step_success(gpointer *scenario, JsonNode *args);

static CukeStepEnvironment testenv = {
		.tag = "test",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.num_defs = 4,
		.definitions = {
				{"^I fail$", step_fail},
				{"^I succeed$", step_success},
				{"^I do (.*) stuff$", step_success},
				{"^I do (.*) stuff (.*)$", step_success},
		}
};

static void stop_mainloop(int signal);

static GOptionEntry opt_entries[] = {
		{ "bind-address", 'a', 0, G_OPTION_ARG_STRING, &opt_bind_address,
				"Bind to this address", "addr" },
		{ "bind-port", 'p', 0, G_OPTION_ARG_INT, &opt_bind_port,
				"Listen to this port", "port" }, { NULL } };

int main(int argc, char *argv[]) {
	GOptionContext *optctx;
	GError *error = NULL;
	CukeSocket *cs = NULL;

	g_type_init();

	optctx = g_option_context_new("- Merlin protocol cucumber test daemon");
	g_option_context_add_main_entries(optctx, opt_entries, NULL);
	if (!g_option_context_parse(optctx, &argc, &argv, &error)) {
		g_print("%s\n", error->message);
		g_error_free(error);
		exit(1);
	}

	mainloop = g_main_loop_new(NULL, TRUE);
	/* non glib-unix version of stopping signals */
	signal(SIGHUP, stop_mainloop);
	signal(SIGINT, stop_mainloop);
	signal(SIGTERM, stop_mainloop);

	cs = cukesock_new(opt_bind_address, opt_bind_port);
	g_return_val_if_fail(cs != NULL, 1);

	cukesock_register_stepenv(cs, &testenv);

	g_message("Main Loop: Enter");
	g_main_loop_run(mainloop);
	g_message("Main Loop: Exit");

	cukesock_destroy(cs);
	g_main_loop_unref(mainloop);

	g_option_context_free(optctx);

	return 0;
}

static void stop_mainloop(int signal) {
	g_main_loop_quit(mainloop);
}

static gpointer step_begin_scenario(void) {
	glong *buf = g_malloc(sizeof(glong));
	*buf = 0;
	g_message("Scenario started");
	return buf;
}
static void step_end_scenario(gpointer *scenario) {
	glong *buf = (glong*)scenario;
	g_message("Scenario ended, %d steps", *buf);
	g_free(buf);
}

static gint step_fail(gpointer *scenario, JsonNode *args) {
	glong *buf = (glong*)scenario;
	if(args) {
		char *jsonbuf = json_encode(args);
		g_message("Got some data: %s", jsonbuf);
		free(jsonbuf);
	}
	(*buf)++;
	return 0;
}
static gint step_success(gpointer *scenario, JsonNode *args) {
	glong *buf = (glong*)scenario;
	if(args) {
		char *jsonbuf = json_encode(args);
		g_message("Got some data: %s", jsonbuf);
		free(jsonbuf);
	}
	(*buf)++;
	return 1;
}
