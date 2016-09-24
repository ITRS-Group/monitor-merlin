#include "steps_livestatus.h"
#include <glib.h>
#include <string.h>
#include <base/jsonx.h>

#include <merlincat/client_gsource.h>

#include <naemon/naemon.h>

typedef struct LivestatusScenario_ {
	/* Per request storage */
	ClientSource *cur_source;
	ConnectionStorage *cur_conn;
	CukeResponseRef respref;
	GString *strbuf;

	/* Match table storage */
	JsonNode *result;
} LivestatusScenario;

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);

STEP_DEF(step_ls_query);
STEP_DEF(step_ls_result);

static gpointer ls_conn_new(ConnectionStorage *conn, gpointer user_data);
static void ls_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data);
static void ls_conn_close(gpointer conn_user_data);

CukeStepEnvironment steps_livestatus =
	{
		.tag = "livestatus",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.definitions =
			{
				{ "^I submit the following livestatus query to (.*)$", step_ls_query },
				{ "^I should see the following livestatus response$", step_ls_result },
				{ NULL, NULL }
			}
	};

STEP_BEGIN(step_begin_scenario) {
	LivestatusScenario *lss = g_malloc0(sizeof(LivestatusScenario));
	return lss;
}

STEP_END(step_end_scenario) {
	LivestatusScenario *lss = (LivestatusScenario*) scenario;
	client_source_destroy(lss->cur_source);
	json_delete(lss->result);
	g_free(lss);
}

STEP_DEF(step_ls_query) {
	LivestatusScenario *lss = (LivestatusScenario*) scenario;

	char *ls_path;
	JsonNode *querynode, *curnode;
	ConnectionInfo conn_info;

	/* Step take two arguments. 1: path from regex, 2: table representing query lines */
	if (!jsonx_locate(args, 'a', 0, 's', &ls_path) ||
		!jsonx_locate(args, 'a', 1, 'j', &querynode)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	if (querynode->tag != JSON_ARRAY) {
		STEP_FAIL("Not a table argument");
		return;
	}

	/* Internal referense for STEP_* */
	lss->respref = respref;

	/* Clean up last query, if running */
	client_source_destroy(lss->cur_source);
	lss->cur_source = NULL;
	json_delete(lss->result);
	lss->result = NULL;

	/* Set up connection to socket */
	conn_info.listen = 0;
	conn_info.type = UNIX;
	conn_info.dest_addr = ls_path;
	conn_info.dest_port = 0;
	conn_info.source_addr = "";
	conn_info.source_port = 0;

	/* Open connection, replace old if necessary */
	lss->cur_source = client_source_new(&conn_info, ls_conn_new, ls_conn_data,
		ls_conn_close, lss);
	if (!lss->cur_source) {
		STEP_FAIL("Can't connect to livestatus");
		return;
	}
	if (!lss->cur_conn) {
		STEP_FAIL("Can't get connection to livestatus");
		return;
	}

	/* Send query. Don't care about fragmentation, send it as chunks */
	json_foreach(curnode, querynode) {
		const char *line;
		/* Well... this shouldn't happen... Ignore invalid lines, test will fail later */
		/* so fetch first column as string */
		if (jsonx_locate(curnode, 'a', 0, 's', &line)) {
			connection_send(lss->cur_conn, line, strlen(line));
			connection_send(lss->cur_conn, "\n", 1);
		}
	}
	/*
	 *  Output format json matches really well with cucumber table format of:
	 * | title1 | title2 |
	 * | value  | value  |
	 * | value  | value  |
	 */
	connection_send(lss->cur_conn, "ColumnHeaders: on\n", 18); 
	connection_send(lss->cur_conn, "OutputFormat: json\n\n", 28);
}

static gpointer ls_conn_new(ConnectionStorage *conn, gpointer user_data) {
	LivestatusScenario *lss = (LivestatusScenario*) user_data;
	lss->cur_conn = conn;
	lss->strbuf = g_string_new(NULL);
	return lss;
}
static void ls_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data) {
	LivestatusScenario *lss = (LivestatusScenario*) conn_user_data;
	gchar *tmpstr;
	tmpstr = g_strndup(buffer, length);
	g_string_append(lss->strbuf, tmpstr);
	g_free(tmpstr);
}
static void ls_conn_close(gpointer conn_user_data) {
	LivestatusScenario *lss = (LivestatusScenario*) conn_user_data;
	CukeResponseRef respref = lss->respref; /* required for STEP_OK/FAIL */

	gchar *response = NULL;

	lss->cur_conn = NULL;

	/* Extract and split string buffer */
	response = g_string_free(lss->strbuf, FALSE);
	lss->strbuf = NULL;
	if(response != NULL) {
		lss->result = json_decode(response);
	}

	/* Send result */
	if(lss->result) {
		STEP_OK;
	} else {
		STEP_FAIL("Invalid response from livestatus");
	}

	/* cleanup local storage */
	g_free(response);
}




STEP_DEF(step_ls_result) {
	LivestatusScenario *lss = (LivestatusScenario*) scenario;
	JsonNode *resultnode;
	JsonNode *str_result, *str_expect;
	int diff;

	/* Only one argument: a match table */
	if (!jsonx_locate(args, 'a', 0, 'j', &resultnode)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	if (resultnode->tag != JSON_ARRAY) {
		STEP_FAIL("Not a table argument");
		return;
	}
	if (lss->result == NULL) {
		STEP_FAIL("No result recorded, have you sent a query?");
	}

	str_expect = jsonx_stringobj(resultnode);
	str_result = jsonx_stringobj(lss->result);

	STEP_DIFF(str_expect, str_result);

	json_delete(str_expect);
	json_delete(str_result);
}
