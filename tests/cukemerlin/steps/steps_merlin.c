#include "steps_merlin.h"
#include <glib.h>
#include <stdlib.h>
#include <base/jsonx.h>

/* merlincat headers */
#include <merlincat/client_gsource.h>
#include <merlincat/server_gsource.h>
#include <merlincat/event_packer.h>
#include <merlincat/merlinreader.h>

/* merlin headers */
#include <shared/shared.h>
#include <shared/compat.h>

/* naemon / libnaemon headers */
#include <naemon/naemon.h>

#define STEP_MERLIN_CONNECTION_TIMEOUT 10000
#define STEP_MERLIN_MESSAGE_TIMEOUT 3000

typedef struct MerlinScenario_ {
	GTree *connections;
} MerlinScenario;

typedef struct MerlinScenarioConnection_ {
	ClientSource *cs;
	ServerSource *ss;

	MerlinReader *mr;
	ConnectionStorage *conn;

	/*
	 * Buffer of merlin_events, clear from the beginning, initialized by step
	 * "X starts recording"
	 */
	GPtrArray *event_buffer;

	/*
	 * Identify which step is currently active. A step is only active is the
	 * step timer is active. If step timer isn't active, this variable content
	 * is undefined.
	 */
	enum {
		STEP_MERLIN_NONE = 0,
		STEP_MERLIN_IS_CONNECTED,
		STEP_MERLIN_IS_DISCONNECTED,
		STEP_MERLIN_EVENT_RECEIVED,
		STEP_MERLIN_EVENT_NOT_RECEIVED
	} current_step;

	/*
	 * The current matching filter, for event matching steps
	 */
	gint match_eventtype;
	struct kvvec *match_kv;

	/*
	 * Storage for step timer
	 */
	struct {
		CukeResponseRef respref;
		guint timer;
		gboolean success;
		const gchar *message;
	} steptimer;

	/*
	 * Tag for the connection, as named in the cucumber steps
	 */
	const char *tag;
} MerlinScenarioConnection;

static struct kvvec *jsontbl_to_kvvec(JsonNode *tbl);

static MerlinScenarioConnection *mrlscenconn_new(ConnectionInfo *conn_info, const char *tag);
static void mrlscenconn_destroy(MerlinScenarioConnection *msc);
static void mrlscenconn_clear_buffer(MerlinScenarioConnection *msc);

static void mrlscenconn_record_match_set(MerlinScenarioConnection *msc,
	const char *typestr, struct kvvec *matchkv);
static gboolean mrlscenconn_record_match(MerlinScenarioConnection *msc,
	merlin_event *evt);

static MerlinScenarioConnection *mrlscen_get_conn(MerlinScenario *ms,
	const gchar *tag);

static gpointer net_conn_new(ConnectionStorage *conn, gpointer user_data);
static void net_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data);
static void net_conn_close(gpointer conn_user_data);

STEP_BEGIN(step_begin_scenario);
STEP_END(step_end_scenario);

STEP_DEF(step_connect_tcp);
STEP_DEF(step_connect_unix);
STEP_DEF(step_listen_tcp);
STEP_DEF(step_listen_unix);
STEP_DEF(step_disconnect);

STEP_DEF(step_is_connected);
STEP_DEF(step_is_disconnected);

STEP_DEF(step_send_event);

STEP_DEF(step_clear_buffer);
STEP_DEF(step_record_check);
STEP_DEF(step_no_record_check);

static void steptimer_start(MerlinScenarioConnection *msc, CukeResponseRef respref, guint timeout, gboolean success, const gchar *message);
static void steptimer_stop(MerlinScenarioConnection *msc, gboolean success, const gchar *message);
static gboolean steptimer_timeout(gpointer userdata);

