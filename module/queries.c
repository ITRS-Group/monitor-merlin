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

	/*
	 * This is used for test case integration, shouldn't be documented and used
	 * in production, since the system will misbehave if used
	 */
	if (0 == prefixcmp(buf, "testif ")) {
		return merlin_testif_qh(sd, buf+7);
	}

	return 400;
}
