#ifndef INCLUDE_queries_h__
#define INCLUDE_queries_h__

#include "node.h"

int merlin_qh(int sd, char *buf, unsigned int len);
int runcmd_callback(merlin_node *node, merlin_event *pkt);

#endif
