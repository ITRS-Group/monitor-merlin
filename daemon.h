#ifndef INCLUDE_daemon_h__
#define INCLUDE_daemon_h__

#include "shared.h"
#include "net.h"
#include "status.h"
#include "sql.h"

extern int use_database;
extern int mrm_db_update(merlin_event *pkt);

#endif
