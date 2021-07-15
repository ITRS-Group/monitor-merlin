/*
 * Process In/Out
 *
 * This file contains functions that shuffle data from the module part of
 * the module (the timed and triggered events) to the thread part of the
 * module (the multiplexing networker), as well as functions that re-insert
 * the data from the network to the running Nagios process.
 * In short, these functions are only called from the triggered event
 * thingie.
 */
#include "hooks.h"
#include "node.h"
#include "shared.h"
#include "node.h"
#include "module.h"
#include "codec.h"
#include "ipc.h"
#include "pgroup.h"
#include "net.h"
#include <string.h>
#include <naemon/naemon.h>

extern merlin_event *recv_event;

static nebstruct_comment_data *block_comment;
static int check_dupes;
static merlin_event last_pkt;
static unsigned long long dupes, dupe_bytes;
static uint32_t ev_mask;

static merlin_event tmp_notif_pkt;
static nebstruct_notification_data *tmp_notif_data;

struct merlin_check_stats {
	unsigned long long poller, peer, self, orphaned;
};
static struct merlin_check_stats service_checks, host_checks;

static int is_dupe(merlin_event *pkt)
{
	if (!check_dupes) {
		return 0;
	}

	if (last_pkt.hdr.type != pkt->hdr.type) {
		return 0;
	}

	if (packet_size(&last_pkt) != packet_size(pkt)) {
		return 0;
	}

	/* if this is truly a dupe, return 1 and log every 100'th */
	if (!memcmp(&last_pkt, pkt, packet_size(pkt))) {
		dupe_bytes += packet_size(pkt);
		if (!(++dupes % 100)) {
			ldebug("%s in %llu duplicate packets dropped",
				   human_bytes(dupe_bytes), dupes);
		}
		return 1;
	}

	return 0;
}

static int send_generic(merlin_event *pkt, void *data)
{
	int result = 0;
	uint i, ntable_stop = num_masters + num_peers;
	linked_item *li;

	if ((!num_nodes || pkt->hdr.code == MAGIC_NONET) && !daemon_wants(pkt->hdr.type)) {
		ldebug("ipcfilter: Not sending %s event. %s, and daemon doesn't want it",
			   callback_name(pkt->hdr.type),
			   pkt->hdr.code == MAGIC_NONET ? "No-net magic" : "No nodes");
		return 0;
	}
	if (!pkt->hdr.code == MAGIC_NONET && !daemon_wants(pkt->hdr.type)) {
		ldebug("ipcfilter: Not sending %s event. No-net magic and daemon doesn't want it",
			   callback_name(pkt->hdr.type));
		return 0;
	}

	pkt->hdr.len = merlin_encode_event(pkt, data);
	if (!pkt->hdr.len) {
		lerr("Header len is 0 for callback %d. Update offset in hookinfo.h", pkt->hdr.type);
		return -1;
	}

	if (is_dupe(pkt)) {
		ldebug("ipcfilter: Not sending %s event: Duplicate packet",
		       callback_name(pkt->hdr.type));
		return 0;
	}

	if (daemon_wants(pkt->hdr.type)) {
		result = ipc_send_event(pkt);
		/*
		 * preserve the event so we can check for dupes,
		 * but only if we successfully sent it
		 */
		if (result < 0)
			memset(&last_pkt, 0, sizeof(last_pkt));
		else
			memcpy(&last_pkt, pkt, packet_size(pkt));
	}

	if (!num_nodes)
		return 0;
	if (pkt->hdr.code == MAGIC_NONET)
		return 0;

	/*
	 * The module can mark certain packets with a magic destination.
	 * Such packets avoid all other inspection and get sent to where
	 * the module wants us to.
	 */
	if (magic_destination(pkt)) {
		if ((pkt->hdr.selection & DEST_MASTERS) == DEST_MASTERS) {
			for (i = 0; i < num_masters; i++) {
				net_sendto(node_table[i], pkt);
			}
		}
		if ((pkt->hdr.selection & DEST_PEERS) == DEST_PEERS) {
			for (i = 0; i < num_peers; i++) {
				net_sendto(peer_table[i], pkt);
			}
		}
		if ((pkt->hdr.selection & DEST_POLLERS) == DEST_POLLERS) {
			for (i = 0; i < num_pollers; i++) {
				net_sendto(poller_table[i], pkt);
			}
		}

		return 0;
	}

	/*
	 * "normal" packets get sent to all peers and masters, and possibly
	 * a group of, or all, pollers as well
	 */

	/* general control packets are for everyone */
	if (pkt->hdr.selection == CTRL_GENERIC && pkt->hdr.type == CTRL_PACKET) {
		ntable_stop = num_nodes;
	}

	/* Send this to all who should have it */
	for (i = 0; i < ntable_stop; i++) {
		net_sendto(node_table[i], pkt);
	}

	/* if we've already sent to everyone we return early */
	if (ntable_stop == num_nodes || !num_pollers)
		return 0;

	li = nodes_by_sel_id(pkt->hdr.selection);
	if (!li) {
		lerr("No matching selection for id %d", pkt->hdr.selection);
		return -1;
	}

	for (; li; li = li->next_item) {
		net_sendto((merlin_node *)li->item, pkt);
	}

	return result;
}

static int get_selection(const char *key)
{
	node_selection *sel = node_selection_by_hostname(key);

	return sel ? sel->id & 0xffff : DEST_PEERS_MASTERS;
}

static int get_hostgroup_selection(const char *key)
{
	node_selection *sel = node_selection_by_name(key);

	return sel ? sel->id & 0xffff : DEST_PEERS_POLLERS;
}

