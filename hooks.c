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

#define NSCORE
#include "nagios/objects.h"
#include "module.h"

int hook_generic(int cb, void *data)
{
	merlin_event pkt;

	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(data, cb, pkt.body, sizeof(pkt.body));
	if (!pkt.hdr.len)
		lerr("Header len is 0 for callback %d. Update offset in hookinfo.h", cb);
	pkt.hdr.selection = 0xffff;
	return ipc_send_event(&pkt);
}

/*
 * The hooks are called from broker.c in Nagios.
 */
int hook_service_result(int cb, void *data)
{
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;
	merlin_event pkt;
	int result;

	if (ds->type != NEBTYPE_SERVICECHECK_PROCESSED
		|| ds->check_type != SERVICE_CHECK_ACTIVE
		|| cb != NEBCALLBACK_SERVICE_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(ds, cb, pkt.body, sizeof(pkt.body));
	result = mrm_ipc_write(ds->host_name, &pkt);

	return result;
}

int hook_host_result(int cb, void *data)
{
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;
	merlin_event pkt;
	int result;

	/* ignore un-processed and passive checks */
	if (ds->type != NEBTYPE_HOSTCHECK_PROCESSED ||
		ds->check_type != HOST_CHECK_ACTIVE ||
		cb != NEBCALLBACK_HOST_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for host '%s'", ds->host_name);
	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(ds, cb, pkt.body, sizeof(pkt.body));
	result = mrm_ipc_write(ds->host_name, &pkt);

	return result;
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


int hook_host_status(int cb, void *data)
{
	nebstruct_host_status_data *ds = (nebstruct_host_status_data *)data;
	merlin_host_status *st_obj;
	struct host_struct *obj;

	obj = (struct host_struct *)ds->object_ptr;

	st_obj = malloc(sizeof(*st_obj));

	COPY_STATE_VARS(st_obj->state, obj);
	st_obj->state.last_notification = obj->last_host_notification;
	st_obj->state.next_notification = obj->next_host_notification;
	st_obj->state.accept_passive_checks = obj->accept_passive_host_checks;
	st_obj->state.obsess = obj->obsess_over_host;
	st_obj->name = obj->name;

	return 0;
}

int hook_service_status(int cb, void *data)
{
	nebstruct_service_status_data *ds = (nebstruct_service_status_data *)data;
	merlin_service_status *st_obj;
	struct service_struct *obj;

	obj = (struct service_struct *)ds->object_ptr;

	st_obj = malloc(sizeof(*st_obj));

	COPY_STATE_VARS(st_obj->state, obj);
	st_obj->state.last_notification = obj->last_notification;
	st_obj->state.next_notification = obj->next_notification;
	st_obj->host_name = obj->host_name;
	st_obj->service_description = obj->description;

	return 0;
}

int hook_notification(int cb, void *data)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)data;

	if (ds->type != NEBTYPE_NOTIFICATION_END)
		return 0;

	return hook_generic(cb, data);
}
