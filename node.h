#ifndef INCLUDE_node_h__
#define INCLUDE_node_h__

#include <sys/types.h>
#include <sys/time.h>
#include "cfgfile.h"
#include "binlog.h"

#define MERLIN_PROTOCOL_VERSION 0

/* various magic options for the "type" field */
#define CTRL_PACKET   0xffff  /* control packet. "code" described below */
#define ACK_PACKET    0xfffe  /* ACK ("I understood") (not used) */
#define NAK_PACKET    0xfffd  /* NAK ("I don't understand") (not used) */

/* If "type" is CTRL_PACKET, then "code" is one of the following */
#define CTRL_PULSE    1 /* keep-alive signal */
#define CTRL_INACTIVE 2 /* signals that a slave went offline */
#define CTRL_ACTIVE   3 /* signals that a slave went online */
#define CTRL_PATHS    4 /* body contains paths to import */
#define CTRL_STALL    5 /* signal that we can't accept events for a while */
#define CTRL_RESUME   6 /* now we can accept events again */
#define CTRL_STOP     7 /* exit() immediately (only accepted via ipc) */
#define CTRL_GENERIC  0xffff  /* generic control packet */

#define HDR_SIZE (sizeof(merlin_header))
#define PKT_SIZE (sizeof(merlin_event))
#define BODY_SIZE (TOTAL_PKT_SIZE - sizeof(merlin_header))
#define TOTAL_PKT_SIZE 32768
#define MAX_PKT_SIZE TOTAL_PKT_SIZE /* for now. remove this macro later */

#define packet_size(pkt) ((pkt)->hdr.len + HDR_SIZE)

struct merlin_header {
	u_int16_t protocol;   /* always 0 for now */
	u_int16_t type;       /* event type */
	u_int16_t code;       /* event code (used for control packets) */
	u_int16_t selection;  /* used when noc Nagios communicates with mrd */
	u_int32_t len;        /* size of body */
	struct timeval sent;  /* when this message was sent */

	/* pad to 64 bytes for future extensions */
	char padding[64 - sizeof(struct timeval) - (2 * 6)];
} __attribute__((packed));
typedef struct merlin_header merlin_header;

struct merlin_event {
	merlin_header hdr;
	char body[BODY_SIZE];
} __attribute__((packed));
typedef struct merlin_event merlin_event;

struct statistics_vars {
	uint64_t sent, read, logged, dropped;
};
struct merlin_node_stats {
	struct statistics_vars events, bytes;
	time_t last_logged;     /* when we logged the event-count last */
};
typedef struct merlin_node_stats merlin_node_stats;

/* used for various objects which we build linked lists for */
typedef struct linked_item {
	void *item;
	struct linked_item *next_item;
} linked_item;

struct node_selection {
	int id;
	char *name;
	linked_item *nodes;
};
typedef struct node_selection node_selection;

/* for node->type */
#define MODE_LOCAL     0
#define MODE_NOC       1
#define MODE_PEER      (1 << 1)
#define MODE_POLLER    (1 << 2)

/* for node->state */
#define STATE_NONE 0
#define STATE_PENDING 1
#define STATE_NEGOTIATING 2
#define STATE_CONNECTED 3

struct merlin_node {
	char *name;             /* name of this node */
	uint id;                 /* internal index lookup number */
	int sock;               /* the socket */
	int type;               /* server type (master, slave, peer) */
	int state;              /* state of this node (down, pending, active) */
	unsigned selection;     /* numeric index for hostgroup */
	int peer_id;            /* peer id, used to distribute checks */
	char *hostgroup;        /* only set for pollers on the noc-side */
	struct sockaddr *sa;    /* should always point to sain */
	struct sockaddr_in sain;
	time_t last_recv;       /* last time node sent something to us */
	time_t last_sent;       /* when we sent something last */
	struct timeval start;   /* when this node's module started */
	int last_action;        /* LA_CONNECT | LA_DISCONNECT | LA_HANDLED */
	binlog *binlog;         /* binary backlog for this node */
	merlin_node_stats stats; /* event/data statistics */
	int (*action)(struct merlin_node *, int); /* (daemon) action handler */
};
typedef struct merlin_node merlin_node;

#define node_table noc_table
extern merlin_node **noc_table, **peer_table, **poller_table;

extern node_selection *node_selection_by_name(const char *name);
extern char *get_sel_name(int index);
extern int get_sel_id(const char *name);
extern int get_num_selections(void);
extern linked_item *nodes_by_sel_id(int sel);
extern linked_item *nodes_by_sel_name(const char *name);
extern void node_grok_config(struct cfg_comp *config);
extern void node_log_event_count(merlin_node *node, int force);
extern void node_disconnect(merlin_node *node);
extern int node_send_event(merlin_node *node, merlin_event *pkt, int msec);
extern int node_read_event(merlin_node *node, merlin_event *pkt, int msec);
extern const char *node_state(merlin_node *node);
extern const char *node_type(merlin_node *node);
extern void node_set_state(merlin_node *node, int state);
extern int node_ctrl(merlin_node *node, int code, uint selection, void *data, uint32_t len, int msec);

/*
 * we make these inlined rather than macros so the compiler
 * does type-checking in the arguments
 */
static inline int node_send_ctrl_inactive(merlin_node *node, uint id, int msec)
{
	return node_ctrl(node, CTRL_INACTIVE, id, NULL, 0, msec);
}

static inline int node_send_ctrl_active(merlin_node *node, uint id, struct timeval *tv, int msec)
{
	return node_ctrl(node, CTRL_ACTIVE, id, (void *)tv, sizeof(*tv), msec);
}
#endif
