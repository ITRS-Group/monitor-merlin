#include "shared.h"
#include "module.h"
#include "dlist.h"
#include "logging.h"
#include "ipc.h"
#include "testif_qh.h"
#include <naemon/naemon.h>
#include <string.h>
#include "runcmd.h"
#include "node.h"

static int dump_cbstats(merlin_node *n, int sd)
{
	int i;

	nsock_printf(sd, "name=%s;type=%s;", n->name, node_type(n));
	for (i = 0; i <= NEBCALLBACK_NUMITEMS; i++) {
		const char *cb_name = callback_name(i);
		/* don't print empty values */
		if(!n->stats.cb_count[i].in && !n->stats.cb_count[i].in)
			continue;
		nsock_printf(sd, "%s_IN=%u;%s_OUT=%u;",
					 cb_name, n->stats.cb_count[i].in,
					 cb_name, n->stats.cb_count[i].in);
	}
	nsock_printf(sd, "\n");
	return 0;
}

static int dump_notify_stats(int sd)
{
	int a, b, c;

	for (a = 0; a < 9; a++) {
		const char *rtype = notification_reason_name(a);
		for (b = 0; b < 2; b++) {
			const char *ntype = b == SERVICE_NOTIFICATION ? "SERVICE" : "HOST";
			for (c = 0; c < 2; c++) {
				const char *ctype = c == CHECK_TYPE_ACTIVE ? "ACTIVE" : "PASSIVE";
				struct merlin_notify_stats *mns = &merlin_notify_stats[a][b][c];
				nsock_printf(sd, "type=%s;reason=%s;checktype=%s;"
					"peer=%lu;poller=%lu;master=%lu;net=%lu;sent=%lu\n",
					ntype, rtype, ctype,
					mns->peer, mns->poller, mns->net, mns->master, mns->sent);
			}
		}
	}
	return 0;
}

static int help(int sd)
{
	nsock_printf_nul(sd,
		"I answer questions regarding the merlin *module*, not the daemon\n"
		"nodeinfo      Print info about all nodes I know about\n"
		"cbstats       Print callback statistics for each node\n"
		"notify-stats  Print notification statistics\n"
		"expired       Print information regarding expired events\n"
		"runcmd        Runs a runcmd (test this check) on a remote node\n"
		"remote-fetch  Tells a remote node to execute mon oconf fetch\n"
	);
	return 0;
}

static int dump_expired(int sd)
{
	struct dlist_entry *it;

	dlist_foreach(expired_events, it) {
		struct merlin_expired_check *mec = it->data;
		if (mec->type == SERVICE_CHECK) {
			struct service *s = mec->object;
			nsock_printf(sd, "host_name=%s;service_description=%s;",
				s->host_name, s->description);
		} else {
			struct host *h = mec->object;
			nsock_printf(sd, "host_name=%s;", h->name);
		}
		nsock_printf(sd, "added=%lu;responsible=%s\n", mec->added, mec->node->name);
	}
	return 0;
}

/* Called when a node recieves a RUNCMD_RESP packet. Output is printed to socket */
int runcmd_callback(merlin_node *node, merlin_event *pkt){
	merlin_runcmd * runcmd = (merlin_runcmd *) pkt->body;
	nsock_printf_nul(runcmd->sd,
		"%s", runcmd->content
	);
	return 0;

}

/* runc query handler for requesting a remote node to run a runcmd command */
static int remote_runcmd(int sd, char *buf, unsigned int len)
{
	struct kvvec *kvv;
	char * node_name;
	merlin_node * node = NULL;
	uint i;
	runcmd_ctx * ctx;
	merlin_runcmd * runcmd;

	if (0 != prefixcmp(buf, "node=")) {
		nsock_printf_nul(sd, "outstd=runcmd must start with the node\n");
		return 1;
	}

	/* get node name from buffer */
	kvv = ekvstr_to_kvvec(buf);
	node_name = kvvec_fetch_str_str(kvv, "node");

	/* get the node */
	for (i = 0; i < num_nodes; i++) {
		if (strcmp(node_name, node_table[i]->name) == 0) {
			node = node_table[i];
			break;
		}
	}

	if (node == NULL) {
		nsock_printf_nul(sd, "outstd=Could not find node: %s does it exist?\n", node_name);
		return 1;
	}

	if (!node->encrypted) {
		nsock_printf_nul(sd, "outstd=Encryption must be enbled to use SSH-less test this check on nodes with connect set to no\n");
		return 1;
	}

	if (node->state != STATE_CONNECTED) {
		nsock_printf_nul(sd, "outstd=Node %s in not connected\n", node->name);
		return 1;
	}

	/* Filter away the node name from the command */
	buf = strchr(buf, ';');
	if (buf == NULL) {
		nsock_printf_nul(sd, "outstd=Error while parsing command\n");
		return 1;
	}
	buf = buf + 1;

	/* Destroy/free kvvec */
	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	/* set runcmd */
	runcmd = malloc(sizeof(*runcmd));
	if (runcmd == NULL) {
		nsock_printf_nul(sd, "outstd=Failed to malloc runcmd");
		return 0;
	}
	runcmd->sd = sd;
	runcmd->content = strdup(buf);

	/* set the context */
	ctx = malloc(sizeof(*ctx));
	if (runcmd == NULL) {
		nsock_printf_nul(sd, "outstd=Failed to malloc runcmd context");
		free(runcmd);
		return 0;
	}
	ctx->node = node;
	ctx->runcmd = runcmd;
	ctx->type = RUNCMD_CMD;

	schedule_event(0, send_runcmd_cmd, ctx);
	return 0;
}

