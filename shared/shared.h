/*
 * shared.h: Header file for code shared between module and daemon
 */

#ifndef INCLUDE_shared_h__
#define INCLUDE_shared_h__

#include <time.h>
#include <stdlib.h>
#include <naemon/naemon.h>

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
#define safe_free(ptr) do { if (ptr) { free(ptr); ptr = NULL; } } while(0)

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
extern int node_activity_check_interval;
extern int node_auto_delete_check_interval;
extern int debug;
extern char *binlog_dir;
extern char *merlin_config_file;
extern unsigned long long int binlog_max_memory_size;
extern unsigned long long int binlog_max_file_size;

extern int use_database;

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
	unsigned long hourly_value;
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
	unsigned long modified_attributes;
	int notified_on;
	char *plugin_output;
	char *long_plugin_output;
	char *perf_data;
};
typedef struct monitored_object_state monitored_object_state;

struct merlin_host_status {
	int nebattr;
	monitored_object_state state;
	char *name;
};
typedef struct merlin_host_status merlin_host_status;

struct merlin_service_status {
	int nebattr;
	monitored_object_state state;
	char *host_name;
	char *service_description;
};
typedef struct merlin_service_status merlin_service_status;


/* Let's avoid having to track this in 3 different places */
static inline int daemon_wants(int type)
{
	switch (type) {
	case NEBCALLBACK_NOTIFICATION_DATA:
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
	case NEBCALLBACK_COMMENT_DATA:
		return 0;
	default:
		return 1;
	}
	return 0;
}

/** prototypes **/
extern strvec *str_explode(char *str, int delim);
extern int strtobool(const char *str);
extern int grok_seconds(const char *p, long *result);
extern char *tohex(const unsigned char *data, int len);
extern void bt_scan(const char *mark, int count);
extern const char *human_bytes(unsigned long long n);
extern int merlin_set_socket_options(int sd, int beefup_buffers);
extern char *next_word(char *str);
extern const char *callback_name(int id);
extern int callback_id(const char *orig_name);
extern const char *ctrl_name(uint code);
extern const char *node_state_name(int state);
extern const char *tv_delta(const struct timeval *start, const struct timeval *stop);
#endif
