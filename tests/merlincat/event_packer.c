#include "event_packer.h"
#include "merlincat_codec.h"
#include "nebev2kvvec.h"
#include "kvvec2nebev.h"
#include <shared/shared.h>
#include <naemon/naemon.h>
#include <glib.h>

/*
 Mapping between NEBCALLBACK_X_DATA and structure name.

 PROCESS process
 TIMED_EVENT timed_event
 LOG log
 SYSTEM_COMMAND system_command
 EVENT_HANDLER event_handler
 NOTIFICATION notification
 SERVICE_CHECK merlin_service_status
 HOST_CHECK merlin_host_status
 COMMENT comment
 DOWNTIME downtime
 FLAPPING flapping
 PROGRAM_STATUS program_status
 HOST_STATUS merlin_host_status
 SERVICE_STATUS merlin_service_status
 ADAPTIVE_PROGRAM adaptive_program
 ADAPTIVE_HOST adaptive_host
 ADAPTIVE_SERVICE adaptive_service
 EXTERNAL_COMMAND external_command
 AGGREGATED_STATUS aggregated_status
 RETENTION retention
 CONTACT_NOTIFICATION contact_notification
 CONTACT_NOTIFICATION_METHOD contact_notification_method
 ACKNOWLEDGEMENT acknowledgement
 STATE_CHANGE statechange
 CONTACT_STATUS contact_status
 ADAPTIVE_CONTACT adaptive_contact
 CTRL_ACTIVE merlin_nodeinfo
 */

char *event_packer_pack(const merlin_event *evt) {
	struct kvvec *kvv = NULL;
	const char *name = NULL;
	char *packed_data = NULL;
	char *result_line = NULL;

	kvv = event_packer_pack_kvv(evt, &name);
	if(kvv == NULL)
		return NULL;

	packed_data = kvvec_to_ekvstr(kvv);
	result_line = malloc(10 + strlen(name) + strlen(packed_data));
	sprintf(result_line, "EVT %s %s", name, packed_data);
	free(packed_data);

	kvvec_destroy(kvv, KVVEC_FREE_ALL);
	return result_line;
}

struct kvvec *event_packer_pack_kvv(const merlin_event *evt, const char **name) {
	struct kvvec *kvv = NULL;
	gpointer *unpacked_data = NULL;

	unpacked_data = g_malloc0(evt->hdr.len);
	memcpy(unpacked_data, evt->body, evt->hdr.len);

	/* CTRL_PACKET isn't decoded through merlin*_decode */
	if (evt->hdr.type != CTRL_PACKET) {
		if (merlincat_decode(unpacked_data, evt->hdr.len, evt->hdr.type)) {
			g_free(unpacked_data);
			return NULL;
		}
	}

	kvv = kvvec_create(30);
	if (kvv == NULL) {
		g_free(unpacked_data);
		return NULL;
	}

