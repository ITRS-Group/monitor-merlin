#include "steps_merlin.h"
#include <glib.h>
#include <stdlib.h>

/* merlincat headers */
#include <merlincat/client_gsource.h>
#include <merlincat/event_packer.h>
#include <merlincat/merlinreader.h>

/* merlin headers */
#include <shared/shared.h>
#include <shared/compat.h>

/* naemon / libnaemon headers */
#include <naemon/naemon.h>

typedef struct MerlinScenario_ {
	GTree *connections;
} MerlinScenario;

typedef struct MerlinScenarioConnection_ {
	ClientSource *cs;
	MerlinReader *mr;
	ConnectionStorage *conn;
} MerlinScenarioConnection;

static MerlinScenarioConnection *mrlscenconn_new(ConnectionInfo *conn_info);
static void mrlscenconn_destroy(MerlinScenarioConnection *msc);

static gpointer net_conn_new(ConnectionStorage *conn, gpointer user_data);
static void net_conn_data(ConnectionStorage *conn, gpointer buffer, gsize length, gpointer conn_user_data);
static void net_conn_close(gpointer conn_user_data);

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);

STEP_DEF(step_connect_tcp);
STEP_DEF(step_disconnect);
STEP_DEF(step_send_event);

CukeStepEnvironment steps_merlin = {
		.tag = "merlin",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.definitions = {
				{"^([a-z0-9-_]+) connect to merlin at port ([0-9]+)$", step_connect_tcp},
				{"^([a-z0-9-_]+) disconnects from merlin$", step_disconnect},
				{"^([a-z0-9-_]+) sends event ([A-Z_]+)$", step_send_event},
				{NULL, NULL}
		}
};

STEP_BEGIN(step_begin_scenario) {
	MerlinScenario *ms = g_malloc(sizeof(MerlinScenario));
	/* Create a storage for all connections */
	ms->connections = g_tree_new_full((GCompareDataFunc) g_strcmp0, NULL,
			g_free, (GDestroyNotify) mrlscenconn_destroy);
	return ms;
}

STEP_END(step_end_scenario) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	/* Close all connections, and free storage */
	g_tree_destroy(ms->connections);
	g_free(ms);
}

STEP_DEF(step_connect_tcp) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	long port = 0;
	MerlinScenarioConnection *msc;
	ConnectionInfo conn_info;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
			|| !jsonx_locate(args, 'a', 1, 'l', &port)) {
		return 0;
	}

	conn_info.listen = 0;
	conn_info.dest_addr = "127.0.0.1";
	conn_info.dest_port = port;
	conn_info.source_addr = "0.0.0.0";
	conn_info.source_port = 31337;
	conn_info.type = TCP;
	msc = mrlscenconn_new(&conn_info);
	if (msc == NULL)
		return 0;

	g_tree_insert(ms->connections, g_strdup(conntag), msc);

	return 1;
}

STEP_DEF(step_disconnect) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)) {
		return 0;
	}

	/* This frees up the connection and everything */
	if(g_tree_remove(ms->connections, conntag)) {
		/* Succeed only if the connection was found */
		return 1;
	}
	return 0;
}

STEP_DEF(step_send_event) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	MerlinScenarioConnection *msc;
	const char *conntag = NULL;
	const char *typetag = NULL;
	JsonNode *tbl = NULL;
	JsonNode *row = NULL;
	merlin_event *evt = NULL;

	struct kvvec *kvv = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag) ||
			!jsonx_locate(args, 'a', 1, 's', &typetag) ||
			!jsonx_locate(args, 'a', 2, 'j', &tbl)) {
		return 0;
	}
	if (tbl->tag != JSON_ARRAY) {
		return 0;
	}

	msc = g_tree_lookup(ms->connections, conntag);
	if(msc == NULL) {
		/* If conntag isn't found, fail */
		return 0;
	}
	if(msc->conn == NULL) {
		/* If disconnected, fail */
		return 0;
	}

	kvv = kvvec_create(30);
	if (kvv == NULL) {
		/* If disconnected, fail */
		return 0;
	}

	json_foreach(row, tbl)
	{
		const char *key = NULL;
		const char *value = NULL;
		if (jsonx_locate(row, 'a', 0, 's', &key)
				&& jsonx_locate(row, 'a', 1, 's', &value)) {
			kvvec_addkv_str(kvv, strdup(key), strdup(value));
		}
	}

	evt = event_packer_unpack_kvv(typetag, kvv);
	if(!evt) {
		g_message("Failed to pack message");
		kvvec_destroy(kvv, KVVEC_FREE_ALL);
		return 0;
	}

	g_message("Sending packet of type %s", typetag);
	connection_send(msc->conn, evt, HDR_SIZE + evt->hdr.len);

	free(evt);
	kvvec_destroy(kvv, KVVEC_FREE_ALL);
	return 1;
}

/**
 * Create a TCP connection, and return a storage for that connection.
 *
 * It's ok to add handlers to the main context, to update the state during
 * runtime, as long as everything is freed during mrlscenconn_destroy
 */
static MerlinScenarioConnection *mrlscenconn_new(ConnectionInfo *conn_info) {
	MerlinScenarioConnection *msc = g_malloc(sizeof(MerlinScenarioConnection));

	msc->cs = client_source_new(conn_info, net_conn_new, net_conn_data, net_conn_close, msc);

	return msc;
}
static void mrlscenconn_destroy(MerlinScenarioConnection *msc) {
	if(msc == NULL)
		return;
	client_source_destroy(msc->cs);
	g_free(msc);
}


static gpointer net_conn_new(ConnectionStorage *conn, gpointer user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *)user_data;
	msc->mr = merlinreader_new();
	msc->conn = conn;
	return msc;
}
static void net_conn_data(ConnectionStorage *conn, gpointer buffer, gsize length, gpointer conn_user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *)conn_user_data;
	merlin_event *evt;
	gsize read_size;
	char *buf;
	while(length) {
		read_size = merlinreader_add_data(msc->mr, buffer, length);
		length -= read_size;
		buffer += read_size;

		while(NULL != (evt = merlinreader_get_event(msc->mr))) {
			buf = event_packer_pack(evt);
			printf("%s\n", buf);
			free(buf);
			g_free(evt);
		}
	}
}
static void net_conn_close(gpointer conn_user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *)conn_user_data;
	merlinreader_destroy(msc->mr);
	msc->mr = NULL;
	msc->conn = NULL;
}
