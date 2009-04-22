#include "nagios/nebstructs.h"
#include "nagios/nebcallbacks.h"
#include "nagios/broker.h"
#include "sql.h"
#include "data.h"
#include "protocol.h"
#include "logging.h"

#define safe_str(str) (str == NULL ? "''" : str)
#define safe_free(str) do { if (str) free(str); } while (0)
static int mdb_update_host_status(const nebstruct_host_check_data *p)
{
	char *host_name, *output, *perf_data = NULL;
	int result;

	if (p->type != NEBTYPE_HOSTCHECK_PROCESSED)
		return 0;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->output, &output);
	sql_quote(p->perf_data, &perf_data);

	result = sql_query
		("UPDATE %s.host SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = %s, perf_data = %s "
		 "WHERE host_name = '%s'",
		 sql_db_name(),
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency, p->end_time.tv_sec,
		 p->return_code, output, safe_str(perf_data),
		 p->host_name);

	free(host_name);
	free(output);
	safe_free(perf_data);

	return result;
}

static int mdb_update_service_status(const nebstruct_service_check_data *p)
{
	char *host_name, *output, *perf_data, *service_description;
	int result;

	if (p->type != NEBTYPE_SERVICECHECK_PROCESSED)
		return 0;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->output, &output);
	sql_quote(p->perf_data, &perf_data);
	sql_quote(p->service_description, &service_description);

	result = sql_query
		("UPDATE %s.service SET current_attempt = %d, check_type = %d, "
		 "state_type = %d, current_state = %d, timeout = %d, "
		 "start_time = %lu, end_time = %lu, early_timeout = %d, "
		 "execution_time = %f, latency = '%.3f', last_check = %lu, "
		 "return_code = %d, plugin_output = %s, perf_data = %s "
		 " WHERE host_name = (SELECT id FROM %s.host WHERE host_name = %s) AND service_description = %s",
		 sql_db_name(),
		 p->current_attempt, p->check_type,
		 p->state_type, p->state, p->timeout,
		 p->start_time.tv_sec, p->end_time.tv_sec, p->early_timeout,
		 p->execution_time, p->latency, p->end_time.tv_sec,
		 p->return_code, output, safe_str(perf_data),
		 sql_db_name(), host_name, service_description);

	free(host_name);
	free(output);
	safe_free(perf_data);
	free(service_description);

	return result;
}

static int mdb_update_program_status(const nebstruct_program_status_data *p)
{
	char *global_host_event_handler;
	char *global_service_event_handler;
	int result;

	sql_quote(p->global_host_event_handler, &global_host_event_handler);
	sql_quote(p->global_service_event_handler, &global_service_event_handler);

	result = sql_query
		("UPDATE %s.program_status SET is_running = 1, "
		 "last_alive = %lu, program_start = %lu, pid = %d, daemon_mode = %d, "
		 "last_command_check = %lu, last_log_rotation = %lu, "
		 "notifications_enabled = %d, "
		 "active_service_checks_enabled = %d, passive_service_checks_enabled = %d, "
		 "active_host_checks_enabled = %d, passive_host_checks_enabled = %d, "
		 "event_handlers_enabled = %d, flap_detection_enabled = %d, "
		 "failure_prediction_enabled = %d, process_performance_data = %d, "
		 "obsess_over_hosts = %d, obsess_over_services = %d, "
		 "modified_host_attributes = %lu, modified_service_attributes = %lu, "
		 "global_host_event_handler = %s, global_service_event_handler = %s"
		 "WHERE instance_id = 0",
		 sql_db_name(),
		 time(NULL), p->program_start, p->pid, p->daemon_mode,
		 p->last_command_check, p->last_log_rotation,
		 p->notifications_enabled,
		 p->active_service_checks_enabled, p->passive_service_checks_enabled,
		 p->active_host_checks_enabled, p->passive_host_checks_enabled,
		 p->event_handlers_enabled, p->flap_detection_enabled,
		 p->failure_prediction_enabled, p->process_performance_data,
		 p->obsess_over_hosts, p->obsess_over_services,
		 p->modified_host_attributes, p->modified_service_attributes,
		 safe_str(global_host_event_handler), safe_str(global_service_event_handler));

	free(global_host_event_handler);
	free(global_service_event_handler);
	return result;
}