static int send_host_status(merlin_event *pkt, int nebattr, host *obj)
{
	merlin_host_status st_obj;
	static host *last_obj = NULL;

	if (!obj) {
		lerr("send_host_status() called with NULL obj");
		return -1;
	}
	memset(&st_obj, 0, sizeof(st_obj));
	if (obj == last_obj) {
		check_dupes = 1;
	} else {
		check_dupes = 0;
		last_obj = obj;
	}

	st_obj.nebattr = nebattr;
	st_obj.name = obj->name;
	MOD2NET_STATE_VARS(st_obj.state, obj);

	return send_generic(pkt, &st_obj);
}

static int send_service_status(merlin_event *pkt, int nebattr, service *obj)
{
	merlin_service_status st_obj;
	static service *last_obj = NULL;

	if (!obj) {
		lerr("send_service_status() called with NULL obj");
		return -1;
	}
	memset(&st_obj, 0, sizeof(st_obj));
	if (obj == last_obj) {
		check_dupes = 1;
	} else {
		check_dupes = 0;
		last_obj = obj;
	}

	st_obj.nebattr = nebattr;
	st_obj.host_name = obj->host_name;
	st_obj.service_description = obj->description;
	MOD2NET_STATE_VARS(st_obj.state, obj);

	return send_generic(pkt, &st_obj);
}

static inline int should_run_check(unsigned int id)
{
	return assigned_peer(id, ipc.info.active_peers + 1) == ipc.peer_id;
}

/**
 * hold_notification_packet() makes a deep copy of a notification message and
 * stores it in a global variable tmp_notif_data. The purpose is to make it
 * possible to send notification messages the triggering check result.
 * Meaning that if a processed check result generates a notification, the
 * notification will be stored and sent once the ending check result is sent.
 * Without this the check result will be sent right after the notification
 * packet making the receiver overwrite any information stored from the
 * notification packet.
 */
static int hold_notification_packet(merlin_event *pkt, nebstruct_notification_data *data)
{
	if(tmp_notif_data) {
		return -1;	/* there is already some stored notification, bailing */
	}

	if (data->notification_type == HOST_NOTIFICATION) {
		ldebug("holding host notification for %s", data->host_name);
	} else {
		ldebug("holding service notification for %s;%s",
				data->service_description, data->host_name);
	}

	/* copy the notification data to storage */
	memcpy(&tmp_notif_pkt, pkt, packet_size(pkt));
	tmp_notif_data = malloc(sizeof(nebstruct_notification_data));
	memcpy(tmp_notif_data, data, sizeof(nebstruct_notification_data));
	tmp_notif_data->host_name = data->host_name ? strdup(data->host_name) : NULL;
	tmp_notif_data->service_description = data->service_description ? strdup(data->service_description) : NULL;
	tmp_notif_data->output = data->output ? strdup(data->output) : NULL;
	tmp_notif_data->ack_author = data->ack_author ? strdup(data->ack_author) : NULL;
	tmp_notif_data->ack_data = data->ack_data ? strdup(data->ack_data) : NULL;

	return 0;
}

/**
 * flush_notification() is called every time we send a check result to
 * peers/masters. So if the check result being sent is a notification triggering
 * one there should be a notification packet in storage which then will be sent.
 */
static int flush_notification(void)
{
	if (tmp_notif_data == NULL)
		return -1;

	if (tmp_notif_data->notification_type == HOST_NOTIFICATION) {
		ldebug("flushing host notification for %s",
				tmp_notif_data->host_name);
	} else {
		ldebug("flushing service notification for %s;%s",
				tmp_notif_data->service_description, tmp_notif_data->host_name);
	}

	/* Send the stored notification */
	send_generic(&tmp_notif_pkt, (void *) tmp_notif_data);

	/* clear the stored notification */
	free(tmp_notif_data->host_name);
	free(tmp_notif_data->service_description);
	free(tmp_notif_data->output);
	free(tmp_notif_data->ack_author);
	free(tmp_notif_data->ack_data);
	free(tmp_notif_data);
	tmp_notif_data = NULL;

	return 0;
}

/**
 * Given a node, n, this function returns the number of nodes in that nodes
 * pollergroup that can notify
 */
static unsigned int pgroup_num_notify(merlin_node * node) {
	unsigned int i;
	unsigned int pollers_can_notify = 0;
	for (i=0; i < node->pgroup->active_nodes; i++) {
		merlin_node *tmp_node = node->pgroup->nodes[i];
		if (tmp_node->flags & MERLIN_NODE_NOTIFIES) {
			pollers_can_notify++;
		}
	}
	return pollers_can_notify;
}

/**
 * The hooks are called from broker.c in Nagios.
 * Handle service check result from local node. Should not be used from network
 */
static int hook_service_result(merlin_event *pkt, void *data)
{
	int ret;
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;
	service *s = (service *)ds->object_ptr;
	struct merlin_node *node;

	switch (ds->type) {
	case NEBTYPE_SERVICECHECK_ASYNC_PRECHECK:
		node = pgroup_service_node(s->id);
		schedule_expiration_event(SERVICE_CHECK, node, s);
		if (node != &ipc) {
			/* We're not responsible, so block this check here */
			return NEBERROR_CALLBACKCANCEL;
		} else if (node_blocked_hostgroup(node, data, ds->type) == true ) {
			return NEBERROR_CALLBACKCANCEL;
		}
		service_checks.self++;
		return 0;

	case NEBTYPE_SERVICECHECK_PROCESSED:

		/* By default, check results are always sent to peers and masters */
		pkt->hdr.selection = DEST_PEERS_MASTERS;
		unexpire_service(s);

		/*
		 * We fiddle with the last_check time here so that the time
		 * shown in nagios.log (for a service alert, e.g) is the same
		 * as that in the report_data to avoid (user) confusion
		 */
		s->last_check = (time_t) ds->end_time.tv_sec;

		if ((merlin_sender && recv_event) &&
			(recv_event->hdr.type == NEBCALLBACK_SERVICE_CHECK_DATA ||
			recv_event->hdr.type == NEBCALLBACK_SERVICE_STATUS_DATA))
		{
			/*
			 * Check result was received over network.
			 * Return so we don't make packets bounce on the network or
			 * are sending duplicate packets to the daemon.
			 */
			set_service_check_node(merlin_sender, s, ds->check_type == CHECK_TYPE_PASSIVE);
			return 0;
		}

		set_service_check_node(&ipc, s, ds->check_type == CHECK_TYPE_PASSIVE);
		ret = send_service_status(pkt, ds->attr, ds->object_ptr);
		flush_notification();

		return ret;
	}

	return 0;
}

