#ifndef INCLUDE_protocol_h__
#define INCLUDE_protocol_h__

#include <sys/types.h>

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

struct merlin_event_counter {
	unsigned long long sent, read, logged, dropped;
	struct timeval start;
};
typedef struct merlin_event_counter merlin_event_counter;

extern int proto_send_event(int sock, merlin_event *pkt);
extern int proto_read_event(int sock, merlin_event *pkt);
extern int proto_ctrl(int sock, int control_type, int selection);

#endif
