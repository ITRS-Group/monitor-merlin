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

#include "module.h"
#include "nagios/objects.h"

static int send_generic(merlin_event *pkt, void *data)
{
	pkt->hdr.len = blockify_event(pkt, data);
	if (!pkt->hdr.len) {
		lerr("Header len is 0 for callback %d. Update offset in hookinfo.h", pkt->hdr.type);
		return -1;
	}

	return ipc_send_event(pkt);
}

static int get_selection(const char *key)
{
	int *selection = hash_find_val(key);

	return selection ? *selection & 0xffff : CTRL_GENERIC;
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

	return send_generic(pkt, ds);
}


static int hook_host_status(merlin_event *pkt, void *data)
{
	nebstruct_host_status_data *ds = (nebstruct_host_status_data *)data;
	merlin_host_status st_obj;
	struct host_struct *obj;

	memset(&st_obj, 0, sizeof(st_obj));
	obj = (struct host_struct *)ds->object_ptr;

	MOD2NET_STATE_VARS(st_obj.state, obj);
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

	memset(&st_obj, 0, sizeof(st_obj));
	obj = (struct service_struct *)ds->object_ptr;

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

	/* If we've lost sync, we must make sure we send the paths again */
	if (merlin_should_send_paths && merlin_should_send_paths < time(NULL)) {
		/* send_paths resets merlin_should_send_paths if successful */
		send_paths();
	}

	ldebug("Processing callback %s", callback_name(cb));

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
