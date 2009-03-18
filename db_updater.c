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

	sql_quote(p->output, &output);
	sql_quote(p->perf_data, &perf_data);

	ldebug("Updating db for host '%s'\n", p->host_name);
	result = sql_query
		("UPDATE monitor_gui.host SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = %s, perf_data = %s "
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

	sql_quote(p->output, &output);
	sql_quote(p->perf_data, &perf_data);
	sql_quote(p->service_description, &service_description);

	ldebug("Updating db for service '%s' on host '%s'\n",
		   p->service_description, p->host_name);
	result = sql_query
		("UPDATE monitor_gui.service SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = %s, perf_data = %s "
		 " WHERE host_name = (SELECT id FROM monitor_gui.host WHERE host_name = '%s') AND service_description = %s",
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

static int mdb_update_program_status(const nebstruct_program_status_data *p)
{
	char *global_host_event_handler;
	char *global_service_event_handler;
	int result;

	ldebug("Updating program status data");
	sql_quote(p->global_host_event_handler, &global_host_event_handler);
	sql_quote(p->global_service_event_handler, &global_service_event_handler);

	result = sql_query
		("UPDATE monitor_gui.program_status SET is_running = 1, "
		 "last_alive = %lu, program_start = %lu, pid = %d, daemon_mode = %d, "
		 "last_command_check = %lu, last_log_rotation = %lu, "
		 "notifications_enabled = %d, "
		 "active_service_checks_enabled = %d, passive_service_checks_enabled = %d, "
		 "active_host_checks_enabled = %d, passive_host_checks_enabled = %d, "
		 "event_handlers_enabled = %d, flap_detection_enabled = %d, "
		 "failure_prediction_enabled = %d, process_performance_data = %d, "
		 "obsess_over_hosts = %d, obsess_over_services = %d, "
		 "modified_host_attributes = %lu, modified_service_attributes = %lu, "
		 "global_host_event_handler = %s, global_service_event_handler = %s",
		 time(NULL), p->program_start, p->pid, p->daemon_mode,
		 p->last_command_check, p->last_log_rotation,
		 p->notifications_enabled,
		 p->active_service_checks_enabled, p->passive_service_checks_enabled,
		 p->active_host_checks_enabled, p->passive_host_checks_enabled,
		 p->event_handlers_enabled, p->flap_detection_enabled,
		 p->failure_prediction_enabled, p->process_performance_data,
		 p->obsess_over_hosts, p->obsess_over_services,
		 p->modified_host_attributes, p->modified_service_attributes,
		 global_host_event_handler, global_service_event_handler);

	free(global_host_event_handler);
	free(global_service_event_handler);
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
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		errors = mdb_update_program_status((void *)pkt->body);
		break;
	default:
		ldebug("Unknown callback type. Weird, to say the least...");
		break;
	}
	sql_free_result();

	return errors;
}
