#include <glib.h>
#include <stdlib.h>

#include "cukesocket.h"
#include "jsonsocket.h"
#include "jsonx.h"

struct CukeSocket_ {
	JSONSocket *js;
};

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata);
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata);
static void cukesock_cb_close(gpointer userdata);

CukeSocket *cukesock_new(const gchar *bind_addr, const gint bind_port) {
	CukeSocket *cs = g_malloc(sizeof(CukeSocket));
	cs->js = jsonsocket_new(bind_addr, bind_port, cukesock_cb_new,
			cukesock_cb_data, cukesock_cb_close, cs);
	return cs;
}
void cukesock_destroy(CukeSocket *cs) {
	if (cs == NULL)
		return;
	jsonsocket_destroy(cs->js);
	g_free(cs);
}

static gpointer cukesock_cb_new(GSocket *conn, gpointer userdata) {
	g_message("New connection");
	return userdata;
}
static gboolean cukesock_cb_data(GSocket *conn, JsonNode *node,
		gpointer userdata) {
	JsonNode *cmdnode = json_first_child(node);
	JsonNode *response = json_mkarray();

	if (cmdnode == NULL) {
		json_append_element(response, json_mkstring("fail"));
		goto do_send;
	}
	if (cmdnode->tag != JSON_STRING) {
		json_append_element(response, json_mkstring("fail"));
		goto do_send;
	}
	if (0 == strcmp(cmdnode->string_, "step_matches")) {
		/* We need to send some id and args */
		json_append_element(response, json_mkstring("success"));

		json_append_element(response,
			jsonx_packarray(
				jsonx_packobject(
					"id", json_mkstring("1"),
					"args", json_mkarray(),
					NULL, NULL
					),
				NULL
			)
		);

	} else {
		json_append_element(response, json_mkstring("success"));
	}

	do_send: /**/

	{
		char *req_str = json_stringify(node, "  ");
		char *resp_str = json_stringify(response, "  ");

		g_message("Request:\n%s", req_str);
		g_message("Response:\n%s", resp_str);

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