CukeStepEnvironment steps_merlin =
	{
		.tag = "merlin",
		.begin_scenario = step_begin_scenario,
		.end_scenario = step_end_scenario,

		.definitions =
			{
				/* Connection handling */
				{ "^([a-z0-9-_]+) connect to merlin at port ([0-9]+)$",
					step_connect_tcp },
				{
					"^([a-z0-9-_]+) connect to merlin at port ([0-9]+) from port ([0-9]+)$",
					step_connect_tcp },
				{
					"^([a-z0-9-_]+) connect to merlin at socket (.+)$",
					step_connect_unix },
				{
					"^([a-z0-9-_]+) listens for merlin at port ([0-9]+)$",
					step_listen_tcp },
				{
					"^([a-z0-9-_]+) listens for merlin at socket (.+)$",
					step_listen_unix },
				{ "^([a-z0-9-_]+) disconnects from merlin$", step_disconnect },

				/* Connection verification */
				{ "^([a-z0-9-_]+) is connected to merlin$", step_is_connected },
				{
					"^([a-z0-9-_]+) is not connected to merlin$",
					step_is_disconnected },

				/* Send events */
				{ "^([a-z0-9-_]+) sends raw event ([A-Z_]+)$", step_send_event },

				/* Receive events */
				{ "^([a-z0-9-_]+) clears buffer$", step_clear_buffer },
				{ "^([a-z0-9-_]+) received event ([A-Z_]+)$", step_record_check },
				{
					"^([a-z0-9-_]+) should not receive ([A-Z_]+)$",
					step_no_record_check },

				{ NULL, NULL }
			}
	};

STEP_BEGIN(step_begin_scenario) {
	MerlinScenario *ms = g_malloc0(sizeof(MerlinScenario));
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
	long dport = 0;
	long sport = 0;
	MerlinScenarioConnection *msc;
	ConnectionInfo conn_info;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 'l', &dport)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	if (!jsonx_locate(args, 'a', 2, 'l', &sport)) {
		/* This is valid, just not specified, take default  value */
		sport = 0;
	}

	conn_info.listen = 0;
	conn_info.type = TCP;
	conn_info.dest_addr = "127.0.0.1";
	conn_info.dest_port = dport;
	conn_info.source_addr = "0.0.0.0";
	conn_info.source_port = sport;
	msc = mrlscenconn_new(&conn_info, conntag);
	if (msc == NULL) {
		STEP_FAIL("Can not connect to merlin socket");
		return;
	}

	g_tree_insert(ms->connections, g_strdup(conntag), msc);

	STEP_OK;
}

STEP_DEF(step_connect_unix) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	const char *sockpath = NULL;
	MerlinScenarioConnection *msc;
	ConnectionInfo conn_info;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 's', &sockpath)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	conn_info.listen = 0;
	conn_info.type = UNIX;
	conn_info.dest_addr = g_strdup(sockpath);
	conn_info.dest_port = 0;
	conn_info.source_addr = "";
	conn_info.source_port = 0;

	msc = mrlscenconn_new(&conn_info, conntag);

	g_free(conn_info.dest_addr);
	if (msc == NULL) {
		STEP_FAIL("Can not connect to merlin socket");
		return;
	}

	g_tree_insert(ms->connections, g_strdup(conntag), msc);

	STEP_OK;
}

STEP_DEF(step_listen_tcp) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	long dport = 0;
	MerlinScenarioConnection *msc;
	ConnectionInfo conn_info;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 'l', &dport)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	conn_info.listen = 1;
	conn_info.type = TCP;
	conn_info.dest_addr = "127.0.0.1";
	conn_info.dest_port = dport;
	conn_info.source_addr = "0.0.0.0";
	conn_info.source_port = 0;
	msc = mrlscenconn_new(&conn_info, conntag);
	if (msc == NULL) {
		STEP_FAIL("Can not start listen to merlin socket");
		return;
	}

	g_tree_insert(ms->connections, g_strdup(conntag), msc);

	STEP_OK;
}

STEP_DEF(step_listen_unix) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	const char *sockpath = NULL;
	MerlinScenarioConnection *msc;
	ConnectionInfo conn_info;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 's', &sockpath)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	conn_info.listen = 1;
	conn_info.type = UNIX;
	conn_info.dest_addr = g_strdup(sockpath);
	conn_info.dest_port = 0;
	conn_info.source_addr = "";
	conn_info.source_port = 0;

	msc = mrlscenconn_new(&conn_info, conntag);

	g_free(conn_info.dest_addr);
	if (msc == NULL) {
		STEP_FAIL("Can not start listen to merlin socket");
		return;
	}

	g_tree_insert(ms->connections, g_strdup(conntag), msc);

	STEP_OK;
}

STEP_DEF(step_disconnect) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	/* This frees up the connection and everything */
	if (!g_tree_remove(ms->connections, conntag)) {
		/* Fail if the connection was not found */
		STEP_FAIL("No active connection");
		return;
	}

	STEP_OK;
}

