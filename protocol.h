#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>

#define MERLIN_PROTOCOL_VERSION 0

/* If "type" is CTRL_PACKET, then "len" is one of the following.
 * control packets never have a body */
#define CTRL_PACKET   0xffff
#define CTRL_PULSE    1
#define CTRL_INACTIVE 2
#define CTRL_ACTIVE   3

#define HDR_SIZE (sizeof(struct merlin_header))
#define PKT_SIZE (sizeof(struct merlin_event))
#define BODY_SIZE (TOTAL_PKT_SIZE - sizeof(struct merlin_header))
#define TOTAL_PKT_SIZE 32768
#define MAX_PKT_SIZE TOTAL_PKT_SIZE /* for now. remove this macro later */

#define packet_size(pkt) \
	(pkt->hdr.type == CTRL_PACKET ? HDR_SIZE : (pkt->hdr.len + HDR_SIZE))

struct merlin_header {
	u_int16_t protocol;   /* always 0 for now */
	u_int16_t type;       /* event type */
	u_int16_t selection;  /* used when noc Nagios communicates with mrd */
	u_int32_t len;        /* size of body */
	struct timeval sent;  /* when this message was sent */

	/* pad to 64 bytes for future extensions */
	char padding[64 - sizeof(struct timeval) - (2 * 5)];
} __attribute__((packed));

struct merlin_event {
	struct merlin_header hdr;
	char body[BODY_SIZE];
} __attribute__((packed));

typedef struct merlin_header merlin_header;
typedef struct merlin_event merlin_event;

extern int proto_send_event(int sock, struct merlin_event *pkt);
extern int proto_read_event(int sock, struct merlin_event *pkt);
extern int proto_ctrl(int sock, int control_type, int selection);

#endif
