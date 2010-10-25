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
#include <execinfo.h>
#include "module.h"
#include "nagios/objects.h"
#include "nagios/neberrors.h"
#include "nagios/common.h"

static int check_dupes;
static merlin_event last_pkt;
static unsigned long long dupes, dupe_bytes;

struct check_stats {
	unsigned long long poller, peer, self;
};
static struct check_stats service_checks, host_checks;

#ifdef DEBUG_DUPES_CAREFULLY
#define mos_case(vname) \
	if (offset >= offsetof(monitored_object_state, vname) && \
	    offset < offsetof(monitored_object_state, vname) + sizeof(mss.vname)) \
			return #vname
static const char *mos_offset_name(int offset)
{
	monitored_object_state mss;

	if (offset >= sizeof(monitored_object_state)) {
		return "string area";
	}

	mos_case(initial_state);
	mos_case(flap_detection_enabled);
	mos_case(low_flap_threshold);
	mos_case(high_flap_threshold);
	mos_case(check_freshness);
	mos_case(freshness_threshold);
	mos_case(process_performance_data);
	mos_case(checks_enabled);
	mos_case(accept_passive_checks);
	mos_case(event_handler_enabled);
	mos_case(obsess);
	mos_case(problem_has_been_acknowledged);
	mos_case(acknowledgement_type);
	mos_case(check_type);
	mos_case(current_state);
	mos_case(last_state);
	mos_case(last_hard_state);
	mos_case(state_type);
	mos_case(current_attempt);
	mos_case(current_event_id);
	mos_case(last_event_id);
	mos_case(current_problem_id);
	mos_case(last_problem_id);
	mos_case(latency);
	mos_case(execution_time);
	mos_case(notifications_enabled);
	mos_case(last_notification);
	mos_case(next_notification);
	mos_case(next_check);
	mos_case(should_be_scheduled);
	mos_case(last_check);
	mos_case(last_state_change);
	mos_case(last_hard_state_change);
	mos_case(last_time_up);
	mos_case(last_time_down);
	mos_case(last_time_unreachable);
	mos_case(has_been_checked);
	mos_case(current_notification_number);
	mos_case(current_notification_id);
	mos_case(check_flapping_recovery_notification);
	mos_case(scheduled_downtime_depth);
	mos_case(pending_flex_downtime);
	mos_case(is_flapping);
	mos_case(flapping_comment_id);
	mos_case(percent_state_change);
	mos_case(plugin_output);
	mos_case(long_plugin_output);
	mos_case(perf_data);

	return NULL;
}
#endif

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

#ifdef DEBUG_DUPES_CAREFULLY
	/*
	 * The "near-dupes" detection only works for host and
	 * service status events for now, so return early if
	 * the event type is neither of those
	 */
	if (pkt->hdr.type == NEBCALLBACK_SERVICE_STATUS_DATA ||
		pkt->hdr.type == NEBCALLBACK_HOST_STATUS_DATA)
	{
		int i, diffbytes = 0, diffvars = 0;
		const char *name, *last_name = NULL;
		unsigned char *a, *b;

		lwarn("Near-duplicate %s event created by Nagios",
			  callback_name(pkt->hdr.type));

		a = (unsigned char *)&last_pkt;
		b = (unsigned char *)pkt;
		for (i = 0; i < packet_size(pkt); i++) {
			if (a[i] == b[i])
				continue;
			diffbytes++;
			name = mos_offset_name(i);
			if (name && name == last_name)
				continue;
			diffvars++;
			ldebug("%s differs", name);
			last_name = name;
		}
		ldebug("%d variables and %d bytes differ", diffvars, diffbytes);
	}
#endif

	return 0;
}

static int send_generic(merlin_event *pkt, void *data)
{
	int result;

	pkt->hdr.len = merlin_encode_event(pkt, data);
	if (!pkt->hdr.len) {
		lerr("Header len is 0 for callback %d. Update offset in hookinfo.h", pkt->hdr.type);
		return -1;
	}

	if (is_dupe(pkt)) {
		return 0;
	}

	/*
	 * preserve the event so we can check for dupes,
	 * but only if we successfully sent it
	 */
	result = ipc_send_event(pkt);
	if (result < 0)
		memset(&last_pkt, 0, sizeof(last_pkt));
	else
		memcpy(&last_pkt, pkt, packet_size(pkt));

	return result;
}