/**
 * Handle host check result from local node
 */
static int hook_host_result(merlin_event *pkt, void *data)
{
	int ret;
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;
	struct host *h = (struct host *)ds->object_ptr;
	struct merlin_node *node;

	switch (ds->type) {
	case NEBTYPE_HOSTCHECK_ASYNC_PRECHECK:
	case NEBTYPE_HOSTCHECK_SYNC_PRECHECK:
		node = pgroup_host_node(h->id);
		schedule_expiration_event(HOST_CHECK, node, h);
		if (node != &ipc) {
			/* We're not responsible, so block this check here */
			return NEBERROR_CALLBACKCANCEL;
		} else if (node_blocked_hostgroup(node, data, ds->type) == true ) {
			return NEBERROR_CALLBACKCANCEL;
		}
		host_checks.self++;
		return 0;

	/* only send processed host checks */
	case NEBTYPE_HOSTCHECK_PROCESSED:

		/* By default, check results are always sent to peers and masters */
		pkt->hdr.selection = DEST_PEERS_MASTERS;
		unexpire_host(h);

		/*
		 * We fiddle with the last_check time here so that the time
		 * shown in nagios.log (for a service alert, e.g) is the same
		 * as that in the report_data to avoid (user) confusion
		 */
		h->last_check = (time_t) ds->end_time.tv_sec;

		if ((merlin_sender && recv_event) &&
			(recv_event->hdr.type == NEBCALLBACK_HOST_CHECK_DATA ||
			recv_event->hdr.type == NEBCALLBACK_HOST_STATUS_DATA))
		{
			/*
			 * Check result was received over network.
			 * Return so we don't make packets bounce on the network or
			 * are sending duplicate packets to the daemon.
			 */
			set_host_check_node(merlin_sender, h, ds->check_type == CHECK_TYPE_PASSIVE);
			return 0;
		}

		set_host_check_node(&ipc, h, ds->check_type == CHECK_TYPE_PASSIVE);
		ret = send_host_status(pkt, ds->attr, ds->object_ptr);
		flush_notification();

		return ret;
	}

	return 0;
}

/*
 * Comments are buggy as hell from Nagios, so we must block
 * some of them and make others cause object status events
 * pass through unmolested, even if they're being checked
 * by a poller.
 */
static int hook_comment(merlin_event *pkt, void *data)
{
	nebstruct_comment_data *ds = (nebstruct_comment_data *)data;

	/*
	 * comments always generate two events. One add and one load.
	 * We must make sure to skip one of them, and so far, load
	 * seems to be the sanest one to keep
	 */
	if (ds->type == NEBTYPE_COMMENT_ADD)
		return 0;

	/* avoid sending network-triggered comment events */
	if (merlin_sender != NULL)
		return 0;

	/*
	 * Downtime is notoriously tricky to handle since there are so many
	 * commands for scheduling it. We propagate downtime commands, but
	 * not downtime comments (since commands generate comments).
	 */
	if (ds->entry_type == DOWNTIME_COMMENT && ds->type != NEBTYPE_COMMENT_DELETE) {
		pkt->hdr.code = MAGIC_NONET;
	}

	/*
	 * same for acknowledgements
	 */
	if (ds->entry_type == ACKNOWLEDGEMENT_COMMENT && ds->type != NEBTYPE_COMMENT_DELETE) {
		pkt->hdr.code = MAGIC_NONET;
	}

	/*
	 * if the reaper thread is adding the comment we're getting an
	 * event for now, we'll need to block that comment from being
	 * sent to the daemon to avoid pingpong action and duplicate
	 * entries in the database.
	 */
	if (pkt->hdr.code != MAGIC_NONET && block_comment &&
		block_comment->entry_type == ds->entry_type &&
		block_comment->comment_type == ds->comment_type &&
		block_comment->expires == ds->expires &&
		block_comment->persistent == ds->persistent &&
		!strcmp(block_comment->host_name, ds->host_name) &&
		!strcmp(block_comment->author_name, ds->author_name) &&
		!strcmp(block_comment->comment_data, ds->comment_data) &&
		(block_comment->service_description == ds->service_description ||
		 !strcmp(block_comment->service_description, ds->service_description)))
	{
		/*
		 * This avoids USER_COMMENT and FLAPPING_COMMENT entry_type
		 * comments from bouncing back and forth indefinitely
		 */
		ldebug("CMNT: Marking event with MAGIC_NONET");
		pkt->hdr.code = MAGIC_NONET;
	} else {
		if (block_comment) {
			ldebug("We have a block_comment, but it doesn't match");
		}
		pkt->hdr.selection = get_selection(ds->host_name);
	}

	return send_generic(pkt, data);
}

static int hook_downtime(merlin_event *pkt, void *data)
{
	nebstruct_downtime_data *ds = (nebstruct_downtime_data *)data;

	/* avoid sending network-triggered downtime events */
	if (merlin_sender)
		return 0;

	/*
	 * Downtime delete and stop events are transferred.
	 * Adding is done on all nodes from the downtime command
	 * that always gets transferred, but if a user cancels
	 * downtime early, we get a "delete" event with attribute
	 * NEBATTR_DOWNTIME_STOP_CANCELLED that we must transfer
	 * properly, or the other node (which might be notifying)
	 * will think the node is still in downtime.
	 */
	if (ds->attr == NEBATTR_DOWNTIME_STOP_CANCELLED)
		pkt->hdr.selection = get_selection(ds->host_name);
	else
		pkt->hdr.code = MAGIC_NONET;

	return send_generic(pkt, data);
}

