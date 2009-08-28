/*
 * shared.h: Header file for code shared between module and daemon
 */

#ifndef INCLUDE_shared_h__
#define INCLUDE_shared_h__

#define NSCORE

#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
#ifndef __USE_FILE_OFFSET64
# define __USE_FILE_OFFSET64
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

#include "types.h"
#include "io.h"
#include "ipc.h"
#include "logging.h"
#include "cfgfile.h"
#include "protocol.h"
#include "binlog.h"
#include "nagios/nebstructs.h"
#include "nagios/nebcallbacks.h"

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
# define ARRAY_SIZE(ary) (sizeof(ary)/sizeof(ary[0]))
#endif

#define MODE_NOC       1
#define MODE_PEER      (1 << 1)
#define MODE_POLLER    (1 << 2)

static inline void *xfree(void *ptr)
{
	if(ptr)
		free(ptr);

	return NULL;
}

static inline float tv_delta(struct timeval *start, struct timeval *stop)
{
	float result = 0;

	result = ((stop->tv_sec - start->tv_sec) * 1000000);
	result += stop->tv_usec - start->tv_usec;
	result /= 1000000;

	return result;
}

#define xstrdup(str) str ? strdup(str) : NULL
#define prefixcmp(a, b) strncmp(a, b, strlen(b))

/** global variables present in both module and daemon **/
extern const char *merlin_version;
extern int is_module;
extern int pulse_interval;
extern int debug;

extern const char *merlin_version;

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
	int is_flapping;
	unsigned long flapping_comment_id;
	double percent_state_change;
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

/** prototypes **/
extern int set_socket_buffers(int sd);
extern char *next_word(char *str);
extern int grok_common_var(struct cfg_comp *config, struct cfg_var *v);
extern void index_selections(void);
extern int add_selection(char *name);
extern char *get_sel_name(int index);
extern int get_sel_id(const char *name);
extern int get_num_selections(void);
extern const char *callback_name(int id);

/* data blockification routines */
extern int blockify(void *data, int cb_type, char *buf, int buflen);
extern int deblockify(void *ds, off_t len, int cb_type);
static inline int blockify_event(merlin_event *pkt, void *data)
{
	return blockify(data, pkt->hdr.type, pkt->body, sizeof(pkt->body));
}
#endif