static int mdb_handle_downtime(const nebstruct_downtime_data *p)
{
	int result;
	char *host_name, *service_description, *comment_data, *author_name;

	if (p->type == NEBTYPE_DOWNTIME_DELETE) {
		result = sql_query("DELETE FROM %s.scheduled_downtime "
						   "WHERE downtime_id = %lu",
						   sql_db_name(), p->downtime_id);
		if (p->start_time > time(NULL))
			return result;
	}

	if (p->type != NEBTYPE_DOWNTIME_DELETE) {
		sql_quote(p->host_name, &host_name);
		sql_quote(p->service_description, &service_description);
	}

	switch (p->type) {
	case NEBTYPE_DOWNTIME_START:
	case NEBTYPE_DOWNTIME_STOP:
		result = sql_query
			("UPDATE %s.%s "
			 "SET scheduled_downtime_depth = scheduled_downtime_depth %c 1 "
			 "WHERE host_name = %s AND service_description = %s",
			 sql_db_name(),
			 p->service_description ? "service" : "host",
			 p->type == NEBTYPE_DOWNTIME_START ? '+' : '-',
			 host_name, safe_str(service_description));
		break;
	case NEBTYPE_DOWNTIME_LOAD:
		result = sql_query
			("DELETE FROM %s.scheduled_downtime WHERE downtime_id = %lu",
			 sql_db_name(), p->downtime_id);
		/* fallthrough */
	case NEBTYPE_DOWNTIME_ADD:
		sql_quote(p->author_name, &author_name);
		sql_quote(p->comment_data, &comment_data);
		result = sql_query
			("INSERT INTO %s.scheduled_downtime "
			 "(downtime_type, host_name, service_description, entry_time, "
			 "author_name, comment_data, start_time, end_time, fixed, "
			 "duration, triggered_by, downtime_id) "
			 "VALUES(%d, %s, %s, %lu, "
			 "       %s, %s, %lu, %lu, %d, "
			 "       %lu, %lu, %lu)",
			 sql_db_name(),
			 p->downtime_type, host_name, safe_str(service_description),
			 p->entry_time, author_name, comment_data, p->start_time,
			 p->end_time, p->fixed, p->duration, p->triggered_by,
			 p->downtime_id);
		free(author_name);
		free(comment_data);
		break;
	case NEBTYPE_DOWNTIME_DELETE:
		result = sql_query
			("DELETE FROM %s.scheduled_downtime WHERE downtime_id = %lu",
			 sql_db_name(), p->downtime_id);
		break;
	default:
		linfo("Unknown downtime type %d", p->type);
		break;
	}

	safe_free(host_name);
	safe_free(service_description);

	return result;
}

static int mdb_handle_flapping(const nebstruct_flapping_data *p)
{
	int result;
	char *host_name, *service_description = NULL;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->service_description, &service_description);

	result = sql_query
		("UPDATE %s.%s SET is_flapping = %d, "
		 "flapping_comment_id = %lu, percent_state_change = %f"
		 "WHERE host_name = %s AND service_description = %s",
		 sql_db_name(),
		 service_description ? "service" : "host",
		 p->type == NEBTYPE_FLAPPING_START,
		 p->comment_id, p->percent_change,
		 host_name, safe_str(service_description));

	safe_free(service_description);
	free(host_name);

	return result;
}

static int mdb_handle_comment(const nebstruct_comment_data *p)
{
	int result;
	char *host_name, *author_name, *comment_data, *service_description;

	if (p->type == NEBTYPE_COMMENT_DELETE) {
		result = sql_query
			("DELETE FROM %s.comment WHERE comment_id = %lu",
			 sql_db_name(), p->comment_id);
		return result;
	}

	sql_quote(p->host_name, &host_name);
	sql_quote(p->author_name, &author_name);
	sql_quote(p->comment_data, &comment_data);
	sql_quote(p->service_description, &service_description);

	result = sql_query
		("INSERT INTO %s.comment(comment_type, host_name, "
		 "service_description, entry_time, author_name, comment_data, "
		 "persistent, source, entry_type, expires, expire_time, "
		 "comment_id) "
		 "VALUES(%d, %s, %s, %lu, %s, %s, %d, %d, %d, %d, %lu, %lu)",
		 sql_db_name(), p->comment_type, host_name,
		 safe_str(service_description), p->entry_time,
		 author_name, comment_data, p->persistent, p->source,
		 p->entry_type, p->expires, p->expire_time, p->comment_id);

	free(host_name);
	free(author_name);
	free(comment_data);
	safe_free(service_description);

	return result;
}

static int mdb_handle_notification(const nebstruct_notification_data *p)
{
	char *host_name, *service_description;
	char *output, *ack_author, *ack_data;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->service_description, &service_description);
	sql_quote(p->output, &output);
	sql_quote(p->ack_author, &ack_author);
	sql_quote(p->ack_data, &ack_data);

	return sql_query
		("INSERT INTO %s.notification "
		 "(notification_type, start_time, end_time, host_name,"
		 "service_description, reason_type, state, output,"
		 "ack_author, ack_data, escalated, contacts_notified) "
		 "VALUES(%d, %lu, %lu, %s,"
		 "%s, %d, %d, %s, %s, %s, %d, %d)",
		 sql_db_name(),
		 p->notification_type, p->start_time.tv_sec, p->end_time.tv_sec,
		 host_name,  safe_str(service_description), p->reason_type, p->state,
		 safe_str(output), safe_str(ack_author), safe_str(ack_data),
		 p->escalated, p->contacts_notified);
}

int mrm_db_update(struct merlin_event *pkt)
{
	int errors = 0;

	if (!pkt) {
		lerr("pkt is NULL in mrm_db_update");
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
	case NEBCALLBACK_COMMENT_DATA:
		errors = mdb_handle_comment((void *)pkt->body);
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		errors = mdb_handle_downtime((void *)pkt->body);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		errors = mdb_handle_flapping((void *)pkt->body);
		break;
	case NEBCALLBACK_NOTIFICATION_DATA:
		errors = mdb_handle_notification((void *)pkt->body);
		break;
	default:
		ldebug("Unknown callback type. Weird, to say the least...");
		return -1;
		break;
	}
	sql_free_result();

	return errors;
}