static int get_cmd_selection(char *cmd, int hostgroup)
{
	char *semi_colon;
	int ret;

	/*
	 * only global commands have no arguments at all. Those
	 * shouldn't end up here, but if they do we forward them
	 * to peers and pollers
	 */
	if (!cmd) {
		ldebug("Global command [%s] ended up in get_cmd_selection()", cmd);
		return DEST_PEERS_POLLERS;
	}

	semi_colon = strchr(cmd, ';');
	if (semi_colon)
		*semi_colon = '\0';
	if (!hostgroup) {
		ret = get_selection(cmd);
	} else {
		ret = get_hostgroup_selection(cmd);
	}
	if (semi_colon)
		*semi_colon = ';';

	return ret;
}

static int hook_external_command(merlin_event *pkt, void *data)
{
	nebstruct_external_command_data *ds = (nebstruct_external_command_data *)data;
	int cb_result = NEB_OK;

	/*
	 * all comments generate two events, but we only want to
	 * send one of them, so focus on NEBTYPE_EXTERNALCOMMAND_START,
	 * since we need to be able to block the execution of the command
	 * in some cases where the command affects a single host or service.
	 */
	if (ds->type != NEBTYPE_EXTERNALCOMMAND_START)
		return NEB_OK;

	switch (ds->command_type) {
		/*
		 * Comments are handled by their respective comment
		 * events, so we mustn't forward them.
		 */
	case CMD_DEL_HOST_COMMENT:
	case CMD_DEL_SVC_COMMENT:
	case CMD_ADD_HOST_COMMENT:
	case CMD_ADD_SVC_COMMENT:
		return NEB_OK;

		/*
		 * These only contain the downtime id, so they're mostly useless,
		 * but potentially dangerous.
		 * We'll forward the downtime_delete event instead.
		 */
	case CMD_DEL_HOST_DOWNTIME:
	case CMD_DEL_SVC_DOWNTIME:
		return NEB_OK;

		/*
		 * these are forwarded and handled specially on the
		 * receiving end
		 */
	case CMD_ACKNOWLEDGE_HOST_PROBLEM:
	case CMD_ACKNOWLEDGE_SVC_PROBLEM:

		/*
		 * downtime is a troll of its own. For now, downtime
		 * commands aren't blocked, but their comments are.
		 * We keep them stashed here though, in case we
		 * want to modify how we handle them later.
		 */
	case CMD_SCHEDULE_HOST_DOWNTIME:
	case CMD_SCHEDULE_SVC_DOWNTIME:
	case CMD_SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME:
	case CMD_SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME:
		/* fallthrough */

	case CMD_ENABLE_SVC_CHECK:
	case CMD_DISABLE_SVC_CHECK:
	case CMD_SCHEDULE_SVC_CHECK:
	case CMD_DELAY_SVC_NOTIFICATION:
	case CMD_DELAY_HOST_NOTIFICATION:
	case CMD_ENABLE_HOST_SVC_CHECKS:
	case CMD_DISABLE_HOST_SVC_CHECKS:
	case CMD_SCHEDULE_HOST_SVC_CHECKS:
	case CMD_DELAY_HOST_SVC_NOTIFICATIONS:
	case CMD_DEL_ALL_HOST_COMMENTS:
	case CMD_DEL_ALL_SVC_COMMENTS:
	case CMD_ENABLE_SVC_NOTIFICATIONS:
	case CMD_DISABLE_SVC_NOTIFICATIONS:
	case CMD_ENABLE_HOST_NOTIFICATIONS:
	case CMD_DISABLE_HOST_NOTIFICATIONS:
	case CMD_ENABLE_HOST_SVC_NOTIFICATIONS:
	case CMD_DISABLE_HOST_SVC_NOTIFICATIONS:
	case CMD_ENABLE_PASSIVE_SVC_CHECKS:
	case CMD_DISABLE_PASSIVE_SVC_CHECKS:
	case CMD_ENABLE_HOST_EVENT_HANDLER:
	case CMD_DISABLE_HOST_EVENT_HANDLER:
	case CMD_ENABLE_SVC_EVENT_HANDLER:
	case CMD_DISABLE_SVC_EVENT_HANDLER:
	case CMD_ENABLE_HOST_CHECK:
	case CMD_DISABLE_HOST_CHECK:
	case CMD_START_OBSESSING_OVER_SVC_CHECKS:
	case CMD_STOP_OBSESSING_OVER_SVC_CHECKS:
	case CMD_REMOVE_HOST_ACKNOWLEDGEMENT:
	case CMD_REMOVE_SVC_ACKNOWLEDGEMENT:
	case CMD_SCHEDULE_FORCED_HOST_SVC_CHECKS:
	case CMD_SCHEDULE_FORCED_SVC_CHECK:
	case CMD_ENABLE_HOST_FLAP_DETECTION:
	case CMD_DISABLE_HOST_FLAP_DETECTION:
	case CMD_ENABLE_SVC_FLAP_DETECTION:
	case CMD_DISABLE_SVC_FLAP_DETECTION:
	case CMD_DISABLE_PASSIVE_HOST_CHECKS:
	case CMD_SCHEDULE_HOST_CHECK:
	case CMD_SCHEDULE_FORCED_HOST_CHECK:
	case CMD_CHANGE_HOST_EVENT_HANDLER:
	case CMD_CHANGE_SVC_EVENT_HANDLER:
	case CMD_CHANGE_HOST_CHECK_COMMAND:
	case CMD_CHANGE_SVC_CHECK_COMMAND:
	case CMD_CHANGE_NORMAL_HOST_CHECK_INTERVAL:
	case CMD_CHANGE_NORMAL_SVC_CHECK_INTERVAL:
	case CMD_CHANGE_RETRY_SVC_CHECK_INTERVAL:
	case CMD_CHANGE_MAX_HOST_CHECK_ATTEMPTS:
	case CMD_CHANGE_MAX_SVC_CHECK_ATTEMPTS:
	case CMD_ENABLE_HOST_AND_CHILD_NOTIFICATIONS:
	case CMD_DISABLE_HOST_AND_CHILD_NOTIFICATIONS:
	case CMD_ENABLE_HOST_FRESHNESS_CHECKS:
	case CMD_DISABLE_HOST_FRESHNESS_CHECKS:
	case CMD_SET_HOST_NOTIFICATION_NUMBER:
	case CMD_SET_SVC_NOTIFICATION_NUMBER:
	case CMD_CHANGE_HOST_CHECK_TIMEPERIOD:
	case CMD_CHANGE_SVC_CHECK_TIMEPERIOD:
	case CMD_CHANGE_CUSTOM_HOST_VAR:
	case CMD_CHANGE_CUSTOM_SVC_VAR:
	case CMD_ENABLE_CONTACT_HOST_NOTIFICATIONS:
	case CMD_DISABLE_CONTACT_HOST_NOTIFICATIONS:
	case CMD_ENABLE_CONTACT_SVC_NOTIFICATIONS:
	case CMD_DISABLE_CONTACT_SVC_NOTIFICATIONS:
	case CMD_ENABLE_CONTACTGROUP_HOST_NOTIFICATIONS:
	case CMD_DISABLE_CONTACTGROUP_HOST_NOTIFICATIONS:
	case CMD_ENABLE_CONTACTGROUP_SVC_NOTIFICATIONS:
	case CMD_DISABLE_CONTACTGROUP_SVC_NOTIFICATIONS:
	case CMD_CHANGE_RETRY_HOST_CHECK_INTERVAL:
	case CMD_CHANGE_HOST_NOTIFICATION_TIMEPERIOD:
	case CMD_CHANGE_SVC_NOTIFICATION_TIMEPERIOD:
	case CMD_CHANGE_CONTACT_HOST_NOTIFICATION_TIMEPERIOD:
	case CMD_CHANGE_CONTACT_SVC_NOTIFICATION_TIMEPERIOD:
	case CMD_CHANGE_HOST_MODATTR:
	case CMD_CHANGE_SVC_MODATTR:
	case CMD_DEL_DOWNTIME_BY_HOST_NAME:
		/*
		 * looks like we have everything we need, so get the
		 * selection based on the hostname so the daemon knows
		 * which node(s) to send the command to (could very well
		 * be 'nowhere')
		 */
		if (!merlin_sender)
			pkt->hdr.selection = get_cmd_selection(ds->command_args, 0);
		break;

	case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
	case CMD_PROCESS_HOST_CHECK_RESULT:
		if (!merlin_sender) {
			/* Send to correct node */
			pkt->hdr.selection = get_cmd_selection(ds->command_args, 0);
		}
		/*
		 * Processing check results should only be done by the node owning the
		 * object. Thus, forward to all nodes, but execute it only on the node
		 * owning the object.
		 */
		{
			merlin_node *node;
			char *delim;
			host *this_host;

			delim = strchr(ds->command_args, ';');
			if(delim == NULL) {
				/*
				 * invalid arguments, we shouldn't do anything, but naemon can
				 * result in error later
				 */
				break;
			}

			this_host = find_host(strndupa(ds->command_args, delim - ds->command_args));
			if(this_host == NULL) {
				/*
				 * Unknown host. Thus, nothing we know that we should handle.
				 * Thus block it.
				 */
				cb_result = NEBERROR_CALLBACKCANCEL;
				break;
			}

			node = pgroup_host_node(this_host->id);
			/* If we are handling a custom notification, then we need a few extra
 		 	 * checks to ensure notifications are sent by the master if the
 		 	 * responsible node has notifies=no.
			 */
			if (ds->command_type == CMD_SEND_CUSTOM_HOST_NOTIFICATION) {
				/* if we are a poller, and have masters online, and don't notify */
				if (online_masters && !(ipc.flags & MERLIN_NODE_NOTIFIES)) {
					cb_result = NEBERROR_CALLBACKCANCEL;
				}
				/* Handle notification if we own the object */
				else if (node == &ipc) {
					return NEB_OK;
				}
				/* If a poller (not us) owns the object, either:
				 * Another peer handles the notification
				 * Another poller handles the notication
				 * Or we handle it here
				 */
				else if (node->type == MODE_POLLER) {
					if ((num_peers == 0 || should_run_check(this_host->id)) &&
							pgroup_num_notify(node) == 0) {
						return NEB_OK;
					} else {
						cb_result = NEBERROR_CALLBACKCANCEL;
					}
				}
			}
			else if (node != &ipc) {
				/* We're not responsible, so block this command here */
				cb_result = NEBERROR_CALLBACKCANCEL;
			}
			break;
		}

	case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
	case CMD_PROCESS_SERVICE_CHECK_RESULT:
		if (!merlin_sender) {
			/* Send to correct node */
			pkt->hdr.selection = get_cmd_selection(ds->command_args, 0);
		}
		/*
		 * Processing check results should only be done by the node owning the
		 * object. Thus, forward to all nodes, but execute it only on the node
		 * owning the object.
		 */
		{
			merlin_node *node;
			char *delim_host;
			char *delim_service;
			char *host_name;
			char *service_description;
			service *this_service;

			delim_host = strchr(ds->command_args, ';');
			if(delim_host == NULL) {
				break;
			}
			host_name = strndupa(ds->command_args, delim_host - ds->command_args);

			delim_service = strchr(delim_host+1, ';');
			if(delim_service == NULL) {
				break;
			}
			service_description = strndupa(delim_host+1, delim_service - (delim_host+1));

			this_service = find_service(host_name, service_description);
			if(this_service == NULL) {
				/*
				 * Unknown service. Thus, nothing we know that we should handle.
				 * Thus block it.
				 */
				cb_result = NEBERROR_CALLBACKCANCEL;
				break;
			}

			node = pgroup_service_node(this_service->id);
			/* If we are handling a custom notification, then we need a few extra
 		 	 * checks to ensure notifications are sent by the master if the
 		 	 * responsible node has notifies=no.
			 */
			if (ds->command_type == CMD_SEND_CUSTOM_SVC_NOTIFICATION) {
				/* if we are a poller, and have masters online, and don't notify */
				if (online_masters && !(ipc.flags & MERLIN_NODE_NOTIFIES)) {
					cb_result = NEBERROR_CALLBACKCANCEL;
				}
				/* Handle notification if we own the object */
				else if (node == &ipc) {
					return NEB_OK;
				}
				/* If a poller (not us) owns the object, either:
				 * Another peer handles the notification
				 * Another poller handles the notication
				 * Or we handle it here
				 */
				else if (node->type == MODE_POLLER) {
					if ((num_peers == 0 || should_run_check(this_service->id)) &&
							pgroup_num_notify(node) == 0) {
						return NEB_OK;
					} else {
						cb_result = NEBERROR_CALLBACKCANCEL;
					}
				}
			}
			else if (node != &ipc) {
				/* We're not responsible, so block this command here */
				cb_result = NEBERROR_CALLBACKCANCEL;
			}
			break;
		}

	/* XXX downtime stuff on top */
	/*
	 * service- and hostgroup commands get sent to all peers
	 * and pollers, but not to masters since we can't know if
	 * we'd affect more than our fair share of services on the
	 * master.
	 */
	case CMD_SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
	case CMD_SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
	case CMD_ENABLE_HOSTGROUP_SVC_NOTIFICATIONS:
	case CMD_DISABLE_HOSTGROUP_SVC_NOTIFICATIONS:
	case CMD_ENABLE_HOSTGROUP_HOST_NOTIFICATIONS:
	case CMD_DISABLE_HOSTGROUP_HOST_NOTIFICATIONS:
	case CMD_ENABLE_HOSTGROUP_SVC_CHECKS:
	case CMD_DISABLE_HOSTGROUP_SVC_CHECKS:
	case CMD_ENABLE_HOSTGROUP_HOST_CHECKS:
	case CMD_DISABLE_HOSTGROUP_HOST_CHECKS:
	case CMD_ENABLE_HOSTGROUP_PASSIVE_SVC_CHECKS:
	case CMD_DISABLE_HOSTGROUP_PASSIVE_SVC_CHECKS:
	case CMD_ENABLE_HOSTGROUP_PASSIVE_HOST_CHECKS:
	case CMD_DISABLE_HOSTGROUP_PASSIVE_HOST_CHECKS:
		if (!merlin_sender)
			pkt->hdr.selection = get_cmd_selection(ds->command_args, 1);
		break;
	case CMD_SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
	case CMD_SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
	case CMD_ENABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
	case CMD_DISABLE_SERVICEGROUP_SVC_NOTIFICATIONS:
	case CMD_ENABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
	case CMD_DISABLE_SERVICEGROUP_HOST_NOTIFICATIONS:
	case CMD_ENABLE_SERVICEGROUP_SVC_CHECKS:
	case CMD_DISABLE_SERVICEGROUP_SVC_CHECKS:
	case CMD_ENABLE_SERVICEGROUP_HOST_CHECKS:
	case CMD_DISABLE_SERVICEGROUP_HOST_CHECKS:
	case CMD_ENABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS:
	case CMD_DISABLE_SERVICEGROUP_PASSIVE_SVC_CHECKS:
	case CMD_ENABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS:
	case CMD_DISABLE_SERVICEGROUP_PASSIVE_HOST_CHECKS:
		if (num_masters) {
			linfo("Submitting servicegroup commands on pollers isn't necessarily a good idea");
		}
		if (!merlin_sender)
			pkt->hdr.selection = DEST_PEERS_POLLERS;
		break;

	default:
		/*
		 * global commands get filtered in the daemon so only
		 * peers and pollers get them, but we block them right
		 * here if we have neither of those
		 */
		if (!(num_peers + num_pollers)) {
			ldebug("No peers or pollers. Not sending command %d anywhere",
			       ds->command_type);
			return 0;
		}

		if (!merlin_sender) {
			pkt->hdr.selection = DEST_PEERS_POLLERS;
		}
		break;
	}

	if (merlin_sender)
		pkt->hdr.code = MAGIC_NONET;

	if(0 != send_generic(pkt, data)) {
		ldebug("Can't send merlin packet for command %d",
		       ds->command_type);
	}

	return cb_result;
}

