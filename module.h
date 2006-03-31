/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright (C) 2004,2006 OP5 AB
 * All rights reserved.
 *
 */

#ifndef MRM_MOD_H
#define MRM_MOD_H

#ifndef USE_EVENT_BROKER
# define USE_EVENT_BROKER 1
#endif

/* common include files required for types in this file */
#include <nagios/nebmods.h>
#include <nagios/nebmodules.h>
#include <nagios/nebcallbacks.h>
#include <nagios/nebstructs.h>
#include <nagios/broker.h>

#include "shared.h"
#include "logging.h"

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


/** global variables exported by Nagios **/
extern char *config_file;
extern int event_broker_options;

/** prototypes **/
extern int blockify(void *data, int cb_type, char *buf, int buflen);
extern int deblockify(void *ds, off_t len, int cb_type);
extern int cb_handler(int cmd, void *data); /* the callback handler */
extern void file_list_free(struct file_list *list);
extern int mrm_ipc_write(const char *key, const void *buf, int len, int type);
time_t get_last_cfg_change(void);

int print_service_check_data(nebstruct_service_check_data *ds);
int print_blockified_service_check_data(nebstruct_service_check_data *ds, off_t len);

#define disable_checks(sel) enable_disable_checks(sel, 0)
#define enable_checks(sel) enable_disable_checks(sel, 1)
void enable_disable_checks(int selection, int status);
void control_all_distributed_checks(void);
void create_object_lists(void);
void handle_control(int code, int selection);
#endif /* MRM_MOD_H */
