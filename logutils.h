#ifndef logutils_h__
#define logutils_h__

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <broker.h>
#include <nebcallbacks.h>
#include <lib/dkhash.h>
#include "shared.h"

#define CLR_RESET          "\033[m"
#define CLR_BOLD           "\033[1m"
#define CLR_RED            "\033[31m"
#define CLR_GREEN          "\033[32m"
#define CLR_BROWN          "\033[33m"
#define CLR_YELLOW         "\033[33m\033[1m"
#define CLR_BLUE           "\033[34m"
#define CLR_MAGENTA        "\033[35m"
#define CLR_CYAN           "\033[36m"
#define CLR_BG_RED         "\033[41m"
#define CLR_BRIGHT_RED     "\033[31m\033[1m"
#define CLR_BRIGHT_GREEN   "\033[32m\033[1m"
#define CLR_BRIGHT_BLUE    "\033[34m\033[1m"
#define CLR_BRIGHT_MAGENTA "\033[35m\033[1m"
#define CLR_BRIGHT_CYAN    "\033[36m\033[1m"

/* for the string code structs */
#define add_code(n, s, c) { n, s, sizeof(s) - 1, c, }
#define add_ignored(s) add_code(0, s, IGNORE_LINE)
#define add_cdef(__nvecs, __define) add_code(__nvecs, #__define, __define)
#define get_event_type(str, len) get_string_code(event_codes, str, len)
#define get_command_type(str, len) get_string_code(command_codes, str, len)


#define IGNORE_LINE 0
#define CONCERNS_HOST 50
#define CONCERNS_SERVICE 60

#define DEL_HOST_DOWNTIME                               1
#define DEL_SVC_DOWNTIME                                2
#define SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME            3
#define SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME  4
#define SCHEDULE_HOSTGROUP_HOST_DOWNTIME                5
#define SCHEDULE_HOSTGROUP_SVC_DOWNTIME                 6
#define SCHEDULE_HOST_DOWNTIME                          7
#define SCHEDULE_HOST_SVC_DOWNTIME                      8
#define SCHEDULE_SERVICEGROUP_HOST_DOWNTIME             9
#define SCHEDULE_SERVICEGROUP_SVC_DOWNTIME             10
#define SCHEDULE_SVC_DOWNTIME                          11
#define ACKNOWLEDGE_HOST_PROBLEM                       12
#define ACKNOWLEDGE_SVC_PROBLEM                        13
#define RESTART_PROGRAM                                14

/* for some reason these aren't defined inside Nagios' headers */
#define SERVICE_OK 0
#define SERVICE_WARNING 1
#define SERVICE_CRITICAL 2
#define SERVICE_UNKNOWN 3

 /*duplicate typedef error on my box:
typedef unsigned int uint;
 */

struct naglog_file {
	time_t first;
	char *path;
	uint64_t size;
	uint cmp;
};
extern struct naglog_file *nfile;

struct string_code {
	int nvecs;
	const char *str;
	uint len;
	int code;
};

extern int debug_level;
extern int num_nfile;
extern struct naglog_file *cur_file; /* the file we're currently importing */
extern uint line_no;
extern uint num_unhandled;
extern char **strv;

void __attribute__((__noreturn__)) lp_crash(const char *fmt, ...);
extern int vectorize_string(char *str, int nvecs);
extern char *devectorize_string(char **str, int nvecs);
extern void handle_unknown_event(const char *line);
extern void print_unhandled_events(void);

extern int parse_notification_reason(const char *str);
extern int parse_service_state(const char *str);
extern int parse_host_state(const char *str);
extern int parse_service_state_gently(const char *str);
extern int parse_host_state_gently(const char *str);
extern int soft_hard(const char *str);

extern void print_interesting_objects(void);
extern int add_interesting_object(const char *orig_str);
extern int is_interesting_host(const char *host);
extern int is_interesting_service(const char *host, const char *service);
extern int is_interesting(const char *ptr);
extern int is_start_event(const char *ptr);
extern int is_stop_event(const char *ptr);
extern int add_naglog_path(char *path);


extern struct string_code *get_string_code(struct string_code *codes,
                                           const char *str, uint len);

extern uint warnings;
extern void crash(const char *fmt, ...)
	    __attribute__((__format__(__printf__, 1, 2), __noreturn__));
extern void pdebug(int lvl, const char *fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)));
#define debug(...) pdebug(1, __VA_ARGS__)
extern void warn(const char *fmt, ...);

int strtotimet(const char *str, time_t *val);


extern uint path_cmp_number(char *path);
extern void first_log_time(struct naglog_file *nf);
extern int nfile_cmp(const void *p1, const void *p2);
extern int nfile_rev_cmp(const void *p1, const void *p2);

#endif
