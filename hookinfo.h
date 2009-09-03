/**
 * Conversion table to blockify and de-blockify data-structs sent from
 * Nagios to event broker modules.
 *
 * To add more checks here, first go check in nagios/include/nebcallbacks.h
 * to figure out the thing you want to add stuff to. Then find the
 * corresponding data struct in nagios/include/nebstructs.h and add the
 * necessary info.
 *
 * 'strings' is the number of dynamic strings (char *)
 * 'offset' is 'sizeof(nebstruct_something_data)'
 * 'ptrs[]' are the offsets of each char *. use the offsetof() macro
 */
#ifndef INCLUDE_hookinfo_h__
#define INCLUDE_hookinfo_h__

#include "shared.h"

static struct hook_info_struct {
	int cb_type;
	int strings;
	off_t offset, ptrs[6];
} hook_info[NEBCALLBACK_NUMITEMS] = {
	{ NEBCALLBACK_RESERVED0, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RESERVED1, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RESERVED2, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RESERVED3, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RESERVED4, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RAW_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_NEB_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_PROCESS_DATA, 0, sizeof(nebstruct_process_data),
		{ 0, 0, 0, 0, 0 },
	},
	{ NEBCALLBACK_TIMED_EVENT_DATA, 0, sizeof(nebstruct_timed_event_data),
		{ 0, 0, 0, 0, 0 },
	},
	{ NEBCALLBACK_LOG_DATA, 1, sizeof(nebstruct_log_data),
		{
			offsetof(nebstruct_log_data, data),
			0, 0, 0, 0
		},
	},
	{ NEBCALLBACK_SYSTEM_COMMAND_DATA, 2, sizeof(nebstruct_system_command_data),
		{
			offsetof(nebstruct_system_command_data, command_line),
			offsetof(nebstruct_system_command_data, output),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_EVENT_HANDLER_DATA, 4, sizeof(nebstruct_event_handler_data),
		{
			offsetof(nebstruct_event_handler_data, host_name),
			offsetof(nebstruct_event_handler_data, service_description),
			offsetof(nebstruct_event_handler_data, command_line),
			offsetof(nebstruct_event_handler_data, output),
			0
		},
	},
	{ NEBCALLBACK_NOTIFICATION_DATA, 5, sizeof(nebstruct_notification_data),
		{
			offsetof(nebstruct_notification_data, host_name),
			offsetof(nebstruct_notification_data, service_description),
			offsetof(nebstruct_notification_data, output),
			offsetof(nebstruct_notification_data, ack_author),
			offsetof(nebstruct_notification_data, ack_data),
		},
	},
	{ NEBCALLBACK_SERVICE_CHECK_DATA, 4, sizeof(nebstruct_service_check_data),
		{
			offsetof(nebstruct_service_check_data, host_name),
			offsetof(nebstruct_service_check_data, service_description),
			offsetof(nebstruct_service_check_data, output),
			offsetof(nebstruct_service_check_data, perf_data),
			0
		},
	},
	{ NEBCALLBACK_HOST_CHECK_DATA, 3, sizeof(nebstruct_host_check_data),
		{
			offsetof(nebstruct_host_check_data, host_name),
			offsetof(nebstruct_host_check_data, output),
			offsetof(nebstruct_host_check_data, perf_data),
			0, 0
		}
	},
	{ NEBCALLBACK_COMMENT_DATA, 4, sizeof(nebstruct_comment_data),
		{
			offsetof(nebstruct_comment_data, host_name),
			offsetof(nebstruct_comment_data, service_description),
			offsetof(nebstruct_comment_data, author_name),
			offsetof(nebstruct_comment_data, comment_data),
			0,
		},
	},
	{ NEBCALLBACK_DOWNTIME_DATA, 4, sizeof(nebstruct_downtime_data),
		{
			offsetof(nebstruct_downtime_data, host_name),
			offsetof(nebstruct_downtime_data, service_description),
			offsetof(nebstruct_downtime_data, author_name),
			offsetof(nebstruct_downtime_data, comment_data),
			0,
		},
	},
	{ NEBCALLBACK_FLAPPING_DATA, 2, sizeof(nebstruct_flapping_data),
		{
			offsetof(nebstruct_flapping_data, host_name),
			offsetof(nebstruct_flapping_data, service_description),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_PROGRAM_STATUS_DATA, 2, sizeof(nebstruct_program_status_data),
		{
			offsetof(nebstruct_program_status_data, global_host_event_handler),
			offsetof(nebstruct_program_status_data, global_service_event_handler),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_HOST_STATUS_DATA, 4, sizeof(merlin_host_status),
		{
			offsetof(merlin_host_status, state.plugin_output),
			offsetof(merlin_host_status, state.long_plugin_output),
			offsetof(merlin_host_status, state.perf_data),
			offsetof(merlin_host_status, name),
			0
		},
	},
	{ NEBCALLBACK_SERVICE_STATUS_DATA, 5, sizeof(merlin_service_status),
		{
			offsetof(merlin_service_status, state.plugin_output),
			offsetof(merlin_service_status, state.long_plugin_output),
			offsetof(merlin_service_status, state.perf_data),
			offsetof(merlin_service_status, host_name),
			offsetof(merlin_service_status, service_description)
		}
	},
	{ NEBCALLBACK_ADAPTIVE_PROGRAM_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_HOST_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_SERVICE_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_EXTERNAL_COMMAND_DATA, 2, sizeof(nebstruct_external_command_data),
		{
			offsetof(nebstruct_external_command_data, command_string),
			offsetof(nebstruct_external_command_data, command_args),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_AGGREGATED_STATUS_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RETENTION_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_CONTACT_NOTIFICATION_DATA, 6, sizeof(nebstruct_contact_notification_data),
		{
			offsetof(nebstruct_contact_notification_data, host_name),
			offsetof(nebstruct_contact_notification_data, service_description),
			offsetof(nebstruct_contact_notification_data, contact_name),
			offsetof(nebstruct_contact_notification_data, output),
			offsetof(nebstruct_contact_notification_data, ack_author),
			offsetof(nebstruct_contact_notification_data, ack_data),
		},
	},
	{ NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ACKNOWLEDGEMENT_DATA, 4, sizeof(nebstruct_acknowledgement_data),
		{
			offsetof(nebstruct_acknowledgement_data, host_name),
			offsetof(nebstruct_acknowledgement_data, service_description),
			offsetof(nebstruct_acknowledgement_data, author_name),
			offsetof(nebstruct_acknowledgement_data, comment_data),
			0
		},
	},
	{ NEBCALLBACK_STATE_CHANGE_DATA, 3, sizeof(nebstruct_statechange_data),
		{
			offsetof(nebstruct_statechange_data, host_name),
			offsetof(nebstruct_statechange_data, service_description),
			offsetof(nebstruct_statechange_data, output),
			0, 0
		},
	},
	{ NEBCALLBACK_CONTACT_STATUS_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_CONTACT_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
};

#endif /* INCLUDE_hookinfo_h__ */
