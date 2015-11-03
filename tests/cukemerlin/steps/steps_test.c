#include "steps_test.h"
#include <glib.h>
#include <stdlib.h>

static gpointer step_begin_scenario(void);
static void step_end_scenario(gpointer *scenario);
static gint step_fail(gpointer *scenario, JsonNode *args);
static gint step_success(gpointer *scenario, JsonNode *args);

CukeStepEnvironment steps_test = {
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
