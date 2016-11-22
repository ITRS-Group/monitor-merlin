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

	if (obj == merlin_recv_host)
		return 0;

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
	merlin_encode_event(pkt, &st_obj);
	pkt->hdr.selection = DEST_PEERS_MASTERS;

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
	pkt->hdr.selection = DEST_PEERS_MASTERS;

	return send_generic(pkt, &st_obj);
}

static inline int should_run_check(unsigned int id)
{
	return assigned_peer(id, ipc.info.active_peers + 1) == ipc.peer_id;
}

/**
 * The hooks are called from broker.c in Nagios.
 * Handle service check result from local node. Should not be used from network
 */
static int hook_service_result(merlin_event *pkt, void *data)
{
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
		}
		service_checks.self++;
		return 0;

	case NEBTYPE_SERVICECHECK_PROCESSED:
		unexpire_service(s);
		set_service_check_node(&ipc, s, ds->check_type == CHECK_TYPE_PASSIVE);
		return 0;
	}

	return 0;
}

/**
 * Handle host check result from local node. Should not be used from network
 */
static int hook_host_result(merlin_event *pkt, void *data)
{
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
		}
		host_checks.self++;
		return 0;

	/* only send processed host checks */
	case NEBTYPE_HOSTCHECK_PROCESSED:
		unexpire_host(h);
		set_host_check_node(&ipc, h, ds->check_type == CHECK_TYPE_PASSIVE);
		return 0;
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
			if (node != &ipc) {
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
			if (node != &ipc) {
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

		if (!merlin_sender)
			pkt->hdr.selection = DEST_PEERS_POLLERS;
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

/*
 * The only node that should do any real update on the objects is the node
 * owning the objects. Thus, it should only be sent from one node anyway.
 */
static int hook_host_status(merlin_event *pkt, void *data)
{
	nebstruct_host_status_data *ds = (nebstruct_host_status_data *)data;
	merlin_node *node;
	/* Only owning node is allowed to send status updates */
	node = pgroup_host_node(((host *)ds->object_ptr)->id);
	if (node == &ipc) {
		return send_host_status(pkt, ds->attr, ds->object_ptr);
	}
	return 0;
}

static int hook_service_status(merlin_event *pkt, void *data)
{
	nebstruct_service_status_data *ds = (nebstruct_service_status_data *)data;
	merlin_node *node;
	/* Only owning node is allowed to send status updates */
	node = pgroup_service_node(((service *)ds->object_ptr)->id);
	if (node == &ipc) {
		return send_service_status(pkt, ds->attr, ds->object_ptr);
	}
	return 0;
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

/*
 * Called when a notification chain starts. This is used to
 * avoid sending notifications from a node that isn't supposed
 * to send it
 */
static neb_cb_result * hook_notification(merlin_event *pkt, void *data)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)data;
	unsigned int id, check_type = 0, rtype;
	struct merlin_notify_stats *mns = NULL;
	struct service *s = NULL;
	struct host *h = NULL;
	const char *owning_node_name = NULL;

	if (ds->type == NEBTYPE_NOTIFICATION_END){

		/* Always propagate results to peers and masters */
		pkt->hdr.selection = DEST_PEERS_MASTERS;
		int ret = 0;

		if (ds->notification_type == HOST_NOTIFICATION) {
			host *hst = ds->object_ptr;
			ds->object_ptr = (void *)(uintptr_t)(hst->current_notification_number);
			ds->start_time.tv_usec = hst->no_more_notifications;
			ds->start_time.tv_sec = hst->last_notification;
			ds->end_time.tv_usec = 0;
			ds->end_time.tv_sec = hst->next_notification;
		} else if (ds->notification_type == SERVICE_NOTIFICATION) {
			service *svc = ds->object_ptr;
			ds->object_ptr = (void *)(uintptr_t)(svc->current_notification_number);
			ds->start_time.tv_usec = svc->no_more_notifications;
			ds->start_time.tv_sec = svc->last_notification;
			ds->end_time.tv_usec = 0;
			ds->end_time.tv_sec = svc->next_notification;
		} else {
			lerr("Unknown notification type %i", ds->notification_type);
		}

		ret = send_generic(pkt, data);

		if (ret == NEBERROR_CALLBACKCANCEL || ret == NEBERROR_CALLBACKOVERRIDE) {
			const char *err_type = ret == NEBERROR_CALLBACKCANCEL ? "cancel" : "override";
			lerr("Possible bug! Return from send_generic() triggered %s for notification end", err_type);
			return neb_cb_result_create_full(ret,
					"This might be a bug! Return from send_generic() triggered %s for notification end",
					err_type);
		}
		else {
			return neb_cb_result_create(ret);
		}
	}

	/* don't count or (try to) block notifications after they're sent */
	if (ds->type != NEBTYPE_NOTIFICATION_START)
		return 0;

	if (ds->notification_type == SERVICE_NOTIFICATION) {
		s = (service *)ds->object_ptr;
		check_type = s->check_type;
		id = s->id;
		ldebug("notif: Checking service notification for %s;%s",
		       s->host_name, s->description);
	} else {
		h = (struct host *)ds->object_ptr;
		id = h->id;
		check_type = h->check_type;
		ldebug("notif: Checking host notification for %s", h->name);
	}

	if (node_by_id(assigned_peer(id, ipc.info.active_peers + 1)) != NULL) {
		owning_node_name = node_by_id(assigned_peer(id,
				ipc.info.active_peers + 1 /* number of active peers plus self */
				))->name;
	} else {
		owning_node_name = "<unknown>";
	}

	/* handle NOTIFICATION_CUSTOM being 99 in some releases */
	rtype = ds->reason_type;
	if (rtype > 8)
		rtype = 8;
	mns = &merlin_notify_stats[rtype][ds->notification_type][check_type];

	/* Break out if we only notify when no masters are present and we have masters */
	if (online_masters && !(ipc.flags & MERLIN_NODE_NOTIFIES)) {
		ldebug("notif: poller blocking notification in favour of master");
		mns->master++;
		return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
				"Notification will be handled by master(s)");
	}

	/*
	 * network-received events can go one of two ways:
	 * If the sender is a poller that can't notify on its own, we may
	 * have to send the notification, unless one of our peers is
	 * supposed to do it.
	 * If the sender is not a poller, we should handle the notification if we
	 * are responsible for the check of that object, as usual
	 */
	if (merlin_sender) {
		ldebug("notif: merlin_sender is %s %s", node_type(merlin_sender), merlin_sender->name);
		ldebug("notif: merlin_sender->flags: %d", merlin_sender->flags);
		if (merlin_sender->type == MODE_POLLER && merlin_sender->flags & MERLIN_NODE_NOTIFIES) {
			ldebug("notif: Poller can notify. Cancelling notification");

			return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
					"Notification will be handled by a poller (%s)", merlin_sender->name);
		}

		/*
		 * Check if we should do it and, if so, allow it
		 */
		if ((num_peers == 0 || should_run_check(id))) {
			mns->sent++;
			if(merlin_sender->type == MODE_POLLER) {
				ldebug("notif: Poller can't notify and we're responsible, so notifying");
			} else {
				ldebug("notif: We're responsible, so notifying");
			}
			return neb_cb_result_create(0);
		}

		ldebug("notif: A peer handles poller-sent check. Blocking notifications");
		mns->peer++;

		return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
				"Notification originating on poller (%s) will be handled by another peer (%s)",
				merlin_sender->name,
				owning_node_name);
	}

	/* never block normal, local notificatons from passive checks */
	if (check_type == CHECK_TYPE_PASSIVE && ds->reason_type == NOTIFICATION_NORMAL) {
		ldebug("notif: passive check delivered to us, so we notify");
		mns->sent++;
		return neb_cb_result_create(0);
	}

	/* if we have no peers we won't block the notification at this point */
	if (!num_peers) {
		ldebug("notif: We have no peers, so won't block notification");
		mns->sent++;
		return neb_cb_result_create(0);
	}

	/*
	 * command-triggered notifications are sent immediately
	 * from the node where they originated and blocked
	 * everywhere else
	 */
	switch (ds->reason_type) {
	case NOTIFICATION_ACKNOWLEDGEMENT:
	case NOTIFICATION_CUSTOM:
		ldebug("notif: command-triggered and delivered to us, so allowing");
		mns->sent++;
		return neb_cb_result_create(0);
	}

	if (!num_peers || should_run_check(id)) {
		ldebug("notif: We're responsible for this notification, so allowing it");
		return neb_cb_result_create(0);
	} else {
		ldebug("notif: Blocking notification. A peer is supposed to send it");
		mns->peer++;

		return neb_cb_result_create_full(NEBERROR_CALLBACKCANCEL,
				"A peer (%s) is supposed to send this notification", owning_node_name);
	}

	mns->sent++;
	ldebug("notif: Fell through to the end");
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
		result = hook_host_status(&pkt, data);
		break;
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		result = hook_service_status(&pkt, data);
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
/*
	CB_ENTRY(0, NEBCALLBACK_LOG_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_SYSTEM_COMMAND_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_EVENT_HANDLER_DATA, hook_generic),
*/
	CB_ENTRY(DEST_NETWORK, NEBCALLBACK_NOTIFICATION_DATA, hook_notification),
/*	CB_ENTRY(0, NEBCALLBACK_CONTACT_NOTIFICATION_DATA, hook_contact_notification),
 */
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
