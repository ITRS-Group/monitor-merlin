#ifndef ocimp_h__
#define ocimp_h__

#define CFG_IGNORE (-1)
#define CFG_acknowledgement_type CFG_IGNORE
#define CFG_active_checks_enabled 2
#define CFG_active_host_checks_enabled 3
#define CFG_active_ondemand_host_check_stats 4
#define CFG_active_ondemand_service_check_stats 5
#define CFG_active_scheduled_host_check_stats 6
#define CFG_active_scheduled_service_check_stats 7
#define CFG_active_service_checks_enabled 8
#define CFG_author 9
#define CFG_cached_host_check_stats 10
#define CFG_cached_service_check_stats 11
#define CFG_check_command 12
#define CFG_check_execution_time 13
#define CFG_check_host_freshness 14
#define CFG_check_interval 15
#define CFG_check_latency 16
#define CFG_check_options CFG_IGNORE
#define CFG_check_period 18
#define CFG_check_service_freshness 19
#define CFG_check_type 20
#define CFG_comment 21
#define CFG_comment_data 22
#define CFG_comment_id 23
#define CFG_contact_name 24
#define CFG_created 25
#define CFG_current_attempt 26
#define CFG_current_event_id 27
#define CFG_current_notification_id 28
#define CFG_current_notification_number 29
#define CFG_current_problem_id 30
#define CFG_current_state 31
#define CFG_daemon_mode 32
#define CFG_downtime_id 33
#define CFG_duration 34
#define CFG_enable_event_handlers 35
#define CFG_enable_failure_prediction CFG_IGNORE
#define CFG_enable_flap_detection 37
#define CFG_enable_notifications 38
#define CFG_end_time 39
#define CFG_entry_time 40
#define CFG_entry_type 41
#define CFG_event_handler 42
#define CFG_event_handler_enabled 43
#define CFG_expires 44
#define CFG_expire_time 45
#define CFG_external_command_stats 46
#define CFG_failure_prediction_enabled CFG_IGNORE
#define CFG_fixed 48
#define CFG_flap_detection_enabled 49
#define CFG_global_host_event_handler 50
#define CFG_global_service_event_handler 51
#define CFG_has_been_checked 52
#define CFG_high_external_command_buffer_slots 53
#define CFG_host_name 54
#define CFG_host_notification_period 55
#define CFG_host_notifications_enabled 56
#define CFG_is_flapping 57
#define CFG_last_check 58
#define CFG_last_command_check 59
#define CFG_last_event_id 60
#define CFG_last_hard_state 61
#define CFG_last_hard_state_change 62
#define CFG_last_host_notification 63
#define CFG_last_log_rotation 64
#define CFG_last_notification 65
#define CFG_last_problem_id 66
#define CFG_last_service_notification 67
#define CFG_last_state 68
#define CFG_last_state_change 69
#define CFG_last_time_critical CFG_IGNORE
#define CFG_last_time_down CFG_IGNORE
#define CFG_last_time_ok CFG_IGNORE
#define CFG_last_time_unknown CFG_IGNORE
#define CFG_last_time_unreachable CFG_IGNORE
#define CFG_last_time_up CFG_IGNORE
#define CFG_last_time_warning CFG_IGNORE
#define CFG_last_update 77
#define CFG_last_update_check 78
#define CFG_last_version 79
#define CFG_long_plugin_output 80
#define CFG_max_attempts 81
#define CFG_modified_attributes CFG_IGNORE
#define CFG_modified_host_attributes 83
#define CFG_modified_service_attributes 84
#define CFG_nagios_pid 85
#define CFG_new_version 87
#define CFG_next_check 87
#define CFG_next_comment_id 89
#define CFG_next_downtime_id 90
#define CFG_next_event_id 91
#define CFG_next_notification 92
#define CFG_next_notification_id 93
#define CFG_next_problem_id 94
#define CFG_no_more_notifications CFG_IGNORE
#define CFG_notification_period 96
#define CFG_notifications_enabled 97
#define CFG_obsess_over_host 98
#define CFG_obsess_over_hosts 99
#define CFG_obsess_over_service 100
#define CFG_obsess_over_services 101
#define CFG_parallel_host_check_stats 102
#define CFG_passive_checks_enabled 103
#define CFG_passive_host_checks_enabled 104
#define CFG_passive_host_check_stats 105
#define CFG_passive_service_checks_enabled 106
#define CFG_passive_service_check_stats 107
#define CFG_percent_state_change 108
#define CFG_performance_data 109
#define CFG_persistent 110
#define CFG_plugin_output 111
#define CFG_problem_has_been_acknowledged 112
#define CFG_process_performance_data 113
#define CFG_program_start 114
#define CFG_retry_interval 115
#define CFG_scheduled_downtime_depth 116
#define CFG_serial_host_check_stats 117
#define CFG_service_description 118
#define CFG_service_notification_period 119
#define CFG_service_notifications_enabled 120
#define CFG_should_be_scheduled 121
#define CFG_source 122
#define CFG_start_time 123
#define CFG_state_type 124
#define CFG_total_external_command_buffer_slots 125
#define CFG_triggered_by 126
#define CFG_update_available 127
#define CFG_used_external_command_buffer_slots 128
#define CFG_version 129
/* below are found only in objects.cache */
#define CFG_action_url 200
#define CFG_address 201
#define CFG_alias 202
#define CFG_check_freshness 203
#define CFG_contact_groups 204
#define CFG_contacts 205
#define CFG_display_name 206
#define CFG_first_notification_delay 207
#define CFG_flap_detection_options 208
#define CFG_freshness_threshold 209
#define CFG_high_flap_threshold 210
#define CFG_icon_image 211
#define CFG_initial_state 212
#define CFG_is_volatile 213
#define CFG_low_flap_threshold 214
#define CFG_max_check_attempts CFG_max_attempts
#define CFG_notes 216
#define CFG_notes_url 217
#define CFG_notification_interval 218
#define CFG_notification_options 219
#define CFG_parallelize_check 220
#define CFG_parents 221
#define CFG_process_perf_data 222
#define CFG_retain_nonstatus_information 223
#define CFG_retain_status_information 224
#define CFG_stalking_options 225
#define CFG_statusmap_image CFG_IGNORE
#define CFG_icon_image_alt 227

