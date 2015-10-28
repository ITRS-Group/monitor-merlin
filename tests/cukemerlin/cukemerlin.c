#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "jsonsocket.h"

static gchar *opt_bind_address = "0.0.0.0";
static gint opt_bind_port = 98989;

static GMainLoop *mainloop = NULL;

static void stop_mainloop(int signal);

static gpointer net_conn_new(GSocket *conn, gpointer userdata);
static gboolean net_conn_data(GSocket *conn, JsonNode *node, gpointer userdata);
static void net_conn_close(gpointer userdata);

static GOptionEntry opt_entries[] = {
		{ "bind-address", 'a', 0, G_OPTION_ARG_STRING, &opt_bind_address,
				"Bind to this address", "addr" },
		{ "bind-port", 'p', 0, G_OPTION_ARG_INT, &opt_bind_port,
				"Listen to this port", "port" }, { NULL } };

int main(int argc, char *argv[]) {
	GOptionContext *optctx;
	GError *error = NULL;
	JSONSocket *js = NULL;

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

	js = jsonsocket_new(opt_bind_address, opt_bind_port, net_conn_new,
			net_conn_data, net_conn_close, NULL);
	g_return_val_if_fail(js != NULL, 1);

	g_message("Main Loop: Enter");
	g_main_loop_run(mainloop);
	g_message("Main Loop: Exit");

	jsonsocket_destroy(js);
	g_main_loop_unref(mainloop);

	return 0;
}

static void stop_mainloop(int signal) {
	g_main_loop_quit(mainloop);
}

static gpointer net_conn_new(GSocket *conn, gpointer userdata) {
	g_message("New connection");
	return userdata;
}
static gboolean net_conn_data(GSocket *conn, JsonNode *node, gpointer userdata) {
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
		JsonNode *argarray;
		JsonNode *argobject;
		/* We need to send some id and args */
		json_append_element(response, json_mkstring("success"));

		argobject = json_mkobject();
		json_append_member(argobject, "id", json_mkstring("1"));
		json_append_member(argobject, "args", json_mkarray());

		argarray = json_mkarray();
		json_append_element(argarray, argobject);

		json_append_element(response, argarray);
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
static void net_conn_close(gpointer userdata) {
	g_message("Disconnected");
}