/* We run this in a scheduled task to send a ctrl_fetch to a specific node*/
void send_ctrl_fetch(struct nm_event_execution_properties *evprop) {
	merlin_node * node = (merlin_node *) evprop->user_data;
	node_ctrl(node, CTRL_FETCH, CTRL_GENERIC, NULL, 0);
}

/* Send a CTRL_FETCH to a remote node, which in turn does a mon oconf fetch.
 * format: node=name;node_type=master/poller/peer.
 * Name ignored if type set
 */
static int remote_fetch(int sd, char *buf, unsigned int len) {
	struct kvvec *kvv;
	char * node_name = NULL;
	char * type_str = NULL;
	int type;

	if (0 != prefixcmp(buf, "node=") && 0 != prefixcmp(buf, "type=")) {
		nsock_printf_nul(sd, "command must start with node= or type=\n");
	}

	kvv = ekvstr_to_kvvec(buf);

	/*
	 * if node/type is supplied by with no actual data i.e node=;type=poller
	 * then the kvvvec fucntion will segfault. So we sanity check to make sure
	 * we provide a sensible value before trying to fetch the string.
	 */
	if (strstr(buf, "node=;") == NULL) {
		node_name = kvvec_fetch_str_str(kvv, "node");
	}
	if (strstr(buf, "type=;") == NULL) {
		type_str = kvvec_fetch_str_str(kvv, "type");
	}

	if (type_str != NULL) {
		unsigned int i;
		if (strcmp(type_str, "poller") == 0) {
			type=MODE_POLLER;
		} else if (strcmp(type_str, "master") == 0) {
			nsock_printf_nul(sd, "Masters shouldn't fetch from pollers.\n");
			return 1;
		} else if (strcmp(type_str, "peer") == 0) {
			type=MODE_PEER;
		} else {
			nsock_printf_nul(sd, "Unknown node type: %s\n", type_str);
			return 1;
		}

		for (i = 0; i < num_nodes; i++) {
			merlin_node *node = noc_table[i];
			if (node->state == STATE_CONNECTED && node->type == type) {
				ldebug("Sending ctrl_fetch to: %s\n", node->name);
				schedule_event(0, send_ctrl_fetch, node);
			}
		}
	} else if (node_name != NULL) {
		merlin_node * node = NULL;
		unsigned int i;

		for (i = 0; i < num_nodes; i++) {
			if (strcmp(node_name, node_table[i]->name) == 0) {
				node = node_table[i];
				break;
			}
		}

		if (node == NULL) {
			nsock_printf_nul(sd, "Could not find node: %s does it exist?\n", node_name);
			return 1;
		} else if (node->state != STATE_CONNECTED) {
			nsock_printf_nul(sd, "Node %s is not connected\n", node->name);
		}
		else {
			ldebug("Sending ctrl_fetch to: %s\n", node->name);
			schedule_event(0, send_ctrl_fetch, node);
		}
	}

	return 0;
}

/* Our primary query handler */
int merlin_qh(int sd, char *buf, unsigned int len)
{
	unsigned int i;

	if (0 == strcmp(buf, ""))
		return help(sd);

	ldebug("qh request: '%s' (%u)", buf, len);
	if(0 == strcmp(buf, "nodeinfo")) {
		dump_nodeinfo(&ipc, sd, 0);
		for(i = 0; i < num_nodes; i++) {
			dump_nodeinfo(node_table[i], sd, i + 1);
		}
		return 0;
	}
	if (0 == strcmp(buf, "help"))
		return help(sd);

	if (0 == prefixcmp(buf, "help"))
		return help(sd);

	if (0 == strcmp(buf, "cbstats")) {
		dump_cbstats(&ipc, sd);
		for(i = 0; i < num_nodes; i++) {
			dump_cbstats(node_table[i], sd);
		}
		return 0;
	}
	if (0 == strcmp(buf, "expired")) {
		dump_expired(sd);
		return 0;
	}
	if (0 == strcmp(buf, "notify-stats")) {
		dump_notify_stats(sd);
		return 0;
	}
	if (0 == prefixcmp(buf, "runcmd ")) {
		remote_runcmd(sd, buf+7, len);
		return 0;
	}
	if (0 == prefixcmp(buf, "remote-fetch ")) {
		remote_fetch(sd, buf+13, len);
		return 0;
	}

	/*
	 * This is used for test case integration, shouldn't be documented and used
	 * in production, since the system will misbehave if used
	 */
	if (0 == prefixcmp(buf, "testif ")) {
		return merlin_testif_qh(sd, buf+7);
	}

	return 400;
}
