/*
 * shared.h: Header file for code shared between module and daemon
 */

#ifndef INCLUDE_shared_h__
#define INCLUDE_shared_h__

#define NSCORE

#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif

/** common include files required practically everywhere **/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>
#include <ctype.h>

#include "compat.h"
#include "node.h"
#include "io.h"
#include "ipc.h"
#include "logging.h"
#include "cfgfile.h"
#include "binlog.h"
#include "nagios/nagios.h"
#include "nagios/nebstructs.h"
#include "nagios/nebcallbacks.h"
#include "nagios/broker.h"

/*
 * debug macros. All of them (including assert), goes away when NDEBUG
 * is specified. None of these may have side-effects (Heisenbugs)
 */
#ifndef NDEBUG
# include <assert.h>
# define dbug(s) fprintf(stderr, s " @ %s->%s:%d", __FILE__, __func__, __LINE__)
#else
# define dbug(s)
#endif

#ifndef offsetof
# define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif
#ifndef ARRAY_SIZE
# define ARRAY_SIZE(ary) (unsigned int)(sizeof(ary)/sizeof(ary[0]))
#endif

#define sizeof(x) (uint)sizeof(x)

#define is_flag_set(bfield, flag) (!!((bfield) & (flag)))
#define safe_str(str) (str == NULL ? "NULL" : str)
static inline void *safe_free(void *ptr)
{
	if (ptr)
		free(ptr);

	return NULL;
}

static inline int max(int a, int b)
{
	return a > b ? a : b;
}

static inline int min(int a, int b)
{
	return a < b ? a : b;
}

#define safe_strdup(str) str ? strdup(str) : NULL
#define prefixcmp(a, b) strncmp(a, b, strlen(b))

/** global variables present in both module and daemon **/
extern const char *merlin_version;
extern int is_module;
extern int pulse_interval;
extern int debug;

#define num_masters self.configured_masters
#define num_peers self.configured_peers
#define num_pollers self.configured_pollers
#define num_nodes (num_masters + num_pollers + num_peers)
extern int use_database;
extern merlin_nodeinfo self;

struct strvec {
	unsigned int entries;
	char **str;
};
typedef struct strvec strvec;

/** event structures where Nagios' doesn't provide good ones */
struct monitored_object_state {
	int initial_state;
	int flap_detection_enabled;
	double low_flap_threshold;
	double high_flap_threshold;
	int check_freshness;
	int freshness_threshold;
	int process_performance_data;
	int checks_enabled;
	int accept_passive_checks;
	int event_handler_enabled;
	int obsess;
	int problem_has_been_acknowledged;
	int acknowledgement_type;
	int check_type;
	int current_state;
	int last_state;
	int last_hard_state;
	int state_type;
	int current_attempt;
	unsigned long current_event_id;
	unsigned long last_event_id;
	unsigned long current_problem_id;
	unsigned long last_problem_id;
	double latency;
	double execution_time;
	int notifications_enabled;
	time_t last_notification;
	time_t next_notification;
	time_t next_check;
	int should_be_scheduled;
	time_t last_check;
	time_t last_state_change;
	time_t last_hard_state_change;
	time_t last_time_up;
	time_t last_time_down;
	time_t last_time_unreachable;
	int has_been_checked;
	int current_notification_number;
	unsigned long current_notification_id;
	int check_flapping_recovery_notification;
	int scheduled_downtime_depth;
	int pending_flex_downtime;
	int state_history[MAX_STATE_HISTORY_ENTRIES]; /* flap detection */
	int state_history_index;
	int is_flapping;
	unsigned long flapping_comment_id;
	double percent_state_change;
	int notified_on;
	char *plugin_output;
	char *long_plugin_output;
	char *perf_data;
};
typedef struct monitored_object_state monitored_object_state;

struct merlin_host_status {
	monitored_object_state state;
	char *name;
};
typedef struct merlin_host_status merlin_host_status;

struct merlin_service_status {
	monitored_object_state state;
	char *host_name;
	char *service_description;
};
typedef struct merlin_service_status merlin_service_status;


#define ISOTIME_PREC_YEAR    0
#define ISOTIME_PREC_MONTH   1
#define ISOTIME_PREC_DAY     2
#define ISOTIME_PREC_HOUR    3
#define ISOTIME_PREC_MINUTE  4
#define ISOTIME_PREC_SECOND  5
#define ISOTIME_PREC_USECOND 6
#define ISOTIME_PREC_MAX     ISOTIME_PREC_USECOND

/** prototypes **/
extern strvec *str_explode(char *str, int delim);
extern int strtobool(const char *str);
extern int grok_seconds(const char *p, long *result);
extern const char *isotime(struct timeval *tv, int precision);
extern char *tohex(const unsigned char *data, int len);
extern void bt_scan(const char *mark, int count);
extern const char *human_bytes(unsigned long long n);
extern linked_item *add_linked_item(linked_item *list, void *item);
extern int set_socket_options(int sd, int beefup_buffers);
extern char *next_word(char *str);
extern int grok_confsync_compound(struct cfg_comp *comp, merlin_confsync *csync);
extern int grok_common_var(struct cfg_comp *config, struct cfg_var *v);
extern const char *callback_name(int id);
extern int callback_id(const char *orig_name);
extern const char *ctrl_name(uint code);
extern const char *node_state_name(int state);
extern const char *tv_delta(const struct timeval *start, const struct timeval *stop);
extern int handle_ctrl_active(merlin_node *node, merlin_event *pkt);

/* data encoding/decoding routines */
extern int merlin_encode(void *data, int cb_type, char *buf, int buflen);
extern int merlin_decode(void *ds, off_t len, int cb_type);
static inline int merlin_encode_event(merlin_event *pkt, void *data)
{
	return merlin_encode(data, pkt->hdr.type, pkt->body, sizeof(pkt->body));
}
static inline int merlin_decode_event(merlin_event *pkt)
{
	int ret = merlin_decode(pkt->body, pkt->hdr.len, pkt->hdr.type);

	if (ret) {
		lerr("CODEC: Failed to decode packet. type: %u; code: %u; len: %u",
			 pkt->hdr.type, pkt->hdr.code, pkt->hdr.len);
	}
	return ret;
}
#endif
