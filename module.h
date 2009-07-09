#ifndef MRM_MOD_H
#define MRM_MOD_H

#ifndef USE_EVENT_BROKER
# define USE_EVENT_BROKER 1
#endif

/* common include files required for types in this file */
#include "nagios/nebmods.h"
#include "nagios/nebmodules.h"
#include "nagios/broker.h"

#include "shared.h"
#include "hash.h"

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

/** global variables exported by Nagios **/
extern char *config_file;
extern int event_broker_options;

/** prototypes **/
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
#endif /* MRM_MOD_H */
