#include "module.h"
#include <nagios/nagios.h>

static int dump_nodeinfo(merlin_node *n, int sd)
{
	merlin_nodeinfo *i;
	merlin_node_stats *s = &n->stats;

	i = n == &ipc ? &self : &n->info;

	nsock_printf(sd, "name=%s;source_name=%s;socket=%d;type=%s;"
				 "state=%s;peer_id=%u;flags=%d;"
				 "address=%s;port=%u;"
				 "data_timeout=%u;last_recv=%lu;last_sent=%lu;"
				 "last_conn_attempt=%lu;last_action=%d;"
				 "binlog_size=%u;iocache_available=%lu;"
				 "events_sent=%llu;events_read=%llu;"
				 "events_logged=%llu;events_dropped=%llu;"
				 "bytes_sent=%llu;bytes_read=%llu;"
				 "bytes_logged=%llu;bytes_dropped=%llu;"
				 "version=%u;word_size=%u;byte_order=%u;"
				 "object_structure_version=%u;start=%lu.%lu;"
				 "last_cfg_change=%lu;config_hash=%s;"
				 "self_assigned_peer_id=%u;"
				 "active_peers=%u;configured_peers=%u;"
				 "active_pollers=%u;configured_pollers=%u;"
				 "active_masters=%u;configured_masters=%u;"
				 "host_checks_handled=%u;service_checks_handled=%u;"
				 "monitored_object_state_size=%u;socket=%d\n",
				 n->name, n->source_name, n->sock, node_type(n),
				 node_state_name(n->state), n->peer_id, n->flags,
				 inet_ntoa(n->sain.sin_addr), ntohs(n->sain.sin_port),
				 n->data_timeout, n->last_recv, n->last_sent,
				 n->last_conn_attempt, n->last_action,
				 binlog_size(n->binlog), iocache_available(n->ioc),
				 s->events.sent, s->events.read,
				 s->events.logged, s->events.dropped,
				 s->bytes.sent, s->bytes.read,
				 s->bytes.logged, s->bytes.dropped,
				 i->version, i->word_size, i->byte_order,
				 i->object_structure_version, i->start.tv_sec, i->start.tv_usec,
				 i->last_cfg_change, tohex(i->config_hash, 20),
				 i->peer_id,
				 i->active_peers, i->configured_peers,
				 i->active_pollers, i->configured_pollers,
				 i->active_masters, i->configured_masters,
				 i->host_checks_handled, i->service_checks_handled,
				 i->monitored_object_state_size, n->sock);
	return 0;
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
		dump_nodeinfo(&ipc, sd);
		for(i = 0; i < num_nodes; i++) {
			dump_nodeinfo(node_table[i], sd);
		}
		return 0;
	}
	return 400;
}
