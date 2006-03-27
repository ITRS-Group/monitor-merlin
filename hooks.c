/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright(C) 2005 OP5 AB
 * All rights reserved.
 *
 */

/*
 * Process In/Out
 *
 * This file contains functions that shuffle data from the module part of
 * the module (the timed and triggered events) to the thread part of the
 * module (the multiplexing networker), as well as functions that re-insert
 * the data from the network to the running Nagios process.
 * In short, these functions are only called from the triggered event
 * thingie.
 */

#include "module.h"
#include "protocol.h"
#include "ipc.h"


/*
 * The hooks are called from broker.c in Nagios.
 */
int hook_service_result(int cb, void *data)
{
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;
	void *buf;
	int len, result;

	if (ds->type != NEBTYPE_SERVICECHECK_PROCESSED
		|| ds->check_type != SERVICE_CHECK_ACTIVE
		|| cb != NEBCALLBACK_SERVICE_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	buf = blockify(ds, &len, cb);

	if (!buf)
		return -1;

	result = mrm_ipc_write(ds->host_name, buf, len, cb);
	free(buf);

	return result;
}

int hook_host_result(int cb, void *data)
{
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;
	void *buf = NULL;
	int len, result;

	/* ignore un-processed and passive checks */
	if (ds->type != NEBTYPE_HOSTCHECK_PROCESSED ||
		ds->check_type != HOST_CHECK_ACTIVE ||
		cb != NEBCALLBACK_HOST_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for host '%s'", ds->host_name);
	buf = blockify(ds, &len, cb);
	result = mrm_ipc_write(ds->host_name, buf, len, cb);

	free(buf);

	return result;
}
