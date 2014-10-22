#include "module.h"
#include "dlist.h"
#include <naemon/naemon.h>

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

/* Our primary query handler */
int merlin_qh(int sd, char *buf, unsigned int len)
{
	unsigned int i;

	if (len == 0)
		return help(sd);

	/* last byte is always nul */
	while (buf[len - 1] == 0 || buf[len - 1] == '\n')
		buf[--len] = 0;

	linfo("qh request: '%s' (%u)", buf, len);
	if(len == 8 && !memcmp(buf, "nodeinfo", len)) {
		dump_nodeinfo(&ipc, sd, 0);
		for(i = 0; i < num_nodes; i++) {
			dump_nodeinfo(node_table[i], sd, i + 1);
		}
		return 0;
	}
	if (len == 4 && !memcmp(buf, "help", len))
		return help(sd);
	if (!prefixcmp(buf, "help"))
		return help(sd);
	if (len == 7 && !memcmp(buf, "cbstats", len)) {
		dump_cbstats(&ipc, sd);
		for(i = 0; i < num_nodes; i++) {
			dump_cbstats(node_table[i], sd);
		}
		return 0;
	}
	if (len == strlen("expired") && !memcmp(buf, "expired", len)) {
		dump_expired(sd);
		return 0;
	}
	if (!strcmp(buf, "notify-stats")) {
		dump_notify_stats(sd);
		return 0;
	}
	return 400;
}
