#include "module.h"
#include <nagios/nagios.h>

int dump_nodeinfo(merlin_node *n, int sd, int instance_id)
{
	merlin_nodeinfo *i;
	merlin_node_stats *s = &n->stats;

	i = n == &ipc ? &self : &n->info;

	nsock_printf(sd, "instance_id=%d;name=%s;source_name=%s;socket=%d;type=%s;"
				 "state=%s;peer_id=%u;flags=%d;"
				 "address=%s;port=%u;"
				 "data_timeout=%u;last_recv=%lu;last_sent=%lu;"
				 "last_conn_attempt=%lu;last_action=%d;latency=%d;"
				 "binlog_size=%u;iocache_available=%lu;"
				 "events_sent=%llu;events_read=%llu;"
				 "events_logged=%llu;events_dropped=%llu;"
				 "bytes_sent=%llu;bytes_read=%llu;"
				 "bytes_logged=%llu;bytes_dropped=%llu;"
				 "version=%u;word_size=%u;byte_order=%u;"
				 "object_structure_version=%u;start=%lu.%lu;"
				 "last_cfg_change=%lu;config_hash=%s;"
				 "self_assigned_peer_id=%u;warn_flags=%u;"
				 "active_peers=%u;configured_peers=%u;"
				 "active_pollers=%u;configured_pollers=%u;"
				 "active_masters=%u;configured_masters=%u;"
				 "host_checks_handled=%u;service_checks_handled=%u;"
				 "host_checks_executed=%u;service_checks_executed=%u;"
				 "monitored_object_state_size=%u;connect_time=%lu\n",
				 instance_id,
				 n->name, n->source_name, n->sock, node_type(n),
				 node_state_name(n->state), n->peer_id, n->flags,
				 inet_ntoa(n->sain.sin_addr), ntohs(n->sain.sin_port),
				 n->data_timeout, n->last_recv, n->last_sent,
				 n->last_conn_attempt, n->last_action, n->latency,
				 binlog_size(n->binlog), iocache_available(n->ioc),
				 s->events.sent, s->events.read,
				 s->events.logged, s->events.dropped,
				 s->bytes.sent, s->bytes.read,
				 s->bytes.logged, s->bytes.dropped,
				 i->version, i->word_size, i->byte_order,
				 i->object_structure_version, i->start.tv_sec, i->start.tv_usec,
				 i->last_cfg_change, tohex(i->config_hash, 20),
				 i->peer_id, n->warn_flags,
				 i->active_peers, i->configured_peers,
				 i->active_pollers, i->configured_pollers,
				 i->active_masters, i->configured_masters,
				 i->host_checks_handled, i->service_checks_handled,
				 n->host_checks, n->service_checks,
				 i->monitored_object_state_size, n->connect_time);
	return 0;
}

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

static int help(int sd, char *buf, unsigned int len)
{
	nsock_printf_nul(sd,
		"I answer questions regarding the merlin *module*, not the daemon\n"
		"nodeinfo   Print info about all nodes I know about\n"
		"cbstats    Print callback statistics for each node\n"
	);
}

/* Our primary query handler */
int merlin_qh(int sd, char *buf, unsigned int len)
{
	int i;

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
		return help(sd, NULL, 0);
	if (!prefixcmp(buf, "help"))
		return help(sd, buf + 5, len - 5);
	if (len == 7 && !memcmp(buf, "cbstats", len)) {
		dump_cbstats(&ipc, sd);
		for(i = 0; i < num_nodes; i++) {
			dump_cbstats(node_table[i], sd);
		}
		return 0;
	}
	return 400;
}
