#include <stdio.h>
#include <glib.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include "client_gsource.h"
#include "merlinreader.h"
#include "console.h"
#include "event_packer.h"
#include <shared/shared.h>
#include <shared/compat.h>

GMainLoop *g_mainloop = NULL;

static void stop_mainloop(int signal);

static gpointer test_conn_new(gpointer conn, gpointer user_data);
static void test_conn_data(gpointer conn, gpointer buffer, gsize length, gpointer conn_user_data);
static void test_conn_close(gpointer conn_user_data);

static void merlincat_newline(const char *line, gpointer user_data);

int main(int argc, char *argv[]) {
	ClientSource *cs = NULL;
	int retcode = 0;

	ConsoleIO *cio;

	g_type_init();

	/*
	 * Create main loop, contains information about what should be checked. Is a
	 * singleton
	 */
	g_mainloop = g_main_loop_new(NULL, TRUE);

	/* non glib-unix version of stopping signals */
	signal(SIGHUP, stop_mainloop);
	signal(SIGINT, stop_mainloop);
	signal(SIGTERM, stop_mainloop);

	cio = consoleio_new(merlincat_newline, NULL);

	cs = client_source_new("", test_conn_new, test_conn_data, test_conn_close, NULL);
	if(cs == NULL) {
		fprintf(stderr, "Could not connect\n");
		retcode = 1;
		goto cleanup;
	}

	g_main_loop_run(g_mainloop);

cleanup:
	consoleio_destroy(cio);
	client_source_destroy(cs);
	g_main_loop_unref(g_mainloop);
	return retcode;
}

static void stop_mainloop(int signal) {
	g_main_loop_quit(g_mainloop);
}

static gpointer test_conn_new(gpointer conn, gpointer user_data) {
	MerlinReader *mr = merlinreader_new();
	printf("TEST: Connected\n");

	merlin_event pkt;
	merlin_nodeinfo node;

	memset(&pkt.hdr, 0, HDR_SIZE);
	memset(&node, 0, sizeof(merlin_nodeinfo));

/*
	119         ("L", "version", 1),
	120         ("L", "word_size", 64), # bits per register (sizeof(void *) * 8)
	121         ("L", "byte_order", 1234), # 1234 = little, 4321 = big, ...
	122         ("L", "object_structure_version", 402),
	123         #struct timeval start; # module (or daemon) start time
	124         ("16s", None, ""),
	125         ("Q", "last_cfg_change", 0), # when config was last changed
	126         ("20s", "config_hash", "a cool config hash"), # SHA1 hash of object config hash
	127         ("L", "peer_id", 0), # self-assigned peer-id
	128         ("L", "active_peers", 0),
	129         ("L", "configured_peers", 0),
	130         ("L", "active_pollers", 0),
	131         ("L", "configured_pollers", 0),
	132         ("L", "active_masters", 0),
	133         ("L", "configured_masters", 0),
	134         ("L", "host_checks_handled", 0),
	135         ("L", "service_checks_handled", 0),
	136         ("L", "monitored_object_state_size", 0)
*/

	pkt.hdr.sig.id = MERLIN_SIGNATURE;
	pkt.hdr.protocol = MERLIN_PROTOCOL_VERSION;
	gettimeofday(&pkt.hdr.sent, NULL);
	pkt.hdr.type = CTRL_PACKET;
	pkt.hdr.code = CTRL_ACTIVE;
	pkt.hdr.selection = CTRL_GENERIC & 0xffff;

	node.version = 1; //MERLIN_NODEINFO_VERSION;
	node.word_size = 64; //COMPAT_WORDSIZE;
	node.byte_order = 1234; //endianness();
	node.monitored_object_state_size = sizeof(monitored_object_state);
	node.object_structure_version = 402; //CURRENT_OBJECT_STRUCTURE_VERSION;
	gettimeofday(&node.start, NULL);

	node.last_cfg_change = 1444405566;
	strcpy(node.config_hash, "");//"a77d59a6578c71d7e17f4793a08bcf15d4c6fa06");

	node.configured_peers = 1;

	pkt.hdr.len = sizeof(merlin_nodeinfo);
	memcpy(&pkt.body, &node, sizeof(merlin_nodeinfo));

	client_source_send(conn, &pkt, HDR_SIZE + pkt.hdr.len);

	return mr;
}
static void test_conn_data(gpointer conn, gpointer buffer, gsize length, gpointer conn_user_data) {
	MerlinReader *mr = (MerlinReader *)conn_user_data;
	merlin_event *evt;
	gsize read_size;
	char *buf;
	while(length) {
		read_size = merlinreader_add_data(mr, buffer, length);
		length -= read_size;
		buffer += read_size;

		while(NULL != (evt = merlinreader_get_event(mr))) {
			buf = event_packer_pack(evt);
			printf("Line: %s\n", buf);
			free(buf);
			g_free(evt);
		}
	}
}
static void test_conn_close(gpointer conn_user_data) {
	MerlinReader *mr = (MerlinReader *)conn_user_data;
	printf("TEST: Closed\n");
	merlinreader_destroy(mr);
	g_main_loop_quit(g_mainloop);
}

static void merlincat_newline(const char *line, gpointer user_data) {
	printf("===============\n\nYay we got a new line from command line:\n%s\n\n===============\n", line);
}