static int get_selection(const char *key)
{
	node_selection *sel = node_selection_by_hostname(key);

	return sel ? sel->id & 0xffff : CTRL_GENERIC;
}

static int send_host_status(merlin_event *pkt, struct host_struct *obj)
{
	merlin_host_status st_obj;
	static struct host_struct *last_obj = NULL;

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

	MOD2NET_STATE_VARS(st_obj.state, obj);
	st_obj.state.last_notification = obj->last_host_notification;
	st_obj.state.next_notification = obj->next_host_notification;
	st_obj.state.accept_passive_checks = obj->accept_passive_host_checks;
	st_obj.state.obsess = obj->obsess_over_host;
	st_obj.name = obj->name;

	pkt->hdr.selection = get_selection(obj->name);
	return send_generic(pkt, &st_obj);
}

static int send_service_status(merlin_event *pkt, struct service_struct *obj)
{
	merlin_service_status st_obj;
	static struct service_struct *last_obj = NULL;

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

	MOD2NET_STATE_VARS(st_obj.state, obj);
	st_obj.state.last_notification = obj->last_notification;
	st_obj.state.next_notification = obj->next_notification;
	st_obj.state.accept_passive_checks = obj->accept_passive_service_checks;
	st_obj.state.obsess = obj->obsess_over_service;
	st_obj.host_name = obj->host_name;
	st_obj.service_description = obj->description;

	pkt->hdr.selection = get_selection(obj->host_name);

	return send_generic(pkt, &st_obj);
}

/*
 * checks if a poller responsible for a particular
 * hostname happens to be active and connected
 */
static int has_active_poller(const char *host_name)
{
	node_selection *sel = node_selection_by_hostname(host_name);
	linked_item *li;

	if (!sel || !sel->nodes)
		return 0;

	for (li = sel->nodes; li; li = li->next_item) {
		merlin_node *node = (merlin_node *)li->item;
		if (node->state == STATE_CONNECTED)
			return 1;
	}

	return 0;
}

/*
 * The hooks are called from broker.c in Nagios.
 */
static int hook_service_result(merlin_event *pkt, void *data)
{
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;

	switch (ds->type) {
	case NEBTYPE_SERVICECHECK_ASYNC_PRECHECK:
		/*
		 * if a connected poller is reponsible for checking
		 * the host this service resides on, we simply return
		 * an override forcing Nagios to drop the check on
		 * the floor
		 */
		if (has_active_poller(ds->host_name)) {
			service_checks.poller++;
			return NEBERROR_CALLBACKCANCEL;
		}
		/*
		 * if a peer is supposed to handle this check, we must
		 * take care not to run it
		 */
		if (!ctrl_should_run_service_check(ds->host_name, ds->service_description)) {
			service_checks.peer++;
			return NEBERROR_CALLBACKCANCEL;
		}
		service_checks.self++;
		return 0;

	case NEBTYPE_SERVICECHECK_PROCESSED:
		return send_service_status(pkt, ds->object_ptr);
	}

	return 0;
}

static int hook_host_result(merlin_event *pkt, void *data)
{
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;

	switch (ds->type) {
	case NEBTYPE_HOSTCHECK_ASYNC_PRECHECK:
	case NEBTYPE_HOSTCHECK_SYNC_PRECHECK:
		/*
		 * if a poller that is connected is responsible for
		 * checking this host, we simply return an override,
		 * forcing Nagios to drop the check on the floor.
		 */
		if (has_active_poller(ds->host_name)) {
			host_checks.poller++;
			return NEBERROR_CALLBACKCANCEL;
		}
		if (!ctrl_should_run_host_check(ds->host_name)) {
			host_checks.peer++;
			return NEBERROR_CALLBACKCANCEL;
		}
		host_checks.self++;
		return 0;

	/* only send processed host checks */
	case NEBTYPE_HOSTCHECK_PROCESSED:
		/*
		 * we fiddle a bit here and send processed host check results
		 * as host status updates
		 */
		pkt->hdr.selection = get_selection(ds->host_name);
		return send_host_status(pkt, ds->object_ptr);
	}

	return 0;
}

