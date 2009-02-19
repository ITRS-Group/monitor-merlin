#include <nagios/nebstructs.h>
#include <nagios/nebcallbacks.h>
#include "sql.h"
#include "data.h"
#include "protocol.h"

static int mdb_update_host_status(const nebstruct_host_check_data *p)
{
	return sql_query
		("UPDATE host SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, state = %d, timeout = %d, start_time = %lu, "
		 "end_time = %lu, early_timeout = %d, execution_time = %f, "
		 "latency = '%.3f', return_code = %d, output = '%s', perf_data = '%s' "
		 " WHERE host_name = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout, p->start_time.tv_sec,
		 p->end_time.tv_sec, p->early_timeout, p->execution_time,
		 p->latency, p->return_code, sql_escape(p->output), sql_escape(p->perf_data),
		 sql_escape(p->host_name));
}

static int mdb_update_service_status(const nebstruct_service_check_data *p)
{
	return sql_query
		("UPDATE service SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, state = %d, timeout = %d, start_time = %lu, "
		 "end_time = %lu, early_timeout = %d, execution_time = %f, "
		 "latency = '%.3f', return_code = %d, output = '%s', perf_data = '%s' "
		 " WHERE host_name = '%s' AND service_description = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout, p->start_time.tv_sec,
		 p->end_time.tv_sec, p->early_timeout, p->execution_time,
		 p->latency, p->return_code, sql_escape(p->output), sql_escape(p->perf_data),
		 sql_escape(p->host_name), sql_escape(p->service_description));
}

int mrm_db_update(void *buf)
{
	struct proto_hdr *hdr = (struct proto_hdr *)buf;
	int result;

	switch (hdr->type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
		result = deblockify(buf, hdr->len, hdr->type);
		result |= mdb_update_host_status(buf);
		break;

	case NEBCALLBACK_SERVICE_CHECK_DATA:
		result = deblockify(buf, hdr->len, hdr->type);
		result |= mdb_update_service_status(buf);
		break;
	default:
		break;
	}

	return 0;
}
