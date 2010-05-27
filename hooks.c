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

static int check_dupes;
static merlin_event last_pkt;

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

static void bt_scan(const char *mark, int count)
{
#ifdef DEBUG_DUPES_CAREFULLY
#define TRACE_SIZE 100
	char **strings;
	void *trace[TRACE_SIZE];
	int i, bt_count, have_mark = 0;

	bt_count = backtrace(trace, TRACE_SIZE);
	strings = backtrace_symbols(trace, bt_count);
	for (i = 0; i < bt_count; i++) {
		char *paren;

		if (mark && !have_mark) {
			if (strstr(strings[i], mark))
				have_mark = i;
			continue;
		}
		if (mark && count && i >= have_mark + count)
			break;
		paren = strchr(strings[i], '(');
		paren = paren ? paren : strings[i];
		ldebug("%2d: %s", i, paren);
	}
	free(strings);
#endif
}

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

	if (!memcmp(&last_pkt, pkt, packet_size(pkt))) {
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

	pkt->hdr.len = blockify_event(pkt, data);
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
	int selection = hash_find_val(key);

	if (selection < 0) {
		lwarn("key '%s' doesn't match any possible selection", key);
		return 0;
	}

	return selection & 0xffff;
}

/*
 * The hooks are called from broker.c in Nagios.
 */
