#ifndef INCLUDE_runcmd_h__
#define INCLUDE_runcmd_h__

#include "node.h"

struct runcmd_ctx {
  struct merlin_runcmd * runcmd;
  struct merlin_node * node;
  uint16_t type;
};
typedef struct runcmd_ctx runcmd_ctx;

/* Sends a RUNCMD_PACKET */
void send_runcmd_cmd(struct nm_event_execution_properties *evprop);
/* Handles incoming RUNCMD_PACKETs */
int handle_runcmd_event(merlin_node *node, merlin_event *pkt);

#endif
