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
	const gchar *name = NULL;
	char *result_line = NULL;
	gchar *packed_data = NULL;
	gpointer *unpacked_data = NULL;

	/* CTRL_PACKET isn't decoded through merlin*_decode */
	unpacked_data = g_malloc(evt->hdr.len);
	memcpy(unpacked_data, evt->body, evt->hdr.len);

	if (evt->hdr.type != CTRL_PACKET) {
		if (merlincat_decode(unpacked_data, evt->hdr.len, evt->hdr.type)) {
			goto pack_error;
		}
	}

	kvv = kvvec_create(30);
	if (kvv == NULL)
		goto pack_error;

	switch (evt->hdr.type) {
	case NEBCALLBACK_PROCESS_DATA:
		name = "EVT PROCESS";
		process_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_TIMED_EVENT_DATA:
		name = "EVT TIMED_EVENT";
		timed_event_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_LOG_DATA:
		name = "EVT LOG";
		log_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SYSTEM_COMMAND_DATA:
		name = "EVT SYSTEM_COMMAND";
		system_command_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EVENT_HANDLER_DATA:
		name = "EVT EVENT_HANDLER";
		event_handler_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		name = "EVT NOTIFICATION";
		notification_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		name = "EVT SERVICE_CHECK";
		merlin_service_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_CHECK_DATA:
		name = "EVT HOST_CHECK";
		merlin_host_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		name = "EVT COMMENT";
		comment_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		name = "EVT DOWNTIME";
		downtime_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		name = "EVT FLAPPING";
		flapping_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		name = "EVT PROGRAM_STATUS";
		program_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_HOST_STATUS_DATA:
		name = "EVT HOST_STATUS";
		merlin_host_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		name = "EVT SERVICE_STATUS";
		merlin_service_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_PROGRAM_DATA:
		name = "EVT ADAPTIVE_PROGRAM";
		adaptive_program_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_HOST_DATA:
		name = "EVT ADAPTIVE_HOST";
		adaptive_host_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_SERVICE_DATA:
		name = "EVT ADAPTIVE_SERVICE";
		adaptive_service_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		name = "EVT EXTERNAL_COMMAND";
		external_command_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_AGGREGATED_STATUS_DATA:
		name = "EVT AGGREGATED_STATUS";
		aggregated_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_RETENTION_DATA:
		name = "EVT RETENTION";
		retention_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
		name = "EVT CONTACT_NOTIFICATION";
		contact_notification_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		name = "EVT CONTACT_NOTIFICATION_METHOD";
		contact_notification_method_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ACKNOWLEDGEMENT_DATA:
		name = "EVT ACKNOWLEDGEMENT";
		acknowledgement_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_STATE_CHANGE_DATA:
		name = "EVT STATE_CHANGE";
		statechange_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_CONTACT_STATUS_DATA:
		name = "EVT CONTACT_STATUS";
		contact_status_to_kvvec(kvv, unpacked_data);
		break;
	case NEBCALLBACK_ADAPTIVE_CONTACT_DATA:
		name = "EVT ADAPTIVE_CONTACT";
		adaptive_contact_to_kvvec(kvv, unpacked_data);
		break;
	case CTRL_PACKET:
		switch (evt->hdr.code) {
		case CTRL_ACTIVE:
			name = "EVT CTRL_ACTIVE";
			break;
		default:
			name = "EVT CTRL_UNKNOWN";
		}
		merlin_nodeinfo_to_kvvec(kvv, unpacked_data);
		break;
	default:
		name = "UNKNOWN";
	}

	packed_data = kvvec_to_ekvstr(kvv);
	result_line = malloc(10 + strlen(name) + strlen(packed_data));
	sprintf(result_line, "%s %s", name, packed_data);
	free(packed_data);

	kvvec_destroy(kvv, KVVEC_FREE_ALL);

	g_free(unpacked_data);
	return result_line;

	/* Error cleanup routine */
	pack_error: kvvec_destroy(kvv, KVVEC_FREE_ALL);
	g_free(unpacked_data);
	return NULL;
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
	int res = -1;
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

	if (0 == strcmp("PROCESS", cmd)) {
		evt->hdr.type = NEBCALLBACK_PROCESS_DATA;
		res = kvvec_to_process(kvv, unpacked_data);
	} else if (0 == strcmp("TIMED_EVENT", cmd)) {
		evt->hdr.type = NEBCALLBACK_TIMED_EVENT_DATA;
		res = kvvec_to_timed_event(kvv, unpacked_data);
	} else if (0 == strcmp("LOG", cmd)) {
		evt->hdr.type = NEBCALLBACK_LOG_DATA;
		res = kvvec_to_log(kvv, unpacked_data);
	} else if (0 == strcmp("SYSTEM_COMMAND", cmd)) {
		evt->hdr.type = NEBCALLBACK_SYSTEM_COMMAND_DATA;
		res = kvvec_to_system_command(kvv, unpacked_data);
	} else if (0 == strcmp("EVENT_HANDLER", cmd)) {
		evt->hdr.type = NEBCALLBACK_EVENT_HANDLER_DATA;
		res = kvvec_to_event_handler(kvv, unpacked_data);
	} else if (0 == strcmp("NOTIFICATION", cmd)) {
		evt->hdr.type = NEBCALLBACK_NOTIFICATION_DATA;
		res = kvvec_to_notification(kvv, unpacked_data);
	} else if (0 == strcmp("SERVICE_CHECK", cmd)) {
		evt->hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
		res = kvvec_to_merlin_service_status(kvv, unpacked_data);
	} else if (0 == strcmp("HOST_CHECK", cmd)) {
		evt->hdr.type = NEBCALLBACK_HOST_CHECK_DATA;
		res = kvvec_to_merlin_host_status(kvv, unpacked_data);
	} else if (0 == strcmp("COMMENT", cmd)) {
		evt->hdr.type = NEBCALLBACK_COMMENT_DATA;
		res = kvvec_to_comment(kvv, unpacked_data);
	} else if (0 == strcmp("DOWNTIME", cmd)) {
		evt->hdr.type = NEBCALLBACK_DOWNTIME_DATA;
		res = kvvec_to_downtime(kvv, unpacked_data);
	} else if (0 == strcmp("FLAPPING", cmd)) {
		evt->hdr.type = NEBCALLBACK_FLAPPING_DATA;
		res = kvvec_to_flapping(kvv, unpacked_data);
	} else if (0 == strcmp("PROGRAM_STATUS", cmd)) {
		evt->hdr.type = NEBCALLBACK_PROGRAM_STATUS_DATA;
		res = kvvec_to_program_status(kvv, unpacked_data);
	} else if (0 == strcmp("HOST_STATUS", cmd)) {
		evt->hdr.type = NEBCALLBACK_HOST_STATUS_DATA;
		res = kvvec_to_merlin_host_status(kvv, unpacked_data);
	} else if (0 == strcmp("SERVICE_STATUS", cmd)) {
		evt->hdr.type = NEBCALLBACK_SERVICE_STATUS_DATA;
		res = kvvec_to_merlin_service_status(kvv, unpacked_data);
	} else if (0 == strcmp("ADAPTIVE_PROGRAM", cmd)) {
		evt->hdr.type = NEBCALLBACK_ADAPTIVE_PROGRAM_DATA;
		res = kvvec_to_adaptive_program(kvv, unpacked_data);
	} else if (0 == strcmp("ADAPTIVE_HOST", cmd)) {
		evt->hdr.type = NEBCALLBACK_ADAPTIVE_HOST_DATA;
		res = kvvec_to_adaptive_host(kvv, unpacked_data);
	} else if (0 == strcmp("ADAPTIVE_SERVICE", cmd)) {
		evt->hdr.type = NEBCALLBACK_ADAPTIVE_SERVICE_DATA;
		res = kvvec_to_adaptive_service(kvv, unpacked_data);
	} else if (0 == strcmp("EXTERNAL_COMMAND", cmd)) {
		evt->hdr.type = NEBCALLBACK_EXTERNAL_COMMAND_DATA;
		res = kvvec_to_external_command(kvv, unpacked_data);
	} else if (0 == strcmp("AGGREGATED_STATUS", cmd)) {
		evt->hdr.type = NEBCALLBACK_AGGREGATED_STATUS_DATA;
		res = kvvec_to_aggregated_status(kvv, unpacked_data);
	} else if (0 == strcmp("RETENTION", cmd)) {
		evt->hdr.type = NEBCALLBACK_RETENTION_DATA;
		res = kvvec_to_retention(kvv, unpacked_data);
	} else if (0 == strcmp("CONTACT_NOTIFICATION", cmd)) {
		evt->hdr.type = NEBCALLBACK_CONTACT_NOTIFICATION_DATA;
		res = kvvec_to_contact_notification(kvv, unpacked_data);
	} else if (0 == strcmp("CONTACT_NOTIFICATION_METHOD", cmd)) {
		evt->hdr.type = NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA;
		res = kvvec_to_contact_notification_method(kvv, unpacked_data);
	} else if (0 == strcmp("ACKNOWLEDGEMENT", cmd)) {
		evt->hdr.type = NEBCALLBACK_ACKNOWLEDGEMENT_DATA;
		res = kvvec_to_acknowledgement(kvv, unpacked_data);
	} else if (0 == strcmp("STATE_CHANGE", cmd)) {
		evt->hdr.type = NEBCALLBACK_STATE_CHANGE_DATA;
		res = kvvec_to_statechange(kvv, unpacked_data);
	} else if (0 == strcmp("CONTACT_STATUS", cmd)) {
		evt->hdr.type = NEBCALLBACK_CONTACT_STATUS_DATA;
		res = kvvec_to_contact_status(kvv, unpacked_data);
	} else if (0 == strcmp("ADAPTIVE_CONTACT", cmd)) {
		evt->hdr.type = NEBCALLBACK_ADAPTIVE_CONTACT_DATA;
		res = kvvec_to_adaptive_contact(kvv, unpacked_data);
	} else if (0 == strcmp("CTRL_ACTIVE", cmd)) {
		evt->hdr.type = CTRL_PACKET;
		evt->hdr.code = CTRL_ACTIVE;
		res = kvvec_to_merlin_nodeinfo(kvv, unpacked_data);
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

	return evt;
}