/*
 * Comments are buggy as hell from Nagios, so we must block
 * some of them
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

	return send_generic(pkt, data);
}

static int hook_downtime(merlin_event *pkt, void *data)
{
	nebstruct_downtime_data *ds = (nebstruct_downtime_data *)data;

	/*
	 * set the poller id properly for downtime packets
	 */
	pkt->hdr.selection = get_selection(ds->host_name);

	return send_generic(pkt, data);
}

static int hook_external_command(merlin_event *pkt, void *data)
{
	nebstruct_external_command_data *ds = (nebstruct_external_command_data *)data;
	char *semi_colon;

	/*
	 * all comments generate two events, but we only want to
	 * send one of them, so focus on NEBTYPE_EXTERNALCOMMAND_END,
	 * since that one's only generated if the command is valid in
	 * the Nagios instance that generates it.
	 */
	if (ds->type != NEBTYPE_EXTERNALCOMMAND_END)
		return 0;

	switch (ds->command_type) {
	case CMD_ACKNOWLEDGE_HOST_PROBLEM:
	case CMD_ACKNOWLEDGE_SVC_PROBLEM:
	case CMD_ADD_HOST_COMMENT:
	case CMD_ADD_SVC_COMMENT:
	case CMD_DELAY_SVC_NOTIFICATION:
	case CMD_DELAY_HOST_NOTIFICATION:
	case CMD_SCHEDULE_HOST_SVC_CHECKS:
	case CMD_DELAY_HOST_SVC_NOTIFICATIONS:
	case CMD_PROCESS_HOST_CHECK_RESULT:
	case CMD_PROCESS_SERVICE_CHECK_RESULT:
	case CMD_SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME:
	case CMD_SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME:
	case CMD_SCHEDULE_FORCED_HOST_CHECK:
	case CMD_SCHEDULE_FORCED_HOST_SVC_CHECKS:
	case CMD_SCHEDULE_FORCED_SVC_CHECK:
	case CMD_SCHEDULE_HOST_DOWNTIME:
	case CMD_SCHEDULE_HOST_SVC_DOWNTIME:
	case CMD_SCHEDULE_SVC_DOWNTIME:
	case CMD_SCHEDULE_HOST_CHECK:
	case CMD_SCHEDULE_SVC_CHECK:
	case CMD_SEND_CUSTOM_HOST_NOTIFICATION:
	case CMD_SEND_CUSTOM_SVC_NOTIFICATION:
	case CMD_SET_HOST_NOTIFICATION_NUMBER:
	case CMD_SET_SVC_NOTIFICATION_NUMBER:
		if (!ds->command_args || !(semi_colon = strchr(ds->command_args, ';')))
			return send_generic(pkt, data);

		/*
		 * looks like we have everything we need, so get the
		 * selection based on the hostname so the daemon knows
		 * which node(s) to send the command to (could very well
		 * be 'nowhere')
		 */
		*semi_colon = '\0';
		pkt->hdr.selection = get_selection(ds->command_args);
		*semi_colon = ';';
		break;
	}

	return send_generic(pkt, data);
}

static int hook_host_status(merlin_event *pkt, void *data)
{
	nebstruct_host_status_data *ds = (nebstruct_host_status_data *)data;
	return send_host_status(pkt, ds->object_ptr);
}

static int hook_service_status(merlin_event *pkt, void *data)
{
	nebstruct_service_status_data *ds = (nebstruct_service_status_data *)data;
	return send_service_status(pkt, ds->object_ptr);
}

static int hook_contact_notification(merlin_event *pkt, void *data)
{
	nebstruct_contact_notification_data *ds = (nebstruct_contact_notification_data *)data;

	if (ds->type != NEBTYPE_CONTACTNOTIFICATION_END)
		return 0;

	return send_generic(pkt, data);
}