static int hook_contact_notification_method(merlin_event *pkt, void *data)
{
	nebstruct_contact_notification_method_data *ds =
		(nebstruct_contact_notification_method_data *)data;

	if (ds->type != NEBTYPE_CONTACTNOTIFICATIONMETHOD_END)
		return 0;

	/* Notifications should be broadcasted for logging, but only to peers and masters. */
	pkt->hdr.selection = DEST_PEERS_MASTERS;

	return send_generic(pkt, data);
}

/**
 * @brief Callback for NEBCALLBACK_EVENT_HANDLER events
 *
 * When an NEBCALLBACK_EVENT_HANDLER event occurs this is a callback used to
 * determine wether or not the local node should run the event handler.
 *
 * @return NEB_OK on OK to proceed with event handler execution
 * @return NEBERROR_CALLBACKOVERRIDE when event handler should not be executed
 */
static int hook_event_handler(merlin_event *pkt, void *data)
{
	nebstruct_event_handler_data *ds = (nebstruct_event_handler_data *)data;
	if (ds->type == NEBTYPE_EVENTHANDLER_START) {

		merlin_node *owning_node;

		if (ds->eventhandler_type == SERVICE_EVENTHANDLER || ds->eventhandler_type == GLOBAL_SERVICE_EVENTHANDLER) {
			struct service *svc = (struct service *)ds->object_ptr;
			owning_node = pgroup_service_node(svc->id);
			ldebug("event_handler: Checking event_handler for service %s;%s",
					svc->description, svc->host_name);
		} else {
			struct host *hst = (struct host *)ds->object_ptr;
			owning_node = pgroup_host_node(hst->id);
			ldebug("event_handler: Checking event_handler for host %s",
					hst->name);
		}

		if (owning_node == &ipc) {
			ldebug("event_handler: Local node is owner, running event handler");
			return NEB_OK;
		} else {
			ldebug("event_handler: Owning node is %s (%s), not running event handler locally",
					owning_node->name,
					owning_node->type == MODE_POLLER ? "poller" : "peer");
			return NEBERROR_CALLBACKOVERRIDE;
		}
	}
	return NEB_OK;
}

