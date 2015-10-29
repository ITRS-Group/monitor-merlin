#include <glib.h>
#include <stdlib.h>

#include "cukesocket.h"
#include "jsonsocket.h"
#include "jsonx.h"

struct CukeSocket_ {
	JSONSocket *js;

	GPtrArray *stepenvs;
};

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata);
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata);
static void cukesock_cb_close(gpointer userdata);

CukeSocket *cukesock_new(const gchar *bind_addr, const gint bind_port) {
	CukeSocket *cs = g_malloc(sizeof(CukeSocket));
	cs->js = jsonsocket_new(bind_addr, bind_port, cukesock_cb_new,
			cukesock_cb_data, cukesock_cb_close, cs);

	cs->stepenvs = g_ptr_array_new();
	return cs;
}
void cukesock_destroy(CukeSocket *cs) {
	if (cs == NULL)
		return;
	g_ptr_array_unref(cs->stepenvs);
	jsonsocket_destroy(cs->js);
	g_free(cs);
}

void cukesock_register_stepenv(CukeSocket *cs, CukeStepEnvironment *stepenv) {
	g_return_if_fail(cs != NULL);
	g_return_if_fail(stepenv != NULL);
	g_ptr_array_add(cs->stepenvs, stepenv);
}

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata) {
	g_message("New connection");
	return userdata;
}
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata) {
	CukeSocket *cs = (CukeSocket *)userdata;
	const char *cmd = NULL;
	JsonNode *response = json_mkarray();

	if (!jsonx_locate(node, 'a', 0, 's', &cmd)) {
		json_append_element(response, json_mkstring("fail"));
		goto do_send;
	}

	/*
	 * Find a step, given a name to match, which will be returned as an id tag,
	 * generated out of which module, and which step id, that matches
	 */
	if (0 == strcmp(cmd, "step_matches")) {
		int cur_stepenv;
		int cur_stepdef;
		CukeStepEnvironment *curenv;
		CukeStepDefinition *curdef;

		const char *name_to_match;
		if(!jsonx_locate(node, 'a', 1, 'o', "name_to_match", 's', &name_to_match)) {
			json_append_element(response, json_mkstring("fail"));
			goto do_send;
		}

		/* Traverse step environments */
		for(cur_stepenv=0;cur_stepenv<cs->stepenvs->len;cur_stepenv++) {
			curenv = cs->stepenvs->pdata[cur_stepenv];

			/* Traverse step definitions within the step enviornments */
			for(cur_stepdef=0;cur_stepdef<curenv->num_defs;cur_stepdef++) {
				curdef = &curenv->definitions[cur_stepdef];

				/* If we match, send a matching id tag */
				if(0==strcmp(name_to_match, curdef->match)) {
					json_append_element(response, json_mkstring("success"));
					json_append_element(response,
						jsonx_packarray(
							jsonx_packobject(
								"id", jsonx_packarray(
										json_mkstring(curenv->tag),
										json_mknumber(cur_stepdef),
										NULL
										),
								"args", json_mkarray(),
								"source", json_mkstring(curenv->tag),
								NULL, NULL
								),
							NULL
						)
					);
					goto do_send;
				}
			}
		}

		/* No step found, should be success, but without id tag */
		json_append_element(response, json_mkstring("success"));
		json_append_element(response, json_mkarray());
		goto do_send;
	}

	/*
	 * Invoke a step, identified by and id-tag, located above
	 */
	if (0 == strcmp(cmd, "invoke")) {
		const char *tag = NULL;
		long idx = 0;

		int cur_stepenv;
		CukeStepEnvironment *stepenv = NULL;
		CukeStepDefinition *stepdef;

		JsonNode *stepargs = NULL;


		if(!jsonx_locate(node, 'a', 1, 'o', "id", 'a', 0, 's', &tag) ||
			!jsonx_locate(node, 'a', 1, 'o', "id", 'a', 1, 'l', &idx)) {
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response, jsonx_packobject(
					"message", json_mkstring("Malformed id tag"),
					"exception", json_mkstring("CukeMerlin-internal"),
					NULL, NULL
			));
			goto do_send;
		}

		/* Locate the step environment, given tag */
		for(cur_stepenv=0;cur_stepenv<cs->stepenvs->len;cur_stepenv++) {
			stepenv = cs->stepenvs->pdata[cur_stepenv];
			if(0==strcmp(stepenv->tag, tag)) {
				break;
			}
			stepenv = NULL;
		}
		if(stepenv == NULL || idx < 0 || idx >= stepenv->num_defs) {
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response, jsonx_packobject(
					"message", json_mkstring("Unknown step definition id"),
					"exception", json_mkstring("CukeMerlin-internal"),
					NULL, NULL
			));
			goto do_send;
		}

		/* Locate correct step definition within the stepenv */
		stepdef = &stepenv->definitions[idx]; /* idx bounds verified above */

		/* Locate step arguments, if availble, otherwise NULL */
		if(!jsonx_locate(node, 'a', 1, 'o', "args", 'j', &stepargs)) {
			stepargs = NULL;
		}

		/* Call the handler */
		if((*stepdef->handler)(stepargs)) {
			/* Step succeeds */
			json_append_element(response, json_mkstring("success"));
			goto do_send;
		} else {
			/* Step fails */
			json_append_element(response, json_mkstring("fail"));
			json_append_element(response, jsonx_packobject(
					"message", json_mkstring("Step error"),
					"exception", json_mkstring("CukeMerlin-execution"),
					NULL, NULL
			));
			goto do_send;
		}
	}

	json_append_element(response, json_mkstring("success"));

	do_send: /**/

	{
		char *req_str = json_encode(node);
		char *resp_str = json_encode(response);

		g_message("Request: %s", req_str);
		g_message("Response: %s", resp_str);

		free(req_str);
		free(resp_str);
	}

	jsonsocket_send(conn, response);
	json_delete(response);

	return TRUE;
}
static void cukesock_cb_close(gpointer userdata) {
	g_message("Disconnected");
}
