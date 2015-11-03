#include "steps_test.h"
#include <glib.h>
#include <stdlib.h>

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);
STEP_DEF(step_fail);
STEP_DEF(step_success);

CukeStepEnvironment steps_test = {
		.tag = "test",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.definitions = {
				{"^I fail$", step_fail},
				{"^I succeed$", step_success},
				{"^I do (.*) stuff$", step_success},
				{"^I do (.*) stuff (.*)$", step_success},
				{NULL, NULL}
		}
};


STEP_BEGIN(step_begin_scenario) {
	glong *buf = g_malloc(sizeof(glong));
	*buf = 0;
	g_message("Scenario started");
	return buf;
}

STEP_END(step_end_scenario) {
	glong *buf = (glong*)scenario;
	g_message("Scenario ended, %d steps", *buf);
	g_free(buf);
}

STEP_DEF(step_fail) {
	glong *buf = (glong*)scenario;
	if(args) {
		char *jsonbuf = json_encode(args);
		g_message("Got some data: %s", jsonbuf);
		free(jsonbuf);
	}
	(*buf)++;
	return 0;
}
STEP_DEF(step_success) {
	glong *buf = (glong*)scenario;
	if(args) {
		char *jsonbuf = json_encode(args);
		g_message("Got some data: %s", jsonbuf);
		free(jsonbuf);
	}
	(*buf)++;
	return 1;
}
