#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>

#define MERLIN_PROTOCOL_VERSION 0

/* some special cases for the 'type' field.
 * NET_* can be used over network
 * IPC_* is for ipc communication */
#define CTRL_PACKET   0xffff
#define CTRL_PULSE    1
#define CTRL_INACTIVE 2
#define CTRL_ACTIVE   3

#define HDR_SIZE (sizeof(struct proto_hdr))
#define MAX_PKT_SIZE 8192 /* packets larger than this triggers an error */

struct proto_hdr {
	u_int16_t protocol;   /* always 0 for now */
	u_int16_t type;       /* event type */
	u_int16_t selection;  /* used when noc Nagios communicates with mrd */
	u_int32_t len;        /* size of body */
	struct timeval sent;  /* when this message was sent */

	/* pad to 64 bytes for future extensions */
	char padding[64 - (
					   (sizeof(u_int16_t) * 3)
					   + (sizeof(u_int32_t) * 1)
					   + (sizeof(struct timeval))
					   )];
} __attribute__((packed));

extern int proto_ctrl(int sock, int control_type, int selection);

#endif
