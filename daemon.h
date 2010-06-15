/*
 * This file is included by all C source files used exclusively by
 * the merlin daemon
 */
#ifndef INCLUDE_daemon_h__
#define INCLUDE_daemon_h__

#include "shared.h"
#include "net.h"
#include "status.h"
#include "sql.h"

extern int use_database;
extern int mrm_db_update(merlin_node *node, merlin_event *pkt);
extern void db_mark_node_inactive(merlin_node *node);

#endif
