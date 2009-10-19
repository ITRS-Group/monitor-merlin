/*
 * This file is included by all module-specific C source files
 * and by quite a few files with shared interest
 */

#ifndef INCLUDE_module_h__
#define INCLUDE_module_h__

#ifndef USE_EVENT_BROKER
# define USE_EVENT_BROKER 1
#endif

#include "shared.h"
#include "hash.h"

#include "nagios/nebmods.h"
#include "nagios/nebmodules.h"
#include "nagios/broker.h"

typedef struct BINLOG_HEAD {
	int type;
	int cmd;
} BINLOG_HEAD;

typedef struct file_list {
	char *name;
	struct stat st;
	struct file_list *next;
} file_list;


/* used for Nagios' objects which we build linked lists for */
typedef struct linked_item {
	void *item;
	struct linked_item *next_item;
} linked_item;


extern hash_table *host_hash_table;
#define hash_find_val(key) (int)hash_find(host_hash_table, key)

/* global variables in the module only */
extern time_t merlin_should_send_paths;
extern void *neb_handle;

/** global variables exported by Nagios **/
extern char *config_file;
extern int event_broker_options;

/** prototypes **/
extern int send_paths(void);
extern int handle_ipc_event(merlin_event *pkt);
extern int cb_handler(int cmd, void *data); /* the callback handler */
extern void file_list_free(struct file_list *list);
extern int mrm_ipc_write(const char *key, struct merlin_event *pkt);
time_t get_last_cfg_change(void);

int print_service_check_data(nebstruct_service_check_data *ds);
int print_blockified_service_check_data(nebstruct_service_check_data *ds, off_t len);

#define disable_checks(sel) enable_disable_checks(sel, 0)
#define enable_checks(sel) enable_disable_checks(sel, 1)
void enable_disable_checks(int selection, int status);
void control_all_distributed_checks(void);
void create_object_lists(void);
void handle_control(int code, int selection);

extern int register_merlin_hooks(void);
extern int deregister_merlin_hooks(void);
extern int merlin_mod_hook(int cb, void *data);

/**
 * host and service status structures share a *lot* of data,
 * so we can get away with a lot less code by having these
 * large but rather simple macros here
 */
#define MOD2NET_STATE_VARS(mrln, nag) \
	mrln.initial_state = nag->initial_state; \
	mrln.flap_detection_enabled = nag->flap_detection_enabled; \
	mrln.low_flap_threshold = nag->low_flap_threshold;  \
	mrln.high_flap_threshold = nag->high_flap_threshold; \
	mrln.check_freshness = nag->check_freshness; \
	mrln.freshness_threshold = nag->freshness_threshold; \
	mrln.process_performance_data = nag->process_performance_data; \
	mrln.checks_enabled = nag->checks_enabled; \
	mrln.event_handler_enabled = nag->event_handler_enabled; \
	mrln.problem_has_been_acknowledged = nag->problem_has_been_acknowledged; \
	mrln.acknowledgement_type = nag->acknowledgement_type; \
	mrln.check_type = nag->check_type; \
	mrln.current_state = nag->current_state; \
	mrln.last_state = nag->last_state; \
	mrln.last_hard_state = nag->last_hard_state; \
	mrln.state_type = nag->state_type; \
	mrln.current_attempt = nag->current_attempt; \
	mrln.current_event_id = nag->current_event_id; \
	mrln.last_event_id = nag->last_event_id; \
	mrln.current_problem_id = nag->current_problem_id; \
	mrln.last_problem_id = nag->last_problem_id; \
	mrln.latency = nag->latency; \
	mrln.execution_time = nag->execution_time; \
	mrln.notifications_enabled = nag->notifications_enabled; \
	mrln.next_check = nag->next_check; \
	mrln.should_be_scheduled = nag->should_be_scheduled; \
	mrln.last_check = nag->last_check; \
	mrln.last_state_change = nag->last_state_change; \
	mrln.last_hard_state_change = nag->last_hard_state_change; \
	mrln.has_been_checked = nag->has_been_checked; \
	mrln.current_notification_number = nag->current_notification_number; \
	mrln.current_notification_id = nag->current_notification_id; \
	mrln.check_flapping_recovery_notification = nag->check_flapping_recovery_notification; \
	mrln.scheduled_downtime_depth = nag->scheduled_downtime_depth; \
	mrln.pending_flex_downtime = nag->pending_flex_downtime; \
	mrln.is_flapping = nag->is_flapping; \
	mrln.flapping_comment_id = nag->flapping_comment_id; \
	mrln.percent_state_change = nag->percent_state_change; \
	mrln.plugin_output = nag->plugin_output; \
	mrln.long_plugin_output = nag->long_plugin_output; \
	mrln.perf_data = nag->perf_data;

#define NET2MOD_STATE_VARS(nag, mrln) \
	nag->plugin_output = xfree(nag->plugin_output); \
	nag->long_plugin_output = xfree(nag->long_plugin_output); \
	nag->perf_data = xfree(nag->perf_data); \
	nag->initial_state = mrln.initial_state; \
	nag->flap_detection_enabled = mrln.flap_detection_enabled; \
	nag->low_flap_threshold = mrln.low_flap_threshold; \
	nag->high_flap_threshold = mrln.high_flap_threshold; \
	nag->check_freshness = mrln.check_freshness; \
	nag->freshness_threshold = mrln.freshness_threshold; \
	nag->process_performance_data = mrln.process_performance_data; \
	nag->checks_enabled = mrln.checks_enabled; \
	nag->event_handler_enabled = mrln.event_handler_enabled; \
	nag->problem_has_been_acknowledged = mrln.problem_has_been_acknowledged; \
	nag->acknowledgement_type = mrln.acknowledgement_type; \
	nag->check_type = mrln.check_type; \
	nag->current_state = mrln.current_state; \
	nag->last_state = mrln.last_state; \
	nag->last_hard_state = mrln.last_hard_state; \
	nag->state_type = mrln.state_type; \
	nag->current_attempt = mrln.current_attempt; \
	nag->current_event_id = mrln.current_event_id; \
	nag->last_event_id = mrln.last_event_id; \
	nag->current_problem_id = mrln.current_problem_id; \
	nag->last_problem_id = mrln.last_problem_id; \
	nag->latency = mrln.latency; \
	nag->execution_time = mrln.execution_time; \
	nag->notifications_enabled = mrln.notifications_enabled; \
	nag->next_check = mrln.next_check; \
	nag->should_be_scheduled = mrln.should_be_scheduled; \
	nag->last_check = mrln.last_check; \
	nag->last_state_change = mrln.last_state_change; \
	nag->last_hard_state_change = mrln.last_hard_state_change; \
	nag->has_been_checked = mrln.has_been_checked; \
	nag->current_notification_number = mrln.current_notification_number; \
	nag->current_notification_id = mrln.current_notification_id; \
	nag->check_flapping_recovery_notification = mrln.check_flapping_recovery_notification; \
	nag->scheduled_downtime_depth = mrln.scheduled_downtime_depth; \
	nag->pending_flex_downtime = mrln.pending_flex_downtime; \
	nag->is_flapping = mrln.is_flapping; \
	nag->flapping_comment_id = mrln.flapping_comment_id; \
	nag->percent_state_change = mrln.percent_state_change; \
	nag->plugin_output = strdup(mrln.plugin_output); \
	nag->long_plugin_output = strdup(mrln.long_plugin_output); \
	nag->perf_data = strdup(mrln.perf_data);

#endif /* MRM_MOD_H */