STEP_DEF(step_is_connected) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	MerlinScenarioConnection *msc;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	msc = g_tree_lookup(ms->connections, conntag);
	if (msc == NULL) {
		/* If conntag isn't found, it's not connected */
		STEP_FAIL("Unknown connection reference");
		return;
	}
	if (msc->conn == NULL || !connection_is_connected(msc->conn)) {
		msc->current_step = STEP_MERLIN_IS_CONNECTED;
		steptimer_start(msc, respref, STEP_MERLIN_CONNECTION_TIMEOUT, FALSE, "Not connected");
		return;
	}
	STEP_OK;
}

STEP_DEF(step_is_disconnected) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	const char *conntag = NULL;
	MerlinScenarioConnection *msc;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	msc = g_tree_lookup(ms->connections, conntag);
	if (msc == NULL) {
		/* If conntag isn't found, it's not connected */
		STEP_OK;
		return;
	}
	if (msc->conn == NULL || !connection_is_connected(msc->conn)) {
		/* If connection isn't found, it's not connected */
		STEP_OK;
		return;
	}
	msc->current_step = STEP_MERLIN_IS_DISCONNECTED;
	steptimer_start(msc, respref, STEP_MERLIN_CONNECTION_TIMEOUT, FALSE, "Connected");
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

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 's', &typetag)
		|| !jsonx_locate(args, 'a', 2, 'j', &tbl)) {
		STEP_FAIL("Invalid arguments");
		return;
	}
	if (tbl->tag != JSON_ARRAY) {
		STEP_FAIL("Not a table argument");
		return;
	}

	msc = g_tree_lookup(ms->connections, conntag);
	if (msc == NULL) {
		/* If conntag isn't found, fail */
		STEP_FAIL("Unknown connection reference");
		return;
	}
	if (msc->conn == NULL) {
		/* If disconnected, fail */
		STEP_FAIL("Connection isn't found");
		return;
	}

	kvv = kvvec_create(30);
	if (kvv == NULL) {
		STEP_FAIL("Memory error, can't create kvvec");
		return;
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
	if (!evt) {
		STEP_FAIL("Failed to pack message");
		kvvec_destroy(kvv, KVVEC_FREE_ALL);
		return;
	}

	g_message("Sending packet of type %s", typetag);
	connection_send(msc->conn, evt, HDR_SIZE + evt->hdr.len);

	free(evt);
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	STEP_OK;
}

STEP_DEF(step_clear_buffer) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	MerlinScenarioConnection *msc;
	const char *conntag = NULL;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	msc = g_tree_lookup(ms->connections, conntag);
	if (msc == NULL) {
		/* If conntag isn't found, fail */
		STEP_FAIL("Unknown connection reference");
		return;
	}

	mrlscenconn_clear_buffer(msc);

	STEP_OK;
}

STEP_DEF(step_record_check) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	MerlinScenarioConnection *msc;
	const char *conntag = NULL;
	const char *typetag = NULL;
	JsonNode *tbl = NULL;
	gint i;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 's', &typetag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	/* It's ok not to have a table, just keep it to NULL */
	if (!jsonx_locate(args, 'a', 2, 'j', &tbl)) {
		tbl = NULL;
	}

	msc = mrlscen_get_conn(ms, conntag);
	if(msc == NULL) {
		STEP_FAIL("Unknown connection reference");
		return;
	}

	mrlscenconn_record_match_set(msc, typetag, jsontbl_to_kvvec(tbl));
	for(i=0;i<msc->event_buffer->len;i++) {
		if(mrlscenconn_record_match(msc, msc->event_buffer->pdata[i])) {
			STEP_OK;
			return;
		}
	}
	msc->current_step = STEP_MERLIN_EVENT_RECEIVED;
	steptimer_start(msc, respref, STEP_MERLIN_MESSAGE_TIMEOUT, FALSE, "No matching entries");
}

STEP_DEF(step_no_record_check) {
	MerlinScenario *ms = (MerlinScenario*) scenario;
	MerlinScenarioConnection *msc;
	const char *conntag = NULL;
	const char *typetag = NULL;
	JsonNode *tbl = NULL;
	gint i;

	if (!jsonx_locate(args, 'a', 0, 's', &conntag)
		|| !jsonx_locate(args, 'a', 1, 's', &typetag)) {
		STEP_FAIL("Invalid arguments");
		return;
	}

	/* It's ok not to have a table, just keep it to NULL */
	if (!jsonx_locate(args, 'a', 2, 'j', &tbl)) {
		tbl = NULL;
	}

	msc = mrlscen_get_conn(ms, conntag);
	if(msc == NULL) {
		STEP_FAIL("Unknown connection reference");
		return;
	}

	mrlscenconn_record_match_set(msc, typetag, jsontbl_to_kvvec(tbl));
	for(i=0;i<msc->event_buffer->len;i++) {
		if(mrlscenconn_record_match(msc, msc->event_buffer->pdata[i])) {
			STEP_FAIL("Message received");
			return;
		}
	}
	msc->current_step = STEP_MERLIN_EVENT_NOT_RECEIVED;
	steptimer_start(msc, respref, STEP_MERLIN_MESSAGE_TIMEOUT, TRUE, NULL);
}

