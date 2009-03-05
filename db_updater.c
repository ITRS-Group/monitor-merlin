#include <nagios/nebstructs.h>
#include <nagios/nebcallbacks.h>
#include "sql.h"
#include "data.h"
#include "protocol.h"
#include "logging.h"

static int mdb_update_host_status(const nebstruct_host_check_data *p)
{
	ldebug("Updating db for host '%s'\n", p->host_name);
	return sql_query
		("UPDATE host SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', "
		 "return_code = %d, output = '%s', perf_data = '%s' "
		 "WHERE host_name = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency,
		 p->return_code, sql_escape(p->output), sql_escape(p->perf_data),
		 sql_escape(p->host_name));
}

static int mdb_update_service_status(const nebstruct_service_check_data *p)
{
	ldebug("Updating db for service '%s' on host '%s'\n",
		   p->service_description, p->host_name);
	return sql_query
		("UPDATE service SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', "
		 "return_code = %d, output = '%s', perf_data = '%s' "
		 " WHERE host_name = '%s' AND service_description = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency,
		 p->return_code, sql_escape(p->output), sql_escape(p->perf_data),
		 sql_escape(p->host_name), sql_escape(p->service_description));
}

int mrm_db_update(void *buf)
{
	struct proto_hdr *hdr = (struct proto_hdr *)buf;
	int errors = 0;

	if (!buf) {
		ldebug("buf is NULL in mrm_db_update");
		return 0;
	}
	deblockify(buf, hdr->len, hdr->type);
	switch (hdr->type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
		errors = mdb_update_host_status(buf);
		break;

	case NEBCALLBACK_SERVICE_CHECK_DATA:
		errors = mdb_update_service_status(buf);
		break;
	default:
		break;
	}

	return errors;
}
