/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright(C) 2006 OP5 AB
 * All rights reserved.
 *
 */

#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <errno.h>

#include "shared.h"
#include "config.h"
#include "types.h"
#include "ipc.h"
#include "net.h"
#include "protocol.h"

extern const char *__progname;

int default_port = 15551;

static void dump_core(int sig)
{
	kill(getpid(), SIGSEGV);
	exit(1);
}

static void usage(char *fmt, ...)
{
	if (fmt) {
		va_list ap;

		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		putchar('\n');
	}

	printf("Usage: %s -c <config-file> [-d] [-h]\n\n", __progname);

	exit(1);
}


void destroy_node(struct node *node)
{
	if (!node)
		return;

	if (node->name)
		free(node->name);

	close(node->sock);
	free(node);
}


static void grok_node(struct compound *c, struct node *node)
{
	int i;

	if (!node)
		return;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!v->val)
			cfg_error(c, v, "Variable must have a value\n");

		if (node->type != MODE_NOC && !strcmp(v->var, "hostgroup")) {
			node->hostgroup = strdup(v->val);
			node->selection = add_selection(node->hostgroup);
		}
		else if (!strcmp(v->var, "address")) {
			if (!net_resolve(v->val, &node->sain.sin_addr))
				cfg_error(c, v, "Unable to resolve '%s'\n", v->val);
		}
		else if (!strcmp(v->var, "port")) {
			node->sain.sin_port = ntohs((unsigned short)atoi(v->val));
			if (!node->sain.sin_port)
				cfg_error(c, v, "Illegal value for port: %s\n", v->val);
		}
		else
			cfg_error(c, v, "Unknown variable\n");
	}
}

static void grok_daemon_compound(struct compound *comp)
{
	int i;

	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];

		if (!strcmp(v->var, "port")) {
			char *endp;

			default_port = strtoul(v->val, &endp, 0);
			if (default_port < 1 || default_port > 65535 || *endp)
				cfg_error(comp, v, "Illegal value for port: %s", v->val);
			continue;
		}

		if (grok_common_var(comp, v))
			continue;
		if (log_grok_var(v->var, v->val))
			continue;

		cfg_error(comp, v, "Unknown variable");
	}
}

static int grok_config(char *path)
{
	int i, node_i = 0;
	struct compound *config;
	struct node *table;

	if (!path)
		return 0;

	config = cfg_parse_file(path);
	if (!config)
		return 0;

	for (i = 0; i < config->vars; i++) {
		struct cfg_var *v = config->vlist[i];

		if (!v->val)
			cfg_error(config, v, "No value for option '%s'", v->var);

		if (grok_common_var(config, v))
			continue;

		if (!strcmp(v->var, "port")) {
			default_port = (unsigned short)strtoul(v->val, NULL, 0);
			continue;
		}

		cfg_warn(config, v, "Unrecognized variable\n");
	}

	/* each compound represents either a node or an error. we'll bail
	 * on errors, but it's nice to keep nodes in continuous memory */
	table = calloc(config->nested, sizeof(struct node));

	for (i = 0; i < config->nested; i++) {
		struct compound *c = config->nest[i];
		struct node *node;

		if (!strcmp(c->name, "module"))
			continue;

		if (!strcmp(c->name, "daemon")) {
			grok_daemon_compound(c);
			continue;
		}

		node = &table[node_i++];
		node->name = next_word(c->name);

		if (!strncmp(c->name, "poller", 6)) {
			node->type = MODE_POLLER;
			grok_node(c, node);
			if (!node->hostgroup)
				cfg_error(c, NULL, "Missing 'hostgroup' variable\n");
		}
		else if (!strncmp(c->name, "peer", 4)) {
			node->type = MODE_PEER;
			grok_node(c, node);
		}
		else if (!strncmp(c->name, "noc", 3)) {
			node->type = MODE_NOC;
			grok_node(c, node);
		}
		else
			cfg_error(c, NULL, "Unknown compound type\n");

		if (node->name)
			node->name = strdup(node->name);
		else
			node->name = strdup(inet_ntoa(node->sain.sin_addr));

		node->sock = -1;
	}

	cfg_destroy_compound(config);

	create_node_tree(table, node_i);

	return 1;
}


static void polling_loop(void)
{
	int count = 0;

	/* do this once first so we can avoid the binary backlog unnecessarily */
	net_poll();

	for (;;) {
		struct proto_hdr hdr;
		int result;

		/* we want to keep the ipc socket emptied, so keep reading until
		 * it returns zero, or until we've done it 5 times */
		result = ipc_read(&hdr, sizeof(hdr), 0);
		if (result == sizeof(hdr)) {
			send_ipc_data(&hdr);

			if (++count < 5)
				continue;
		}
		else if (result < 0) {
			lerr("ipc_read() returned %d: %s", result, strerror(errno));
		}

		count = 0;
		net_poll();
	}
}


static void clean_exit(int sig)
{
	ipc_unlink();
	net_deinit();

	_exit(!!sig);
}


int main(int argc, char **argv)
{
	int i;
	char *config_file = NULL;

	is_module = 0;

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (*arg == '-') {
			switch (arg[1]) {
			case 'h':
				usage(NULL);
				break;
			case 'c':
				config_file = argv[++i];
				break;
			case 'd':
				debug++;
				break;
			default:
				usage("Unknown option: %c\n", arg[1]);
				break;
			}
		}
		else {
			if (!config_file)
				config_file = arg;
			else
				usage("Unknown option: %s\n", arg);
		}
	}

	if (!config_file)
		usage("No config-file specified\n");

	if (!grok_config(config_file)) {
		fprintf(stderr, "%s contains errors. Bailing out\n", config_file);
		return 1;
	}

	if (ipc_bind() < 0) {
		printf("Failed to initalize ipc socket: %s\n", strerror(errno));
		return 1;
	}
	if (net_init() < 0) {
		printf("Failed to initialize networking: %s\n", strerror(errno));
		return 1;
	}

	signal(SIGINT, clean_exit);
	signal(SIGTERM, clean_exit);
	signal(SIGPIPE, dump_core);
	polling_loop();

	clean_exit(0);

	return 0;
}