static int hook_service_result(merlin_event *pkt, void *data)
{
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;

	if (ds->type != NEBTYPE_SERVICECHECK_PROCESSED
		|| ds->check_type != SERVICE_CHECK_ACTIVE
		|| pkt->hdr.type != NEBCALLBACK_SERVICE_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	pkt->hdr.selection = get_selection(ds->host_name);

	return send_generic(pkt, ds);
}

static int hook_host_result(merlin_event *pkt, void *data)
{
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;

	/* ignore un-processed and passive checks */
	if (ds->type != NEBTYPE_HOSTCHECK_PROCESSED
		|| ds->check_type != HOST_CHECK_ACTIVE
		|| pkt->hdr.type != NEBCALLBACK_HOST_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for host '%s'", ds->host_name);

	return send_generic(pkt, ds);
}


/**
 * host and service status structures share a *lot* of data,
 * so we can get away with a lot less code by having this
 * rather simple macro here
 */
#define COPY_STATE_VARS(dest, src) \
	dest.initial_state = src->initial_state; \
	dest.flap_detection_enabled = src->flap_detection_enabled; \
	dest.low_flap_threshold = src->low_flap_threshold;  \
	dest.high_flap_threshold = src->high_flap_threshold; \
	dest.check_freshness = src->check_freshness; \
	dest.freshness_threshold = src->freshness_threshold; \
	dest.process_performance_data = src->process_performance_data; \
	dest.checks_enabled = src->checks_enabled; \
	dest.event_handler_enabled = src->event_handler_enabled; \
	dest.problem_has_been_acknowledged = src->problem_has_been_acknowledged; \
	dest.acknowledgement_type = src->acknowledgement_type; \
	dest.check_type = src->check_type; \
	dest.current_state = src->current_state; \
	dest.last_state = src->last_state; \
	dest.last_hard_state = src->last_hard_state; \
	dest.state_type = src->state_type; \
	dest.current_attempt = src->current_attempt; \
	dest.current_event_id = src->current_event_id; \
	dest.last_event_id = src->last_event_id; \
	dest.current_problem_id = src->current_problem_id; \
	dest.last_problem_id = src->last_problem_id; \
	dest.latency = src->latency; \
	dest.execution_time = src->execution_time; \
	dest.notifications_enabled = src->notifications_enabled; \
	dest.next_check = src->next_check; \
	dest.should_be_scheduled = src->should_be_scheduled; \
	dest.last_check = src->last_check; \
	dest.last_state_change = src->last_state_change; \
	dest.last_hard_state_change = src->last_hard_state_change; \
	dest.has_been_checked = src->has_been_checked; \
	dest.current_notification_number = src->current_notification_number; \
	dest.current_notification_id = src->current_notification_id; \
	dest.check_flapping_recovery_notification = src->check_flapping_recovery_notification; \
	dest.scheduled_downtime_depth = src->scheduled_downtime_depth; \
	dest.pending_flex_downtime = src->pending_flex_downtime; \
	dest.is_flapping = src->is_flapping; \
	dest.flapping_comment_id = src->flapping_comment_id; \
	dest.percent_state_change = src->percent_state_change; \
	dest.plugin_output = src->plugin_output; \
	dest.long_plugin_output = src->long_plugin_output; \
	dest.perf_data = src->perf_data;

static int hook_host_status(merlin_event *pkt, void *data)
{
	nebstruct_host_status_data *ds = (nebstruct_host_status_data *)data;
	merlin_host_status st_obj;
	struct host_struct *obj;
	static struct host_struct *last_obj = NULL;

	obj = (struct host_struct *)ds->object_ptr;
	if (obj == last_obj) {
		check_dupes = 1;
	} else {
		check_dupes = 0;
		last_obj = obj;
	}
	bt_scan("update_host_status", 3);

	COPY_STATE_VARS(st_obj.state, obj);
	st_obj.state.last_notification = obj->last_host_notification;
	st_obj.state.next_notification = obj->next_host_notification;
	st_obj.state.accept_passive_checks = obj->accept_passive_host_checks;
	st_obj.state.obsess = obj->obsess_over_host;
	st_obj.name = obj->name;

	pkt->hdr.selection = get_selection(obj->name);

	return send_generic(pkt, &st_obj);
}

static int hook_service_status(merlin_event *pkt, void *data)
{
	nebstruct_service_status_data *ds = (nebstruct_service_status_data *)data;
	merlin_service_status st_obj;
	struct service_struct *obj;
	static struct service_struct *last_obj = NULL;

	obj = (struct service_struct *)ds->object_ptr;
	if (obj == last_obj) {
		check_dupes = 1;
	} else {
		check_dupes = 0;
		last_obj = obj;
	}
	bt_scan("update_service_status", 3);

	COPY_STATE_VARS(st_obj.state, obj);
	st_obj.state.last_notification = obj->last_notification;
	st_obj.state.next_notification = obj->next_notification;
	st_obj.state.accept_passive_checks = obj->accept_passive_service_checks;
	st_obj.state.obsess = obj->obsess_over_service;
	st_obj.host_name = obj->host_name;
	st_obj.service_description = obj->description;

	pkt->hdr.selection = get_selection(obj->host_name);

	return send_generic(pkt, &st_obj);
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

	ldebug("Processing callback %s", callback_name(cb));

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
	case NEBCALLBACK_DOWNTIME_DATA:
	case NEBCALLBACK_FLAPPING_DATA:
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
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

/*	CB_ENTRY(1, NEBCALLBACK_SERVICE_CHECK_DATA, hook_service_result),
	CB_ENTRY(1, NEBCALLBACK_HOST_CHECK_DATA, hook_host_result),
*/	CB_ENTRY(0, NEBCALLBACK_COMMENT_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_DOWNTIME_DATA, hook_generic),
	CB_ENTRY(1, NEBCALLBACK_FLAPPING_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_PROGRAM_STATUS_DATA, hook_generic),
	CB_ENTRY(0, NEBCALLBACK_HOST_STATUS_DATA, hook_host_status),
	CB_ENTRY(0, NEBCALLBACK_SERVICE_STATUS_DATA, hook_service_status),
	CB_ENTRY(0, NEBCALLBACK_EXTERNAL_COMMAND_DATA, hook_generic),
};

extern void *neb_handle;
int register_merlin_hooks(void)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		neb_register_callback(cb->type, neb_handle, 0, merlin_mod_hook);
	}

	return 0;
}

int deregister_merlin_hooks(void)
{
	uint i;

	for (i = 0; i < ARRAY_SIZE(callback_table); i++) {
		struct callback_struct *cb = &callback_table[i];

		neb_deregister_callback(cb->type, merlin_mod_hook);
	}

	return 0;
}