/* for contacts. We should probably optimize this somewhat */
#define CFG_service_notification_options 227
#define CFG_host_notification_options 228
#define CFG_service_notification_commands 229
#define CFG_host_notification_commands 230
#define CFG_email 231
#define CFG_pager 232
#define CFG_can_submit_commands 233
#define CFG_address1 234
#define CFG_address2 235
#define CFG_address3 236
#define CFG_address4 237
#define CFG_address5 238
#define CFG_address6 239


/*
 * this matches merlin_service_status exactly up until the additional
 * variables. This means we can reuse this struct as if it was a
 * merlin_service_status (or merlin_host_status) struct so long as we
 * only pass it as a pointer.
 */
struct id_object {
	char *host_name;
	char *service_description;
	int id, instance_id;
};
typedef struct id_object id_object;

/* we reuse these for timeperiods */
struct ocimp_group_object {
	char *name;
	int id;
	union {
		char *exclude;
		char *members;
	};
	strvec *strv;
};
typedef struct ocimp_group_object ocimp_group_object;
typedef struct ocimp_group_object ocimp_timeperiod_object;

struct ocimp_contact_object {
	char *name;
	int id;
	int login_enabled;
};
typedef struct ocimp_contact_object ocimp_contact_object;

struct state_object
{
	monitored_object_state state;
	id_object ido;

	char *notification_period;
	char *check_period;
	char *check_command;
	int check_interval;
	int retry_interval;
	char *event_handler;
	int last_update;
	int max_attempts;

	char *action_url;
	char *address;
	char *alias;
	char *display_name;
	char *stalking_options;
	char *flap_detection_options;
	char *icon_image;
	char *icon_image_alt;
	char *notes;
	char *notes_url;
	char *notification_options;
	int first_notification_delay;
	double freshness_threshold;
	double high_flap_threshold;
	double low_flap_threshold;
	int initial_state;
	int is_volatile;
	int notification_interval;
	int parallelize_check;
	int process_perf_data;
	int retain_nonstatus_information;
	int retain_status_information;
	char *parents;
	char *contact_groups;
	char *contacts;

	slist *contact_slist;
};
typedef struct state_object state_object;

struct comment_object
{
	int entry_type;
	int comment_id;
	int source;
	int persistent;
	int entry_time;
	int expires;
	int expire_time;
};
typedef struct comment_object comment_object;

typedef struct cfg_code {
	long len;
	const char *key;
	long code;
} cfg_code;

