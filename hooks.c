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

int hook_generic(int cb, void *data)
{
	struct merlin_event pkt;

	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(data, cb, pkt.body, sizeof(pkt.body));
	if (!pkt.hdr.len)
		lerr("Header len is 0 for callback %d. Update offset in hookinfo.h", cb);
	pkt.hdr.selection = 0xffff;
	return ipc_send_event(&pkt);
}

/*
 * The hooks are called from broker.c in Nagios.
 */
int hook_service_result(int cb, void *data)
{
	nebstruct_service_check_data *ds = (nebstruct_service_check_data *)data;
	struct merlin_event pkt;
	int result;

	if (ds->type != NEBTYPE_SERVICECHECK_PROCESSED
		|| ds->check_type != SERVICE_CHECK_ACTIVE
		|| cb != NEBCALLBACK_SERVICE_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for service '%s' on host '%s'",
		  ds->service_description, ds->host_name);

	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(ds, cb, pkt.body, sizeof(pkt.body));
	result = mrm_ipc_write(ds->host_name, &pkt);

	return result;
}

int hook_host_result(int cb, void *data)
{
	nebstruct_host_check_data *ds = (nebstruct_host_check_data *)data;
	struct merlin_event pkt;
	int result;

	/* ignore un-processed and passive checks */
	if (ds->type != NEBTYPE_HOSTCHECK_PROCESSED ||
		ds->check_type != HOST_CHECK_ACTIVE ||
		cb != NEBCALLBACK_HOST_CHECK_DATA)
	{
		return 0;
	}

	linfo("Active check result processed for host '%s'", ds->host_name);
	pkt.hdr.type = cb;
	pkt.hdr.len = blockify(ds, cb, pkt.body, sizeof(pkt.body));
	result = mrm_ipc_write(ds->host_name, &pkt);

	return result;
}

int hook_notification(int cb, void *data)
{
	nebstruct_notification_data *ds = (nebstruct_notification_data *)data;

	if (ds->type != NEBTYPE_NOTIFICATION_END)
		return 0;

	return hook_generic(cb, data);
}