/*
 * Called when a notification chain starts. This is used to
 * avoid sending notifications from a node that isn't supposed
 * to send it
 */
static neb_cb_result * hook_notification(merlin_event *pkt, void *data)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)data;
	struct merlin_notify_stats *notif_stats = NULL;
	merlin_node *owning_node = NULL;
	struct host *hst;
	struct service *svc;
	unsigned int object_id;
	int check_type;

	if (ds->type == NEBTYPE_NOTIFICATION_END) {

		int ret = 0;

		/* Always propagate results to peers and masters */
		pkt->hdr.selection = DEST_PEERS_MASTERS;

		if (ds->notification_type == HOST_NOTIFICATION) {
			hst = (struct host *)ds->object_ptr;
			ds->object_ptr = (void *)(uintptr_t)(hst->current_notification_number);
			ds->start_time.tv_usec = hst->no_more_notifications;
			ds->start_time.tv_sec = hst->last_notification;
			ds->end_time.tv_usec = 0;
			ds->end_time.tv_sec = hst->next_notification;
		} else if (ds->notification_type == SERVICE_NOTIFICATION) {
			svc = (struct service *)ds->object_ptr;
			ds->object_ptr = (void *)(uintptr_t)(svc->current_notification_number);
			ds->start_time.tv_usec = svc->no_more_notifications;
			ds->start_time.tv_sec = svc->last_notification;
			ds->end_time.tv_usec = 0;
			ds->end_time.tv_sec = svc->next_notification;
		} else {
			lerr("Unknown notification type %i", ds->notification_type);
		}

		/*
		 * If it is a custom notification it should always be sent directly
		 * because we won't have a pending check result waiting to be sent.
		 * The same goes for when we've ended up here as a result of a received
		 * merlin event. In this case, if a poller sends a check result which
		 * generates a notification that we're responsible for, we notify and
		 * let fellow nodes know that we've notified directly.
		 * Otherwise, we're are to expect a check result to be sent to fellow
		 * nodes directly after and we don't want it to overwrite the data
		 * sent in the notification packet, so we hold the notification packet
		 * until next check result is sent.
		 */
		if(ds->reason_type == NOTIFICATION_CUSTOM || (merlin_sender && recv_event)) {
			if (merlin_sender && recv_event && recv_event->hdr.type != NEBCALLBACK_EXTERNAL_COMMAND_DATA) {
				pkt->hdr.selection = get_sel_id(merlin_sender->hostgroups);
			}
			ret = send_generic(pkt, data);
		} else {
			ret = hold_notification_packet(pkt, ds);
		}

		return neb_cb_result_create(ret);
	}

	/* don't count or (try to) block notifications after they're sent */
	if (ds->type != NEBTYPE_NOTIFICATION_START)
		return 0;

	if (ds->notification_type == HOST_NOTIFICATION){
		hst = ds->object_ptr;
		object_id = hst->id;
		check_type = hst->check_type;
		owning_node = pgroup_host_node(hst->id);
		ldebug("notif: Checking host notification for %s", hst->name);
	} else {
		svc = ds->object_ptr;
		object_id = svc->id;
		check_type = svc->check_type;
		owning_node = pgroup_service_node(svc->id);
		ldebug("notif: Checking service notification for %s;%s",
			   svc->host_name, svc->description);
	}
	notif_stats = &merlin_notify_stats[ds->reason_type][ds->notification_type][check_type];

	/*
	 * If we're a poller and we have set "notifies = no" in our configuration
	 * AND we currently have one or more masters online, we should NOT notify.
	 */
	if (online_masters && !(ipc.flags & MERLIN_NODE_NOTIFIES)) {
		ldebug("notif: Poller blocking notification in favour of master");
		notif_stats->master++;
		return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
				"Notification will be handled by master(s)");
	}

	/* Figure out who should handle the notification */
	if (owning_node == &ipc) {
		ldebug("notif: Local node is owner, allowing notification");
		notif_stats->sent++;
		return neb_cb_result_create(0);
	} else if (owning_node->type == MODE_POLLER) {
		if (owning_node->flags & MERLIN_NODE_NOTIFIES) {
			ldebug("notif: Poller can notify. Cancelling notification");
			notif_stats->poller++;
			return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
				"Notification will be handled by a poller (%s)",
				owning_node->name);
		} else {
			/* Poller can't notify, who should handle it instead? */
			if ((num_peers == 0 || should_run_check(object_id)) &&
					pgroup_num_notify(owning_node) == 0 ) {
				notif_stats->sent++;
				ldebug("notif: Poller can't notify, notifying in its place");
				return neb_cb_result_create(0);
			} else {
				ldebug("notif: Poller can't notify, a peer will notify");
				notif_stats->peer++;
				return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
					"Poller can't notify, a peer will notify");
			}
		}
	} else if (owning_node->type == MODE_PEER) {
		ldebug("notif: Cancelling notification, a peer (%s) handles it",
			owning_node->name);
		notif_stats->peer++;
		return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
			"Notification will be handled by a peer (%s)", owning_node->name);
	}

	ldebug("notif: Unable to determine who should notify - allowing");
	notif_stats->sent++;
	return neb_cb_result_create(0);
}

