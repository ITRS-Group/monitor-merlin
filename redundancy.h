/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright (C) 2004 OP5 AB
 * All rights reserved.
 *
 */

#ifndef REDUNDANCY_H_
#define REDUNDANCY_H_

/** macros **/

/* debug macros. All of them (including assert), goes away when NDEBUG
 * is specified. None of these may have side-effects (Heisenbugs) */
#ifndef NDEBUG
# include <assert.h>
# define dbug(s) fprintf(stderr, s " @ %s->%s:%d", __FILE__, __func__, __LINE__)
#else
# define dbug(s)
#endif

/* network communication types */
#define TYPE_PULSE 0
#define TYPE_RESULT 1
#define TYPE_COMMAND 2

/** global and external variables... */
#if !defined(DEBUG) || !defined(C)
/* exported by nagios */
extern char *config_file;
extern int event_broker_options;
#endif

/*
 * prototypes
 */

/* callback hooks */
int hook_service_result(int cb, void *data);
int hook_host_result(int cb, void *data);
int hook_command(int cb, void *data);
int hook_comment(int cb, void *data);
int hook_downtime(int cb, void *data);

/** misc functions **/
time_t get_last_cfg_change(void);

#endif /* REDUNDANCY_H */
