#include <stdio.h>
#include <glib.h>

#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "client_gsource.h"
#include "merlinreader.h"
#include "console.h"
#include "event_packer.h"
#include <shared/shared.h>
#include <shared/compat.h>

GMainLoop *g_mainloop = NULL;
char *program_name;

static void stop_mainloop(int signal);

static gpointer test_conn_new(gpointer conn, gpointer user_data);
static void test_conn_data(gpointer conn, gpointer buffer, gsize length, gpointer conn_user_data);
static void test_conn_close(gpointer conn_user_data);
static void parse_args(struct ConnectionInfo *conn, int argc, char *argv[]);
static void usage(char *msg) __attribute__((noreturn));

static void merlincat_newline(const char *line, gpointer user_data);

int main(int argc, char *argv[]) {
	ClientSource *cs = NULL;
	gpointer *current_conn = NULL;
	int retcode = 0;

	ConsoleIO *cio;
	program_name = argv[0];

	struct ConnectionInfo conn_info;
	parse_args(&conn_info, argc, argv);

	printf("type: %d\n", conn_info.type);
	printf("dest: %s\n", conn_info.dest_addr);
	printf("dest: %d\n", conn_info.dest_port);
	printf("source: %s\n", conn_info.source_addr);
	printf("source: %d\n", conn_info.source_port);
	printf("listen: %d\n", conn_info.listen);

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

	cs = client_source_new(&conn_info, test_conn_new, test_conn_data, test_conn_close, &current_conn);
	if(cs == NULL) {
		fprintf(stderr, "Could not connect\n");
		retcode = 1;
		goto cleanup;
	}

	cio = consoleio_new(merlincat_newline, (gpointer)&current_conn);

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
	gpointer *current_conn = (gpointer*)user_data;
	MerlinReader *mr = merlinreader_new();
	printf("TEST: Connected\n");

	/* Save this socket, so we can send data to it later */
	*current_conn = conn;

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
	gpointer *current_conn = (gpointer*)user_data;
	merlin_event *evt = NULL;

	if(*current_conn == NULL) {
		printf("Not connected\n");
		return;
	}

	evt = event_packer_unpack(line);
	if(evt == NULL) {
		printf("Couldn't parse packet\n");
		return;
	}
	client_source_send(*current_conn, evt, HDR_SIZE + evt->hdr.len);
	printf("Sent %d bytes\n", evt->hdr.len + HDR_SIZE);
	free(evt);
}

static void usage(char *msg) {
	if (msg)
		printf("%s\n\n", msg);

	printf("Usage: %s -t <conntype> -d <address> [-l] [-s <address>]\n", program_name);
	printf("  -t <conntype>   Specify connection type ('unix' or 'inet')\n");
	printf("  -d <address>    Address parameters\n");
	printf("  -s <address>    Source address parameters (optional)\n");
	printf("  -l              Listen instead of connect (default: no)\n");
	printf("\n  An \"address\" consists of <address>:<ip>\n");
	exit(EXIT_FAILURE);
}

static void parse_args(struct ConnectionInfo *conn_info, int argc, char *argv[]) {
	int c;
	char *dest   = NULL;
	char *source = NULL;
	opterr = 0;

	/* Initial values for conn_info */
	memset(conn_info, 0, sizeof(*conn_info));
	conn_info->type = -1;

	while((c = getopt(argc, argv, "t:d:s:l:")) != -1) {
		switch(c) {
			case 't':
				/* Connection type. Could be tcp, unix, etc... */
				if (0 == strcasecmp("inet", optarg))
					conn_info->type = AF_INET;
				else if (0 == strcasecmp("unix", optarg))
					conn_info->type = AF_UNIX;
				else
					usage("Invalid argument for option -t");
				break;
			case 'd':
				/*
				 * Connection destination. Format depending on type.
				 * Save destination and parse later when we know the type.
				 */
				dest = strdup(optarg);
				break;
			case 's':
				/*
				 * Connection source. Only applicable on some types.
				 * Save source and parse later when we know the type.
				 */
				source = strdup(optarg);
				break;
			case 'l':
				/* TODO: Implement this when it is needed for testing. */
				break;
			case '?':
			default:
				usage("Option not found or missing argument for option");
		}
	}

	if (conn_info->type == -1 || dest == NULL) {
		usage("Required option(s) missing");
	}

	if (optind != argc) {
		int i;
		printf("Unhandled arguments present: ");
		for (i = optind; i < argc; i++) {
			printf("%s%s", argv[i], i == argc - 1 ? "\n" : ", ");
		}
		usage(NULL);
	}

	switch (conn_info->type) {
		case AF_UNIX:
			/* TODO: Check that destination seems to be correct. Ignore source. */

			break;
		case AF_INET:
			/* Parse address and port from dest string. */
			conn_info->dest_addr = strsep(&dest, ":"); /* Moves pointer in dest to dest_addr. */
			if (dest)
				conn_info->dest_port = atoi(dest);
			else
				usage("Destination port required");
			dest = NULL;

			/* Parse address and port from source string. */
			if (source) {
				conn_info->source_addr = strsep(&source, ":"); /* Moves pointer in source to source_addr. */
				if (source)
					conn_info->source_port = atoi(source);
				source = NULL;
			}
			break;
		default:
			/* We can never end up here */
			break;
	}

	if (dest)   free(dest);
	if (source) free(source);
}