/**
 * Build a kvvec out of a json table, if possible. Return empty kvvec otherwise
 */
static struct kvvec *jsontbl_to_kvvec(JsonNode *tbl) {
	struct kvvec *kvv;
	JsonNode *row = NULL;

	kvv = kvvec_create(30);
	if (kvv == NULL) {
		/* If disconnected, fail */
		return NULL;
	}

	if (tbl == NULL || tbl->tag != JSON_ARRAY) {
		return kvv;
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

	return kvv;
}

static MerlinScenarioConnection *mrlscen_get_conn(MerlinScenario *ms,
	const gchar *tag) {
	MerlinScenarioConnection *msc;
	msc = g_tree_lookup(ms->connections, tag);
	if (msc == NULL) {
		/* If conntag isn't found, fail */
		return NULL;
	}
	if (msc->conn == NULL) {
		/* If disconnected, fail */
		return NULL;
	}
	return msc;
}

/**
 * Create a TCP connection, and return a storage for that connection.
 *
 * It's ok to add handlers to the main context, to update the state during
 * runtime, as long as everything is freed during mrlscenconn_destroy
 */
static MerlinScenarioConnection *mrlscenconn_new(ConnectionInfo *conn_info, const char *tag) {
	MerlinScenarioConnection *msc = g_malloc0(sizeof(MerlinScenarioConnection));

	msc->event_buffer = g_ptr_array_new_with_free_func(g_free);
	msc->tag = g_strdup(tag);

	if(conn_info->listen) {
		msc->ss = server_source_new(conn_info, net_conn_new, net_conn_data,
			net_conn_close, msc);
		if(msc->ss == NULL)
			goto fail_out;
	} else {
		msc->cs = client_source_new(conn_info, net_conn_new, net_conn_data,
			net_conn_close, msc);
		if(msc->cs == NULL)
			goto fail_out;
	}

	return msc;

	fail_out: /**/
	mrlscenconn_destroy(msc);
	return NULL;
}
static void mrlscenconn_destroy(MerlinScenarioConnection *msc) {
	if (msc == NULL)
		return;
	steptimer_stop(msc, FALSE, "Scenario stopped");

	kvvec_destroy(msc->match_kv, KVVEC_FREE_ALL);

	if (msc->event_buffer != NULL) {
		g_ptr_array_unref(msc->event_buffer);
	}
	client_source_destroy(msc->cs);
	server_source_destroy(msc->ss);
	g_free(msc->tag);
	g_free(msc);
}
static void mrlscenconn_clear_buffer(MerlinScenarioConnection *msc) {
	g_ptr_array_set_size(msc->event_buffer, 0);
}
static void mrlscenconn_record_match_set(MerlinScenarioConnection *msc,
	const char *typestr, struct kvvec *matchkv) {

	msc->match_eventtype = event_packer_str_to_type(typestr);
	kvvec_destroy(msc->match_kv, KVVEC_FREE_ALL);
	msc->match_kv = matchkv;
}

static void mrlscenconn_print_event(merlin_event *evt, const char *conn_tag) {
	struct kvvec *evtkv;
	const char *evtname;
	int i;

	evtkv = event_packer_pack_kvv(evt, &evtname);
	g_print("%-15s Merlin event: %s\n", conn_tag, evtname);
	for(i=0; i<evtkv->kv_pairs; i++) {
		char *valstr = strndup(evtkv->kv[i].value, evtkv->kv[i].value_len);
		g_print("%-15s  %30s = %s\n", conn_tag, evtkv->kv[i].key, valstr);
		free(valstr);
	}
	kvvec_destroy(evtkv, KVVEC_FREE_ALL);
}

static gboolean mrlscenconn_record_match(MerlinScenarioConnection *msc,
	merlin_event *evt) {

	struct kvvec *evtkv;
	int evt_i, match_i, misses;

	const char *evtname;

	evtkv = event_packer_pack_kvv(evt, &evtname);

	if (evt->hdr.type != msc->match_eventtype) {
		kvvec_destroy(evtkv, KVVEC_FREE_ALL);
		return FALSE;
	}

	misses = 0;
	for (match_i = 0; match_i < msc->match_kv->kv_pairs; match_i++) {
		int found = 0;
		for (evt_i = 0; evt_i < evtkv->kv_pairs; evt_i++) {
			if (0 == strcmp(evtkv->kv[evt_i].key,
				msc->match_kv->kv[match_i].key)) {
				found = 1;
				if (0 != strcmp(evtkv->kv[evt_i].value,
					msc->match_kv->kv[match_i].value)) {
					/* Key matches, but not value = miss */
					misses++;
				}
			}
		}
		if (!found) {
			/* If we search for a non-existing key, it's a miss */
			misses++;
		}
	}
	kvvec_destroy(evtkv, KVVEC_FREE_ALL);

	return misses == 0;
}

static gpointer net_conn_new(ConnectionStorage *conn, gpointer user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *) user_data;
	if(msc->conn != NULL) {
		/* If we already have a connection, we can't take a new one */
		/* TODO: make it possible to disconnect/reject connection */
		return NULL;
	}
	msc->mr = merlinreader_new();
	msc->conn = conn;