	switch (evt->hdr.type) {
	case NEBCALLBACK_PROCESS_DATA:
		if (name)
			*name = "PROCESS";
		process_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_TIMED_EVENT_DATA:
		if (name)
			*name = "TIMED_EVENT";
		timed_event_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_LOG_DATA:
		if (name)
			*name = "LOG";
		log_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SYSTEM_COMMAND_DATA:
		if (name)
			*name = "SYSTEM_COMMAND";
		system_command_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EVENT_HANDLER_DATA:
		if (name)
			*name = "EVENT_HANDLER";
		event_handler_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		if (name)
			*name = "NOTIFICATION";
		notification_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		if (name)
			*name = "SERVICE_CHECK";
		merlin_service_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_CHECK_DATA:
		if (name)
			*name = "HOST_CHECK";
		merlin_host_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		if (name)
			*name = "COMMENT";
		comment_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		if (name)
			*name = "DOWNTIME";
		downtime_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		if (name)
			*name = "FLAPPING";
		flapping_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		if (name)
			*name = "PROGRAM_STATUS";
		program_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_STATUS_DATA:
		if (name)
			*name = "HOST_STATUS";
		merlin_host_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		if (name)
			*name = "SERVICE_STATUS";
		merlin_service_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_PROGRAM_DATA:
		if (name)
			*name = "ADAPTIVE_PROGRAM";
		adaptive_program_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_HOST_DATA:
		if (name)
			*name = "ADAPTIVE_HOST";
		adaptive_host_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_SERVICE_DATA:
		if (name)
			*name = "ADAPTIVE_SERVICE";
		adaptive_service_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		if (name)
			*name = "EXTERNAL_COMMAND";
		external_command_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_AGGREGATED_STATUS_DATA:
		if (name)
			*name = "AGGREGATED_STATUS";
		aggregated_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_RETENTION_DATA:
		if (name)
			*name = "RETENTION";
		retention_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
		if (name)
			*name = "CONTACT_NOTIFICATION";
		contact_notification_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		if (name)
			*name = "CONTACT_NOTIFICATION_METHOD";
		contact_notification_method_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ACKNOWLEDGEMENT_DATA:
		if (name)
			*name = "ACKNOWLEDGEMENT";
		acknowledgement_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_STATE_CHANGE_DATA:
		if (name)
			*name = "STATE_CHANGE";
		statechange_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_STATUS_DATA:
		if (name)
			*name = "CONTACT_STATUS";
		contact_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_CONTACT_DATA:
		if (name)
			*name = "ADAPTIVE_CONTACT";
		adaptive_contact_to_kvvec(kvv, unpacked_data);
		break;
	case CTRL_PACKET:
		switch (evt->hdr.code) {
		case CTRL_ACTIVE:
			if (name)
				*name = "CTRL_ACTIVE";
			break;
		default:
			if (name)
				*name = "CTRL_UNKNOWN";
		}
		merlin_nodeinfo_to_kvvec(kvv, unpacked_data);
		break;
	default:
		if (name)
			*name = "UNKNOWN";
	}
	g_free(unpacked_data);
	return kvv;
}

/**
 * If a string has a prefix, return a pointer to the tail of the string,
 * copy the token to the pointer of "prefix", which should be long enough.
 * otherwise NULL
 *
 * TODO: This should handle max length of "prefix" when used more seriously
 */
static const char *get_tail(char *prefix, const char *str) {
	char *tail;
	size_t len;

	tail = strchr(str, ' ');
	if (tail == NULL)
		return NULL;

	len = tail - str;
	memcpy(prefix, str, len);
	prefix[len] = '\0';

	return tail + 1;
}

merlin_event *event_packer_unpack(const char *line) {
	const char *data;
	char cmd[256];
	struct kvvec *kvv = NULL;
	merlin_event *evt = NULL;

	/* Parse first command */
	data = get_tail(cmd, line);
	if (0 == strcmp("EVT", cmd)) {

		/* Parse second sub command, and for all EVT, the tail is a ekvstr */
		data = get_tail(cmd, data);
		kvv = ekvstr_to_kvvec(data);
		if (kvv == NULL)
			goto unpack_error;
		/* UNPACK HERE */
		evt = event_packer_unpack_kvv(cmd, kvv);
		if(evt == NULL)
			goto unpack_error;
	} else {
		goto unpack_error;
	}

	kvvec_destroy(kvv, KVVEC_FREE_ALL);
	return evt;

	/*
	 * Error handling, this is only reached by goto-bailout, and clears the
	 * state
	 */
	unpack_error: /**/
	kvvec_destroy(kvv, KVVEC_FREE_ALL);
	free(evt);
	return NULL;
}