neb_cb_result * merlin_mod_hook(int cb, void *data)
{
	merlin_event pkt;
	int result = 0;
	neb_cb_result *neb_result = NULL;
	static time_t last_pulse = 0, last_flood_warning = 0;
	time_t now;

	if (!data) {
		lerr("eventbroker module called with NULL data");
		return neb_cb_result_create(-1);
	} else if (cb < 0 || cb > NEBCALLBACK_NUMITEMS) {
		lerr("merlin_mod_hook() called with invalid callback id");
		return neb_cb_result_create(-1);
	}

	/*
	 * must reset this here so events we don't check for
	 * dupes are always sent properly
	 */
	check_dupes = 0;

	/* self-heal nodes that have missed out on the fact that we're up */
	now = time(NULL);
	if(!last_pulse || now - last_pulse > 15)
		node_send_ctrl_active(&ipc, CTRL_GENERIC, &ipc.info);
	last_pulse = now;

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.type = cb;
	pkt.hdr.selection = DEST_BROADCAST;
	switch (cb) {
	case NEBCALLBACK_NOTIFICATION_DATA:
		neb_result = hook_notification(&pkt, data);
		break;

	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		result = hook_contact_notification_method(&pkt, data);
		break;

	case NEBCALLBACK_HOST_CHECK_DATA:
		result = hook_host_result(&pkt, data);
		break;

	case NEBCALLBACK_SERVICE_CHECK_DATA:
		result = hook_service_result(&pkt, data);
		break;

	case NEBCALLBACK_COMMENT_DATA:
		result = hook_comment(&pkt, data);
		break;

	case NEBCALLBACK_DOWNTIME_DATA:
		result = hook_downtime(&pkt, data);
		break;

	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		result = hook_external_command(&pkt, data);
		break;

	case NEBCALLBACK_FLAPPING_DATA:
		/*
		 * flapping doesn't go to the network. check processing
		 * will generate flapping alerts on all nodes anyway,
		 */
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
	case NEBCALLBACK_PROCESS_DATA:
		/* these make no sense to ship across the wire */
		pkt.hdr.code = MAGIC_NONET;
		result = send_generic(&pkt, data);
		break;

	case NEBCALLBACK_HOST_STATUS_DATA:
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		/*
		 * Don't handle status updates coming from Naemon.
		 * If we need to send status updates for any reason it is done through
		 * Merlin directly. For normal state updates, we let each node handle
		 * check results so they keep their own state.
		 */
		break;
	case NEBCALLBACK_EVENT_HANDLER_DATA:
		result = hook_event_handler(&pkt, data);
		break;
	default:
		lerr("Unhandled callback '%s' in merlin_hook()", callback_name(cb));
	}

	if (neb_result != NULL) {
		/*
		 * We have a rich callback result, propagate return code
		 * to preserve flood warnings
		 */
		result = neb_cb_result_returncode(neb_result);
	}
	else {
		/*
		 * No rich callback result, create one
		 */
		neb_result = neb_cb_result_create_full(result, "No callback result description available");
	}

	if (result < 0 && now - last_flood_warning > 30) {
		/* log a warning every 30 seconds */
		last_flood_warning = now;
		lwarn("Daemon is flooded and backlogging failed");
	}


	return neb_result;
}

