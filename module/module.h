/*
 * This file is included by all module-specific C source files
 * and by quite a few files with shared interest
 */

#ifndef INCLUDE_module_h__
#define INCLUDE_module_h__

#ifndef USE_EVENT_BROKER
# define USE_EVENT_BROKER 1
#endif

#include "node.h"
#include <naemon/naemon.h>
#include <glib.h>

typedef struct file_list {
	char *name;
	struct stat st;
	struct file_list *next;
} file_list;

struct merlin_notify_stats {
	unsigned long net;    /* blocked due to network origin */
	unsigned long peer;   /* blocked due to peer managing it */
	unsigned long poller; /* blocked due to active poller */
	unsigned long master; /* blocked due to config (master sends it) */
	unsigned long sent;   /* allowed through */
};

struct merlin_expired_check {
	struct merlin_node *node;
	void *object;
	time_t added;
	int type;
};

/* 9 = "reason_type", 2 = host/service, 2 = last check active/passive */
extern struct merlin_notify_stats merlin_notify_stats[9][2][2];

extern struct dlist_entry *expired_events;

extern struct host *merlin_recv_host;
extern struct service *merlin_recv_service;

extern bitmap *poller_handled_hosts;
extern bitmap *poller_handled_services;

/* junk to keep track of which node last executed which check */
extern merlin_node **host_check_node;
extern merlin_node **service_check_node;
extern merlin_node *merlin_sender;

extern GHashTable *host_hash_table;
extern node_selection *node_selection_by_hostname(const char *name);

/** global variables exported by Nagios **/
extern int __nagios_object_structure_version;

/** prototypes **/
extern void schedule_expiration_event(int type, merlin_node *node, void *obj);
extern int handle_ipc_event(merlin_node *node, merlin_event *pkt);
extern int handle_event(merlin_node *node, merlin_event *pkt);

int unexpire_service(struct service *s);
int unexpire_host(struct host *h);

extern void set_host_check_node(merlin_node *node, host *h, int flags);
extern void set_service_check_node(merlin_node *node, service *s, int flags);

/**
 * host and service status structures share a *lot* of data,
 * so we can get away with a lot less code by having these
 * large but rather simple macros here
 */
#define MOD2NET_STATE_VARS(mrln, nag) \
	mrln.flap_detection_enabled = nag->flap_detection_enabled; \
	mrln.check_freshness = nag->check_freshness; \
	mrln.process_performance_data = nag->process_performance_data; \
	mrln.checks_enabled = nag->checks_enabled; \
	mrln.event_handler_enabled = nag->event_handler_enabled; \
	mrln.problem_has_been_acknowledged = nag->problem_has_been_acknowledged; \
	mrln.acknowledgement_type = nag->acknowledgement_type; \
	mrln.has_been_checked = nag->has_been_checked; \
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
	mrln.last_check = nag->last_check; \
	mrln.last_state_change = nag->last_state_change; \
	mrln.last_hard_state_change = nag->last_hard_state_change; \
	mrln.current_notification_number = nag->current_notification_number; \
	mrln.current_notification_id = nag->current_notification_id; \
	mrln.check_flapping_recovery_notification = nag->check_flapping_recovery_notification; \
	mrln.scheduled_downtime_depth = nag->scheduled_downtime_depth; \
	mrln.pending_flex_downtime = nag->pending_flex_downtime; \
	mrln.is_flapping = nag->is_flapping; \
	mrln.flapping_comment_id = nag->flapping_comment_id; \
	memcpy(mrln.state_history, nag->state_history, sizeof(mrln.state_history)); \
	mrln.state_history_index = nag->state_history_index; \
	mrln.percent_state_change = nag->percent_state_change; \
	mrln.modified_attributes = nag->modified_attributes; \
	mrln.plugin_output = nag->plugin_output; \
	mrln.long_plugin_output = nag->long_plugin_output; \
	mrln.perf_data = nag->perf_data; \
	/* Nagios 4 only */ \
	mrln.last_notification = nag->last_notification; \
	mrln.next_notification = nag->next_notification; \
	mrln.accept_passive_checks = nag->accept_passive_checks; \
	mrln.notified_on = nag->notified_on; \
	mrln.obsess = nag->obsess


/*
 * Updating data inside the running Nagios is a bit trickier and
 * some care must be taken for this to work
 */
struct tmp_net2mod_data {
	char *old_plugin_output;
	char *old_long_plugin_output;
	char *old_perf_data;
	char *new_plugin_output;
	char *new_long_plugin_output;
	char *new_perf_data;
};
#define NET2MOD_STATE_VARS(tmp, nag, mrln) \
	/* generate new strings before we even start assignments */ \
	tmp.new_plugin_output = safe_strdup(mrln.plugin_output); \
	tmp.new_long_plugin_output = safe_strdup(mrln.long_plugin_output); \
	tmp.new_perf_data = safe_strdup(mrln.perf_data); \
	/* then we must copy the variables so we don't overwrite them */ \
	tmp.old_plugin_output = nag->plugin_output; \
	tmp.old_long_plugin_output = nag->long_plugin_output; \
	tmp.old_perf_data = nag->perf_data; \
	/* next we copy the new data. assignment is atomic */ \
	nag->plugin_output = tmp.new_plugin_output; \
	nag->long_plugin_output = tmp.new_long_plugin_output; \
	nag->perf_data = tmp.new_perf_data; \
	/* then we assign variables (again, this is atomic) */ \
	nag->flap_detection_enabled = mrln.flap_detection_enabled; \
	nag->check_freshness = mrln.check_freshness; \
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
	nag->last_check = mrln.last_check; \
	nag->last_state_change = mrln.last_state_change; \
	nag->last_hard_state_change = mrln.last_hard_state_change; \
	nag->current_notification_number = mrln.current_notification_number; \
	nag->current_notification_id = mrln.current_notification_id; \
	nag->check_flapping_recovery_notification = mrln.check_flapping_recovery_notification; \
	nag->scheduled_downtime_depth = mrln.scheduled_downtime_depth; \
	nag->pending_flex_downtime = mrln.pending_flex_downtime; \
	nag->is_flapping = mrln.is_flapping; \
	memcpy(nag->state_history, mrln.state_history, sizeof(mrln.state_history)); \
	nag->state_history_index = mrln.state_history_index; \
	nag->percent_state_change = mrln.percent_state_change; \
	nag->modified_attributes = mrln.modified_attributes; \
	/* Nagios 4 only */ \
	nag->obsess = mrln.obsess; \
	nag->last_notification = mrln.last_notification; \
	nag->next_notification = mrln.next_notification; \
	nag->accept_passive_checks = mrln.accept_passive_checks; \
	nag->notified_on = mrln.notified_on; \
	/* end Nagios 4 only */ \
	/* when all is done, we free the old state variables */ \
	safe_free(tmp.old_plugin_output); \
	safe_free(tmp.old_long_plugin_output); \
	safe_free(tmp.old_perf_data);

#endif /* MRM_MOD_H */