uint16_t event_packer_str_to_type(const char *typestr) {
	if (0 == strcmp("PROCESS", typestr)) {
		return NEBCALLBACK_PROCESS_DATA;
	} else if (0 == strcmp("TIMED_EVENT", typestr)) {
		return NEBCALLBACK_TIMED_EVENT_DATA;
	} else if (0 == strcmp("LOG", typestr)) {
		return NEBCALLBACK_LOG_DATA;
	} else if (0 == strcmp("SYSTEM_COMMAND", typestr)) {
		return NEBCALLBACK_SYSTEM_COMMAND_DATA;
	} else if (0 == strcmp("EVENT_HANDLER", typestr)) {
		return NEBCALLBACK_EVENT_HANDLER_DATA;
	} else if (0 == strcmp("NOTIFICATION", typestr)) {
		return NEBCALLBACK_NOTIFICATION_DATA;
	} else if (0 == strcmp("SERVICE_CHECK", typestr)) {
		return NEBCALLBACK_SERVICE_CHECK_DATA;
	} else if (0 == strcmp("HOST_CHECK", typestr)) {
		return NEBCALLBACK_HOST_CHECK_DATA;
	} else if (0 == strcmp("COMMENT", typestr)) {
		return NEBCALLBACK_COMMENT_DATA;
	} else if (0 == strcmp("DOWNTIME", typestr)) {
		return NEBCALLBACK_DOWNTIME_DATA;
	} else if (0 == strcmp("FLAPPING", typestr)) {
		return NEBCALLBACK_FLAPPING_DATA;
	} else if (0 == strcmp("PROGRAM_STATUS", typestr)) {
		return NEBCALLBACK_PROGRAM_STATUS_DATA;
	} else if (0 == strcmp("HOST_STATUS", typestr)) {
		return NEBCALLBACK_HOST_STATUS_DATA;
	} else if (0 == strcmp("SERVICE_STATUS", typestr)) {
		return NEBCALLBACK_SERVICE_STATUS_DATA;
	} else if (0 == strcmp("ADAPTIVE_PROGRAM", typestr)) {
		return NEBCALLBACK_ADAPTIVE_PROGRAM_DATA;
	} else if (0 == strcmp("ADAPTIVE_HOST", typestr)) {
		return NEBCALLBACK_ADAPTIVE_HOST_DATA;
	} else if (0 == strcmp("ADAPTIVE_SERVICE", typestr)) {
		return NEBCALLBACK_ADAPTIVE_SERVICE_DATA;
	} else if (0 == strcmp("EXTERNAL_COMMAND", typestr)) {
		return NEBCALLBACK_EXTERNAL_COMMAND_DATA;
	} else if (0 == strcmp("AGGREGATED_STATUS", typestr)) {
		return NEBCALLBACK_AGGREGATED_STATUS_DATA;
	} else if (0 == strcmp("RETENTION", typestr)) {
		return NEBCALLBACK_RETENTION_DATA;
	} else if (0 == strcmp("CONTACT_NOTIFICATION", typestr)) {
		return NEBCALLBACK_CONTACT_NOTIFICATION_DATA;
	} else if (0 == strcmp("CONTACT_NOTIFICATION_METHOD", typestr)) {
		return NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA;
	} else if (0 == strcmp("ACKNOWLEDGEMENT", typestr)) {
		return NEBCALLBACK_ACKNOWLEDGEMENT_DATA;
	} else if (0 == strcmp("STATE_CHANGE", typestr)) {
		return NEBCALLBACK_STATE_CHANGE_DATA;
	} else if (0 == strcmp("CONTACT_STATUS", typestr)) {
		return NEBCALLBACK_CONTACT_STATUS_DATA;
	} else if (0 == strcmp("ADAPTIVE_CONTACT", typestr)) {
		return NEBCALLBACK_ADAPTIVE_CONTACT_DATA;
	} else if (0 == strncmp("CTRL_", typestr, 5)) {
		/* All prefxies of CTRL_ */
		return CTRL_PACKET;
	}
	/* Guaranteed not to collide with any NEBCALLBACK_..._DATA */
	return NEBCALLBACK_TYPE__COUNT;
}

