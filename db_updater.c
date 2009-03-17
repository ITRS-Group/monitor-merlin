#include <nagios/nebstructs.h>
#include <nagios/nebcallbacks.h>
#include "sql.h"
#include "data.h"
#include "protocol.h"
#include "logging.h"

static int mdb_update_host_status(const nebstruct_host_check_data *p)
{
	char *output, *perf_data;
	int result;

	sql_escape(p->output, &output);
	sql_escape(p->perf_data, &perf_data);

	ldebug("Updating db for host '%s'\n", p->host_name);
	result = sql_query
		("UPDATE monitor_gui.host SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = '%s', perf_data = '%s' "
		 "WHERE host_name = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency, p->end_time.tv_sec,
		 p->return_code, output, perf_data,
		 p->host_name);

	free(output);
	free(perf_data);
	return result;
}

static int mdb_update_service_status(const nebstruct_service_check_data *p)
{
	char *output, *perf_data, *service_description;
	int result;

	sql_escape(p->output, &output);
	sql_escape(p->perf_data, &perf_data);
	sql_escape(p->service_description, &service_description);

	ldebug("Updating db for service '%s' on host '%s'\n",
		   p->service_description, p->host_name);
	result = sql_query
		("UPDATE monitor_gui.service SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = '%s', perf_data = '%s' "
		 " WHERE host_name = (SELECT id FROM monitor_gui.host WHERE host_name = '%s') AND service_description = '%s'",
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency, p->end_time.tv_sec,
		 p->return_code, output, perf_data,
		 p->host_name, service_description);

	free(output);
	free(perf_data);
	free(service_description);
	return result;
}

int mrm_db_update(struct proto_pkt *pkt)
{
	int errors = 0;

	if (!pkt) {
		ldebug("pkt is NULL in mrm_db_update");
		return 0;
	}
	deblockify(pkt->body, pkt->hdr.len, pkt->hdr.type);
	switch (pkt->hdr.type) {
	case NEBCALLBACK_HOST_CHECK_DATA:
		errors = mdb_update_host_status((void *)pkt->body);
		break;

	case NEBCALLBACK_SERVICE_CHECK_DATA:
		errors = mdb_update_service_status((void *)pkt->body);
		break;
	default:
		ldebug("Unknown callback type. Weird, to say the least...");
		break;
	}
	sql_free_result();

	return errors;
}
