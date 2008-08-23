/*
 * shared.h: Header file for code shared between module and daemon
 */

#ifndef SHARED_H
#define SHARED_H

#define NSCORE

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

#include "logging.h"
#include "config.h"

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

#define xstrdup(str) str ? strdup(str) : NULL

/** global variables in both module and daemon **/
extern int is_noc, debug;
extern const int module;

/** prototypes **/
extern char *next_word(char *str);
extern int grok_common_var(struct compound *config, struct cfg_var *v);
extern void index_selections(void);
extern int add_selection(char *name);
extern char *get_sel_name(int index);
extern int get_sel_id(const char *name);
extern int get_num_selections(void);

#endif
