#include "console.h"
#include <shared/shared.h>
#include <tests/nebev2kvvec.h>
#include <naemon/naemon.h>

void console_print_merlin_event(merlin_event *evt) {
	struct kvvec *kvv = kvvec_create(30);
	const char *name;
	char *packed_data;
	switch (evt->hdr.type) {
	case NEBCALLBACK_PROCESS_DATA:
		name = "PROCESS";
		process_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_TIMED_EVENT_DATA:
		name = "TIMED_EVENT";
		timed_event_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_LOG_DATA:
		name = "LOG";
		log_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_SYSTEM_COMMAND_DATA:
		name = "SYSTEM_COMMAND";
		system_command_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_EVENT_HANDLER_DATA:
		name = "EVENT_HANDLER";
		event_handler_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		name = "NOTIFICATION";
		notification_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
		name = "SERVICE_CHECK";
		// service_check_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_HOST_CHECK_DATA:
		name = "HOST_CHECK";
		// host_check_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		name = "COMMENT";
		comment_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		name = "DOWNTIME";
		downtime_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		name = "FLAPPING";
		flapping_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		name = "PROGRAM_STATUS";
		program_status_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_HOST_STATUS_DATA:
		name = "HOST_STATUS";
		// host_status_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		name = "SERVICE_STATUS";
		// service_status_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_ADAPTIVE_PROGRAM_DATA:
		name = "ADAPTIVE_PROGRAM";
		adaptive_program_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_ADAPTIVE_HOST_DATA:
		name = "ADAPTIVE_HOST";
		adaptive_host_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_ADAPTIVE_SERVICE_DATA:
		name = "ADAPTIVE_SERVICE";
		adaptive_service_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		name = "EXTERNAL_COMMAND";
		external_command_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_AGGREGATED_STATUS_DATA:
		name = "AGGREGATED_STATUS";
		aggregated_status_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_RETENTION_DATA:
		name = "RETENTION";
		retention_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
		name = "CONTACT_NOTIFICATION";
		contact_notification_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		name = "CONTACT_NOTIFICATION_METHOD";
		contact_notification_method_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_ACKNOWLEDGEMENT_DATA:
		name = "ACKNOWLEDGEMENT";
		acknowledgement_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_STATE_CHANGE_DATA:
		name = "STATE_CHANGE";
		statechange_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_CONTACT_STATUS_DATA:
		name = "CONTACT_STATUS";
		contact_status_to_kvvec(kvv, (void*) evt->body);
		break;
	case NEBCALLBACK_ADAPTIVE_CONTACT_DATA:
		name = "ADAPTIVE_CONTACT";
		adaptive_contact_to_kvvec(kvv, (void*) evt->body);
		break;
	default:
		name = "UNKNOWN";
	}

	packed_data = kvvec_to_ekvstr(kvv);
	printf("EVT %s %s\n", name, packed_data);
	free(packed_data);

	kvvec_destroy(kvv, KVVEC_FREE_ALL);
}