merlin_event *event_packer_unpack_kvv(const char *cmd, struct kvvec *kvv) {
	merlin_event *evt = NULL;
	gpointer unpacked_data = NULL;
	int res = -1;

	/* Create an empty merlin_event */
	evt = malloc(sizeof(merlin_event));
	if (evt == NULL)
		return NULL;
	memset(evt, 0, sizeof(merlin_event));

	/*
	 * Those parameters is the default values, it might be useful to be able
	 * to override those from command  line for testing purposes later
	 */
	evt->hdr.sig.id = MERLIN_SIGNATURE;
	evt->hdr.protocol = MERLIN_PROTOCOL_VERSION;
	evt->hdr.type = 0; /* updated below */
	evt->hdr.code = 0; /* event code (used for control packets) */
	evt->hdr.selection = 0; /* used when noc Nagios communicates with mrd */
	evt->hdr.len = 0; /* size of body */
	gettimeofday(&evt->hdr.sent, NULL); /* when this message was sent */

	/* Create a storage for unpacked data */
	unpacked_data = malloc(128 << 10); // size is hardcoded in merlin...
	if (unpacked_data == NULL) {
		free(evt);
		return NULL;
	}
	memset(unpacked_data, 0, 128 << 10);

	evt->hdr.type = event_packer_str_to_type(cmd);

	switch (evt->hdr.type) {
	case NEBCALLBACK_PROCESS_DATA:
		res = kvvec_to_process(kvv, unpacked_data);
		break;
	case NEBCALLBACK_TIMED_EVENT_DATA:
		res = kvvec_to_timed_event(kvv, unpacked_data);
		break;
	case NEBCALLBACK_LOG_DATA:
		res = kvvec_to_log(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SYSTEM_COMMAND_DATA:
		res = kvvec_to_system_command(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EVENT_HANDLER_DATA:
		res = kvvec_to_event_handler(kvv, unpacked_data);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		res = kvvec_to_notification(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		res = kvvec_to_merlin_service_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_CHECK_DATA:
		res = kvvec_to_merlin_host_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		res = kvvec_to_comment(kvv, unpacked_data);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		res = kvvec_to_downtime(kvv, unpacked_data);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		res = kvvec_to_flapping(kvv, unpacked_data);
		break;
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		res = kvvec_to_program_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_STATUS_DATA:
		res = kvvec_to_merlin_host_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		res = kvvec_to_merlin_service_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_PROGRAM_DATA:
		res = kvvec_to_adaptive_program(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_HOST_DATA:
		res = kvvec_to_adaptive_host(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_SERVICE_DATA:
		res = kvvec_to_adaptive_service(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		res = kvvec_to_external_command(kvv, unpacked_data);
		break;
	case NEBCALLBACK_AGGREGATED_STATUS_DATA:
		res = kvvec_to_aggregated_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_RETENTION_DATA:
		res = kvvec_to_retention(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
		res = kvvec_to_contact_notification(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		res = kvvec_to_contact_notification_method(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ACKNOWLEDGEMENT_DATA:
		res = kvvec_to_acknowledgement(kvv, unpacked_data);
		break;
	case NEBCALLBACK_STATE_CHANGE_DATA:
		res = kvvec_to_statechange(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_STATUS_DATA:
		res = kvvec_to_contact_status(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_CONTACT_DATA:
		res = kvvec_to_adaptive_contact(kvv, unpacked_data);
		break;
	case CTRL_PACKET:
		evt->hdr.code = CTRL_ACTIVE; /* Todo: Assume CTRL_ACTIVE */
		res = kvvec_to_merlin_nodeinfo(kvv, unpacked_data);
		break;
	}

	if (res != 0) {
		/* Error */
		free(evt);
		free(unpacked_data);
		return NULL;
	}

	if (evt->hdr.type == CTRL_PACKET) {
		memcpy(evt->body, unpacked_data, sizeof(merlin_nodeinfo));
		evt->hdr.len = sizeof(merlin_nodeinfo);
	} else {
		gsize size = merlincat_encode(unpacked_data, evt->hdr.type, evt->body,
				128 << 10);
		if (size == 0) {
			/* Error */
			free(evt);
			free(unpacked_data);
			return NULL;
		}
		evt->hdr.len = size;
	}

	free(unpacked_data);
	return evt;
}