	if(msc->current_step == STEP_MERLIN_IS_CONNECTED) {
		steptimer_stop(msc, TRUE, "Connected");
	}

	return msc;
}
static void net_conn_data(ConnectionStorage *conn, gpointer buffer,
	gsize length, gpointer conn_user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *) conn_user_data;
	merlin_event *evt;
	gsize read_size;

	if(msc == NULL) {
		/* It's a connection we can't handle, just ignore */
		return;
	}

	while (length) {
		read_size = merlinreader_add_data(msc->mr, buffer, length);
		length -= read_size;
		buffer += read_size;

		while (NULL != (evt = merlinreader_get_event(msc->mr))) {
			mrlscenconn_print_event(evt, msc->tag);
			if (msc->event_buffer == NULL) {
				g_free(evt);
			} else {
				if (msc->current_step == STEP_MERLIN_EVENT_RECEIVED) {
					if(mrlscenconn_record_match(msc, evt)) {
						steptimer_stop(msc, TRUE, NULL);
					}
				}
				if (msc->current_step == STEP_MERLIN_EVENT_NOT_RECEIVED) {
					if(mrlscenconn_record_match(msc, evt)) {
						steptimer_stop(msc, FALSE, "Message received");
					}
				}
				g_ptr_array_add(msc->event_buffer, evt);
			}
		}
	}
}
static void net_conn_close(gpointer conn_user_data) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection *) conn_user_data;
	if (msc == NULL)
		return;
	merlinreader_destroy(msc->mr);
	msc->mr = NULL;
	msc->conn = NULL;

	if(msc->current_step == STEP_MERLIN_IS_DISCONNECTED) {
		steptimer_stop(msc, TRUE, "Disconnected");
	}
}

static void steptimer_start(MerlinScenarioConnection *msc, CukeResponseRef respref, guint timeout, gboolean success, const gchar *message) {
	msc->steptimer.respref = respref;
	msc->steptimer.success = success;
	msc->steptimer.message = message;
	msc->steptimer.timer = g_timeout_add(timeout, steptimer_timeout, msc);
}

static void steptimer_stop(MerlinScenarioConnection *msc, gboolean success, const gchar *message) {
	if(msc->steptimer.respref) {
		CukeResponseRef respref = msc->steptimer.respref;
		GSource *timersource;

		msc->steptimer.respref = NULL;

		timersource = g_main_context_find_source_by_id(NULL, msc->steptimer.timer);
		msc->steptimer.timer = 0;
		g_source_destroy(timersource);

		/* If stopped explicitly, we need to send a status message */
		if(success) {
			STEP_OK;
		} else {
			STEP_FAIL(message);
		}
	}
}

static gboolean steptimer_timeout(gpointer userdata) {
	MerlinScenarioConnection *msc = (MerlinScenarioConnection*) userdata;
	if(msc->steptimer.respref) {
		CukeResponseRef respref = msc->steptimer.respref;
		msc->steptimer.respref = NULL;
		msc->steptimer.timer = 0;


		/* If stopped by timeout, we need to send the timeout message */
		if(msc->steptimer.success) {
			STEP_OK;
		} else {
			STEP_FAIL(msc->steptimer.message);
		}
	}
	return FALSE; /* G_SOURCE_REMOVE */
}