#define DEST_DB 1
#define DEST_NETWORK 2
#define CB_ENTRY(dest, type, hook) \
	{ dest, type, #type, #hook }
static struct callback_struct {
	int dest;
	int type;
	const char *name;
	const char *hook_name;
} callback_table[] = {
	CB_ENTRY(0, NEBCALLBACK_PROCESS_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_EVENT_HANDLER_DATA, hook_generic),
	CB_ENTRY(DEST_NETWORK, NEBCALLBACK_NOTIFICATION_DATA, hook_notification),
	CB_ENTRY(0, NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, hook_contact_notification_method),
	CB_ENTRY(0, NEBCALLBACK_SERVICE_CHECK_DATA, hook_service_result),
	CB_ENTRY(0, NEBCALLBACK_HOST_CHECK_DATA, hook_host_result),
	CB_ENTRY(0, NEBCALLBACK_COMMENT_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_DOWNTIME_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_FLAPPING_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_PROGRAM_STATUS_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_HOST_STATUS_DATA, hook_host_status),
	CB_ENTRY(0, NEBCALLBACK_SERVICE_STATUS_DATA, hook_service_status),
	CB_ENTRY(DEST_NETWORK, NEBCALLBACK_EXTERNAL_COMMAND_DATA, hook_generic),
};

int merlin_hooks_init(uint32_t mask)
{
	uint i;
	ev_mask = mask;

	if (!use_database && !num_nodes) {
		ldebug("Not using database and no nodes configured. Ignoring all events");
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		if (cb->dest == DEST_DB && !use_database) {
			ldebug("Not using database. Ignoring %s events", callback_name(cb->type));
			continue;
		}
		if (cb->dest == DEST_NETWORK && !num_nodes) {
			ldebug("No nodes configured. Ignoring %s events", callback_name(cb->type));
			continue;
		}

		/* ignore filtered-out eventtypes */
		if (!(mask & (1 << cb->type))) {
			ldebug("EVENTFILTER: Ignoring %s events", callback_name(cb->type));
			continue;
		}

		neb_register_callback_full(cb->type, neb_handle, 0, NEB_API_VERSION_2, merlin_mod_hook);
	}

	return 0;
}

/*
 * We ignore any event masks here. Nagios should handle a module
 * unloading a function it hasn't registered gracefully anyways.
 */
int merlin_hooks_deinit(void)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];
		neb_deregister_callback(cb->type, merlin_mod_hook);
	}

	return 0;
}

void merlin_set_block_comment(nebstruct_comment_data *cmnt)
{
	block_comment = cmnt;
}
