#include "steps_queryhandler.h"
#include <glib.h>
#include <string.h>
#include <base/jsonx.h>

#include <merlincat/client_gsource.h>

#include <naemon/naemon.h>

typedef struct QueryHandlerScenario_ {
	gchar *qh_path;

	/* Per request storage */
	ClientSource *cur_source;
	ConnectionStorage *cur_conn;
	CukeResponseRef respref;
	GString *strbuf;

	/* Match table storage */
	JsonNode *matches;
} QueryHandlerScenario;

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);

STEP_DEF(step_qh_path);
STEP_DEF(step_qh_match);

static gpointer qh_conn_new(ConnectionStorage *conn, gpointer user_data);
static void qh_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data);
static void qh_conn_close(gpointer conn_user_data);

CukeStepEnvironment steps_queryhandler =
	{
		.tag = "queryhandler",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.definitions =
			{
				{ "^I have query handler path (.*)$", step_qh_path },
				{ "^I ask query handler (.*)", step_qh_match },
				{ NULL, NULL }
			}
	};

STEP_BEGIN(step_begin_scenario) {
	QueryHandlerScenario *qhs = g_malloc0(sizeof(QueryHandlerScenario));
	return qhs;
}

STEP_END(step_end_scenario) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) scenario;
	client_source_destroy(qhs->cur_source);
	json_delete(qhs->matches);
	g_free(qhs->qh_path);
	g_free(qhs);
}

STEP_DEF(step_qh_path) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) scenario;

	const char *qh_path;
	if (!jsonx_locate(args, 'a', 0, 's', &qh_path)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	client_source_destroy(qhs->cur_source);
	g_free(qhs->qh_path);
	qhs->qh_path = g_strdup(qh_path);
	STEP_OK;
}

STEP_DEF(step_qh_match) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) scenario;
	const char *command;
	gchar *formatted_command;
	ConnectionInfo conn_info;
	JsonNode *tbl;

	if (!jsonx_locate(args, 'a', 0, 's', &command) ||
		!jsonx_locate(args, 'a', 1, 'j', &tbl)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	if (!qhs->qh_path) {
		STEP_FAIL("You must specify query handler path");
		return;
	}

	json_delete(qhs->matches);
	qhs->matches = jsonx_table_hashes(tbl);
	if(qhs->matches == NULL) {
		STEP_FAIL("Incorrect table format");
		return;
	}

	conn_info.listen = 0;
	conn_info.type = UNIX;
	conn_info.dest_addr = qhs->qh_path;
	conn_info.dest_port = 0;
	conn_info.source_addr = "";
	conn_info.source_port = 0;

	qhs->respref = respref;

	client_source_destroy(qhs->cur_source);
	qhs->cur_source = client_source_new(&conn_info, qh_conn_new, qh_conn_data,
		qh_conn_close, qhs);
	if (!qhs->cur_source) {
		STEP_FAIL("Cant connect to query handler");
		return;
	}
	if (!qhs->cur_conn) {
		STEP_FAIL("Cant get connection to to query handler");
		return;
	}

	formatted_command = g_strdup_printf("#%s", command);
	/* Include null-byte */
	connection_send(qhs->cur_conn, formatted_command,
		strlen(formatted_command) + 1);
	g_free(formatted_command);
}

static gpointer qh_conn_new(ConnectionStorage *conn, gpointer user_data) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) user_data;
	qhs->cur_conn = conn;
	qhs->strbuf = g_string_new(NULL);
	return qhs;
}
static void qh_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) conn_user_data;
	gchar *tmpstr;
	tmpstr = g_strndup(buffer, length);
	g_string_append(qhs->strbuf, tmpstr);
	g_free(tmpstr);
}
static void qh_conn_close(gpointer conn_user_data) {
	QueryHandlerScenario *qhs = (QueryHandlerScenario*) conn_user_data;
	CukeResponseRef respref = qhs->respref; /* required for STEP_OK/FAIL */

	gchar *response;
	gchar **resplines;
	JsonNode *curmatch;
	int i;

	qhs->cur_conn = NULL;

	/* Extract and split string buffer */
	response = g_string_free(qhs->strbuf, FALSE);
	qhs->strbuf = NULL;
	resplines = g_strsplit_set(response, "\n\r", -1);
	g_free(response);


	json_foreach(curmatch, qhs->matches) {
		const char *filter_var;
		const char *filter_val;
		const char *match_var;
		const char *match_val;
		int matched;
		gchar **curline;
		if(!jsonx_locate(curmatch, 'o', "filter_var", 's', &filter_var) ||
			!jsonx_locate(curmatch, 'o', "filter_val", 's', &filter_val) ||
			!jsonx_locate(curmatch, 'o', "match_var", 's', &match_var) ||
			!jsonx_locate(curmatch, 'o', "match_val", 's', &match_val)) {
			STEP_FAIL("Malformed match");
			goto cleanup;
		}

		matched = 0;
		for (curline = resplines; *curline; curline++) {
			struct kvvec *kvv;
			const char *kvv_filter_val;
			const char *kvv_match_val;

			/* Ignore empty lines, might be due to \n\r */
			if (**curline == '\0')
				continue;

			kvv = ekvstr_to_kvvec(*curline);
			/* If not parseable, fail out */
			if (kvv == NULL) {
				STEP_FAIL("Can not parse query handler response");
				kvvec_destroy(kvv, KVVEC_FREE_ALL);
				goto cleanup;
			}

			/* Match kvv against filter_* and match_* */
			kvv_filter_val = kvvec_fetch_str_str(kvv, filter_var);
			kvv_match_val = kvvec_fetch_str_str(kvv, match_var);
			if(kvv_filter_val != NULL && kvv_match_val) {
				/* Can only match if both filter and match vars exists */
				if (0 == strcmp(kvv_filter_val, filter_val) &&
					0 == strcmp(kvv_match_val, match_val)) {
					matched = 1;
				}
			}

			kvvec_destroy(kvv, KVVEC_FREE_ALL);
		}
		if(!matched) {
			STEP_FAIL("Doesn't match");
			goto cleanup;
		}
	}

	STEP_OK;

	cleanup: /**/
	g_strfreev(resplines);
}