#define get_cfg_code(v, codes) real_get_cfg_code(v, codes, ARRAY_SIZE(codes))

#define OCIMP_CFG_ENTRY(config_key) \
	sizeof(#config_key) - 1, #config_key, CFG_##config_key
static cfg_code slog_options[] = {
	{ OCIMP_CFG_ENTRY(acknowledgement_type) },
	{ OCIMP_CFG_ENTRY(active_checks_enabled) },
	{ OCIMP_CFG_ENTRY(active_host_checks_enabled) },
	{ OCIMP_CFG_ENTRY(active_ondemand_host_check_stats) },
	{ OCIMP_CFG_ENTRY(active_ondemand_service_check_stats) },
	{ OCIMP_CFG_ENTRY(active_scheduled_host_check_stats) },
	{ OCIMP_CFG_ENTRY(active_scheduled_service_check_stats) },
	{ OCIMP_CFG_ENTRY(active_service_checks_enabled) },
	{ OCIMP_CFG_ENTRY(author) },
	{ OCIMP_CFG_ENTRY(cached_host_check_stats) },
	{ OCIMP_CFG_ENTRY(cached_service_check_stats) },
	{ OCIMP_CFG_ENTRY(check_command) },
	{ OCIMP_CFG_ENTRY(check_execution_time) },
	{ OCIMP_CFG_ENTRY(check_host_freshness) },
	{ OCIMP_CFG_ENTRY(check_interval) },
	{ OCIMP_CFG_ENTRY(check_latency) },
	{ OCIMP_CFG_ENTRY(check_options) },
	{ OCIMP_CFG_ENTRY(check_period) },
	{ OCIMP_CFG_ENTRY(check_service_freshness) },
	{ OCIMP_CFG_ENTRY(check_type) },
	{ OCIMP_CFG_ENTRY(comment) },
	{ OCIMP_CFG_ENTRY(comment_data) },
	{ OCIMP_CFG_ENTRY(comment_id) },
	{ OCIMP_CFG_ENTRY(contact_name) },
	{ OCIMP_CFG_ENTRY(created) },
	{ OCIMP_CFG_ENTRY(current_attempt) },
	{ OCIMP_CFG_ENTRY(current_event_id) },
	{ OCIMP_CFG_ENTRY(current_notification_id) },
	{ OCIMP_CFG_ENTRY(current_notification_number) },
	{ OCIMP_CFG_ENTRY(current_problem_id) },
	{ OCIMP_CFG_ENTRY(current_state) },
	{ OCIMP_CFG_ENTRY(daemon_mode) },
	{ OCIMP_CFG_ENTRY(downtime_id) },
	{ OCIMP_CFG_ENTRY(duration) },
	{ OCIMP_CFG_ENTRY(enable_event_handlers) },
	{ OCIMP_CFG_ENTRY(enable_failure_prediction) },
	{ OCIMP_CFG_ENTRY(enable_flap_detection) },
	{ OCIMP_CFG_ENTRY(enable_notifications) },
	{ OCIMP_CFG_ENTRY(end_time) },
	{ OCIMP_CFG_ENTRY(entry_time) },
	{ OCIMP_CFG_ENTRY(entry_type) },
	{ OCIMP_CFG_ENTRY(event_handler) },
	{ OCIMP_CFG_ENTRY(event_handler_enabled) },
	{ OCIMP_CFG_ENTRY(expires) },
	{ OCIMP_CFG_ENTRY(expire_time) },
	{ OCIMP_CFG_ENTRY(external_command_stats) },
	{ OCIMP_CFG_ENTRY(failure_prediction_enabled) },
	{ OCIMP_CFG_ENTRY(fixed) },
	{ OCIMP_CFG_ENTRY(flap_detection_enabled) },
	{ OCIMP_CFG_ENTRY(global_host_event_handler) },
	{ OCIMP_CFG_ENTRY(global_service_event_handler) },
	{ OCIMP_CFG_ENTRY(has_been_checked) },
	{ OCIMP_CFG_ENTRY(high_external_command_buffer_slots) },
	{ OCIMP_CFG_ENTRY(host_name) },
	{ OCIMP_CFG_ENTRY(host_notification_period) },
	{ OCIMP_CFG_ENTRY(host_notifications_enabled) },
	{ OCIMP_CFG_ENTRY(is_flapping) },
	{ OCIMP_CFG_ENTRY(last_check) },
	{ OCIMP_CFG_ENTRY(last_command_check) },
	{ OCIMP_CFG_ENTRY(last_event_id) },
	{ OCIMP_CFG_ENTRY(last_hard_state) },
	{ OCIMP_CFG_ENTRY(last_hard_state_change) },
	{ OCIMP_CFG_ENTRY(last_host_notification) },
	{ OCIMP_CFG_ENTRY(last_log_rotation) },
	{ OCIMP_CFG_ENTRY(last_notification) },
	{ OCIMP_CFG_ENTRY(last_problem_id) },
	{ OCIMP_CFG_ENTRY(last_service_notification) },
	{ OCIMP_CFG_ENTRY(last_state_change) },
	{ OCIMP_CFG_ENTRY(last_time_critical) },
	{ OCIMP_CFG_ENTRY(last_time_down) },
	{ OCIMP_CFG_ENTRY(last_time_ok) },
	{ OCIMP_CFG_ENTRY(last_time_unknown) },
	{ OCIMP_CFG_ENTRY(last_time_unreachable) },
	{ OCIMP_CFG_ENTRY(last_time_up) },
	{ OCIMP_CFG_ENTRY(last_time_warning) },
	{ OCIMP_CFG_ENTRY(last_update) },
	{ OCIMP_CFG_ENTRY(last_update_check) },
	{ OCIMP_CFG_ENTRY(last_version) },
	{ OCIMP_CFG_ENTRY(long_plugin_output) },
	{ OCIMP_CFG_ENTRY(max_attempts) },
	{ OCIMP_CFG_ENTRY(modified_attributes) },
	{ OCIMP_CFG_ENTRY(modified_host_attributes) },
	{ OCIMP_CFG_ENTRY(modified_service_attributes) },
	{ OCIMP_CFG_ENTRY(nagios_pid) },
	{ OCIMP_CFG_ENTRY(new_version) },
	{ OCIMP_CFG_ENTRY(next_check) },
	{ OCIMP_CFG_ENTRY(next_comment_id) },
	{ OCIMP_CFG_ENTRY(next_downtime_id) },
	{ OCIMP_CFG_ENTRY(next_event_id) },
	{ OCIMP_CFG_ENTRY(next_notification) },
	{ OCIMP_CFG_ENTRY(next_notification_id) },
	{ OCIMP_CFG_ENTRY(next_problem_id) },
	{ OCIMP_CFG_ENTRY(no_more_notifications) },
	{ OCIMP_CFG_ENTRY(notification_period) },
	{ OCIMP_CFG_ENTRY(notifications_enabled) },
	{ OCIMP_CFG_ENTRY(obsess_over_host) },
	{ OCIMP_CFG_ENTRY(obsess_over_hosts) },
	{ OCIMP_CFG_ENTRY(obsess_over_service) },
	{ OCIMP_CFG_ENTRY(obsess_over_services) },
	{ OCIMP_CFG_ENTRY(parallel_host_check_stats) },
	{ OCIMP_CFG_ENTRY(passive_checks_enabled) },
	{ OCIMP_CFG_ENTRY(passive_host_checks_enabled) },
	{ OCIMP_CFG_ENTRY(passive_host_check_stats) },
	{ OCIMP_CFG_ENTRY(passive_service_checks_enabled) },
	{ OCIMP_CFG_ENTRY(passive_service_check_stats) },
	{ OCIMP_CFG_ENTRY(percent_state_change) },
	{ OCIMP_CFG_ENTRY(performance_data) },
	{ OCIMP_CFG_ENTRY(persistent) },
	{ OCIMP_CFG_ENTRY(plugin_output) },
	{ OCIMP_CFG_ENTRY(problem_has_been_acknowledged) },
	{ OCIMP_CFG_ENTRY(process_performance_data) },
	{ OCIMP_CFG_ENTRY(program_start) },
	{ OCIMP_CFG_ENTRY(retry_interval) },
	{ OCIMP_CFG_ENTRY(scheduled_downtime_depth) },
	{ OCIMP_CFG_ENTRY(serial_host_check_stats) },
	{ OCIMP_CFG_ENTRY(service_description) },
	{ OCIMP_CFG_ENTRY(service_notification_period) },
	{ OCIMP_CFG_ENTRY(service_notifications_enabled) },
	{ OCIMP_CFG_ENTRY(should_be_scheduled) },
	{ OCIMP_CFG_ENTRY(source) },
	{ OCIMP_CFG_ENTRY(start_time) },
	{ OCIMP_CFG_ENTRY(state_type) },
	{ OCIMP_CFG_ENTRY(total_external_command_buffer_slots) },
	{ OCIMP_CFG_ENTRY(triggered_by) },
	{ OCIMP_CFG_ENTRY(update_available) },
	{ OCIMP_CFG_ENTRY(used_external_command_buffer_slots) },
	{ OCIMP_CFG_ENTRY(version) },
	{ OCIMP_CFG_ENTRY(action_url) },
	{ OCIMP_CFG_ENTRY(address) },
	{ OCIMP_CFG_ENTRY(alias) },
	{ OCIMP_CFG_ENTRY(check_freshness) },
	{ OCIMP_CFG_ENTRY(contact_groups) },
	{ OCIMP_CFG_ENTRY(contacts) },
	{ OCIMP_CFG_ENTRY(display_name) },
	{ OCIMP_CFG_ENTRY(first_notification_delay) },
	{ OCIMP_CFG_ENTRY(flap_detection_options) },
	{ OCIMP_CFG_ENTRY(freshness_threshold) },
	{ OCIMP_CFG_ENTRY(high_flap_threshold) },
	{ OCIMP_CFG_ENTRY(icon_image) },
	{ OCIMP_CFG_ENTRY(icon_image_alt) },
	{ OCIMP_CFG_ENTRY(initial_state) },
	{ OCIMP_CFG_ENTRY(is_volatile) },
	{ OCIMP_CFG_ENTRY(low_flap_threshold) },
	{ OCIMP_CFG_ENTRY(max_check_attempts) },
	{ OCIMP_CFG_ENTRY(notes) },
	{ OCIMP_CFG_ENTRY(notes_url) },
	{ OCIMP_CFG_ENTRY(notification_interval) },
	{ OCIMP_CFG_ENTRY(notification_options) },
	{ OCIMP_CFG_ENTRY(parallelize_check) },
	{ OCIMP_CFG_ENTRY(parents) },
	{ OCIMP_CFG_ENTRY(process_perf_data) },
	{ OCIMP_CFG_ENTRY(retain_nonstatus_information) },
	{ OCIMP_CFG_ENTRY(retain_status_information) },
	{ OCIMP_CFG_ENTRY(stalking_options) },
	{ OCIMP_CFG_ENTRY(statusmap_image) },
	{ OCIMP_CFG_ENTRY(service_notification_options) },
	{ OCIMP_CFG_ENTRY(host_notification_options) },
	{ OCIMP_CFG_ENTRY(service_notification_commands) },
	{ OCIMP_CFG_ENTRY(host_notification_commands) },
	{ OCIMP_CFG_ENTRY(email) },
	{ OCIMP_CFG_ENTRY(pager) },
	{ OCIMP_CFG_ENTRY(can_submit_commands) },
};

/*
 * This basic query is used for both hosts and services
 */
#define INSERT_QUERY(type, vals, valsf) \
	"INSERT INTO " type " (" vals ",\n" \
	"id, instance_id,\n" \
	"host_name, display_name, stalking_options,\n"   /* 5 */ \
	"flap_detection_options, icon_image, notes,\n" \
	"notes_url, notification_options,\n"             /* 10 */ \
	"initial_state, flap_detection_enabled,\n" \
	"low_flap_threshold, high_flap_threshold,\n" \
	"check_freshness, freshness_threshold,\n" \
	"process_performance_data,\n" \
	"active_checks_enabled, passive_checks_enabled,\n" \
	"event_handler_enabled,\n"                            /* 20 */ \
	"obsess_over_" type ", problem_has_been_acknowledged,\n" \
	"acknowledgement_type, check_type,\n" \
	"current_state, last_state,\n" \
	"last_hard_state, state_type,\n" \
	"current_attempt, current_event_id,\n"                /* 30 */ \
	"last_event_id, current_problem_id,\n" \
	"last_problem_id,\n" \
	"latency, execution_time,\n" \
	"notifications_enabled,\n" \
	"last_notification,\n" \
	"next_check, should_be_scheduled, last_check,\n"      /* 40 */ \
	"last_state_change, last_hard_state_change,\n" \
	"has_been_checked,\n" \
	"current_notification_number, current_notification_id,\n"  /* 45 */ \
	"check_flapping_recovery_notifi,\n" \
	"scheduled_downtime_depth, pending_flex_downtime,\n" \
	"is_flapping, flapping_comment_id,\n"                   /* 50 */ \
	"max_attempts, max_check_attempts,\n" \
	"percent_state_change, output, long_output,\n"          /* 55 */ \
	"perf_data, action_url, icon_image_alt,\n" \
	"check_command, check_period,\n"                        /* 60 */ \
	"notification_period, retry_interval,\n" \
	"check_interval, first_notification_delay,\n" \
	"is_volatile,\n"                                        /* 65 */ \
	"next_notification, notification_interval,\n" \
	"parallelize_check,\n" \
	"retain_nonstatus_information, retain_status_information\n"  /* 70 */ \
	") VALUES(" valsf ",\n" \
	"%d, %d,\n" \
	"%s, %s, %s,\n"   /* 5 */   \
	"%s, %s, %s,\n" \
	"%s, %s,\n"       /* 10 */  \
	"%d, %d,\n" \
	"%f, %f,\n" \
	"%d, %d,\n"       /* 16 */ \
	"%d,\n" \
	"%d, %d,\n" \
	"%d,\n"           /* 20 */ \
	"%d, %d,\n" \
	"%d, %d,\n" \
	"%d, %d,\n"       /* 26 */ \
	"%d, %d,\n" \
	"%d, %lu,\n"      /* 30 */ \
	"%lu, %lu,\n" \
	"%lu,\n" \
	"%f, %lf,\n"      /* 35 */ \
	"%d,\n" \
	"%lu,\n" \
	"%lu, %d, %lu,\n" /* 40 */ \
	"%lu, %lu,\n" \
	"%d,\n" \
	"%d, %lu,\n" \
	"%d,\n" \
	"%d, %d,\n" \
	"%d, %lu,\n"      /* 50 */ \
	"%d, %d,\n" \
	"%f, %s, %s,\n" \
	"%s, %s, %s,\n" \
	"%s, %s,\n"       /* 60 */ \
	"%s, %d,\n" \
	"%d, %d,\n" \
	"%d,\n"          /* 65 */ \
	"%lu, %d,\n" \
	"%d,\n" \
	"%d, %d\n"      /* 70 */ \
	")"

#define INSERT_VALUES() \
	p->ido.id, p->ido.instance_id, \
	host_name, safe_str(display_name), safe_str(stalking_options), \
	safe_str(flap_detection_options), safe_str(icon_image), safe_str(notes), \
	safe_str(notes_url), safe_str(notification_options), \
	p->state.initial_state, p->state.flap_detection_enabled, \
	p->state.low_flap_threshold, p->state.high_flap_threshold, \
	p->state.check_freshness, p->state.freshness_threshold, \
	p->state.process_performance_data, \
	p->state.checks_enabled, p->state.accept_passive_checks, \
	p->state.event_handler_enabled, \
	p->state.obsess, p->state.problem_has_been_acknowledged, \
	p->state.acknowledgement_type, p->state.check_type, \
	p->state.current_state, p->state.last_state, \
	p->state.last_hard_state, p->state.state_type, \
	p->state.current_attempt, p->state.current_event_id, \
	p->state.last_event_id, p->state.current_problem_id, \
	p->state.last_problem_id, \
	p->state.latency, p->state.execution_time, \
	p->state.notifications_enabled, \
	p->state.last_notification, \
	p->state.next_check, p->state.should_be_scheduled, p->state.last_check, \
	p->state.last_state_change, p->state.last_hard_state_change, \
	p->state.has_been_checked, \
	p->state.current_notification_number, p->state.current_notification_id, \
	p->state.check_flapping_recovery_notification, \
	p->state.scheduled_downtime_depth, p->state.pending_flex_downtime, \
	p->state.is_flapping, p->state.flapping_comment_id, \
	p->max_attempts, p->max_attempts, \
	p->state.percent_state_change, safe_str(output), safe_str(long_output), \
	safe_str(perf_data), safe_str(action_url), safe_str(icon_image_alt), \
	safe_str(check_command), safe_str(check_period), \
	safe_str(notification_period), p->retry_interval, \
	p->check_interval, p->first_notification_delay, \
	p->is_volatile, \
	p->state.next_notification, p->notification_interval, \
	p->parallelize_check, \
	p->retain_nonstatus_information, p->retain_status_information

#endif
