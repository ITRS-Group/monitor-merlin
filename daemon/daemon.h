/*
 * This file is included by all C source files used exclusively by
 * the merlin daemon
 */
#ifndef INCLUDE_daemon_h__
#define INCLUDE_daemon_h__

#include "node.h"

extern void db_mark_node_inactive(merlin_node *node);
extern void csync_node_active(merlin_node *node);

#endif