static int hook_contact_notification_method(merlin_event *pkt, void *data)
{
	nebstruct_contact_notification_method_data *ds =
		(nebstruct_contact_notification_method_data *)data;

	if (ds->type != NEBTYPE_CONTACTNOTIFICATIONMETHOD_END)
		return 0;

	return send_generic(pkt, data);
}

static int hook_notification(merlin_event *pkt, void *data)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)data;

	if (ds->type != NEBTYPE_NOTIFICATION_END)
		return 0;

	return send_generic(pkt, data);
}

int merlin_mod_hook(int cb, void *data)
{
	merlin_event pkt;
	int result = 0;

	if (!data) {
		lerr("eventbroker module called with NULL data");
		return -1;
	} else if (cb < 0 || cb > NEBCALLBACK_NUMITEMS) {
		lerr("merlin_mod_hook() called with invalid callback id");
		return -1;
	}

	/*
	 * must reset this here so events we don't check for
	 * dupes are always sent properly
	 */
	check_dupes = 0;

	/* If we've lost sync, we must make sure we send the paths again */
	if (merlin_should_send_paths && merlin_should_send_paths < time(NULL)) {
		/* send_paths resets merlin_should_send_paths if successful */
		send_paths();
	}

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.type = cb;
	pkt.hdr.selection = 0xffff;
	switch (cb) {
	case NEBCALLBACK_NOTIFICATION_DATA:
		result = hook_notification(&pkt, data);
		break;

	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
		result = hook_contact_notification(&pkt, data);
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
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
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

	if (result < 0) {
		lwarn("Daemon is flooded and backlogging failed. Staying dormant for 15 seconds");
		merlin_should_send_paths = time(NULL) + 15;
	}

	return result;
}

#define CB_ENTRY(pollers_only, type, hook) \
	{ pollers_only, type, #type, #hook }
static struct callback_struct {
	int pollers_only;
	int type;
	char *name;
	char *hook_name;
} callback_table[] = {
/*
	CB_ENTRY(1, NEBCALLBACK_PROCESS_DATA, post_config_init),
	CB_ENTRY(0, NEBCALLBACK_LOG_DATA, hook_generic),
	CB_ENTRY(1, NEBCALLBACK_SYSTEM_COMMAND_DATA, hook_generic),
	CB_ENTRY(1, NEBCALLBACK_EVENT_HANDLER_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_NOTIFICATION_DATA, hook_notification),
	CB_ENTRY(0, NEBCALLBACK_CONTACT_NOTIFICATION_DATA, hook_contact_notification),
 */
	CB_ENTRY(0, NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, hook_contact_notification_method),

	CB_ENTRY(1, NEBCALLBACK_SERVICE_CHECK_DATA, hook_service_result),
	CB_ENTRY(1, NEBCALLBACK_HOST_CHECK_DATA, hook_host_result),
	CB_ENTRY(0, NEBCALLBACK_COMMENT_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_DOWNTIME_DATA, hook_generic),
	CB_ENTRY(1, NEBCALLBACK_FLAPPING_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_PROGRAM_STATUS_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_HOST_STATUS_DATA, hook_host_status),
	CB_ENTRY(0, NEBCALLBACK_SERVICE_STATUS_DATA, hook_service_status),
	CB_ENTRY(0, NEBCALLBACK_EXTERNAL_COMMAND_DATA, hook_generic),
};

extern void *neb_handle;
int register_merlin_hooks(uint32_t mask)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		/* ignored filtered-out eventtypes */
		if (!(mask & (1 << cb->type))) {
			ldebug("EVENTFILTER: Ignoring %s events from Nagios", callback_name(cb->type));
			continue;
		}

		neb_register_callback(cb->type, neb_handle, 0, merlin_mod_hook);
	}

	return 0;
}

/*
 * We ignore any event masks here. Nagios should handle a module
 * unloading a function it hasn't registered gracefully anyways.
 */
int deregister_merlin_hooks(void)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		neb_deregister_callback(cb->type, merlin_mod_hook);
	}

	return 0;
}
