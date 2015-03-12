#include "codec.h"
#include "daemon.h"
#include "string_utils.h"
#include "ipc.h"
#include "sql.h"
#include <naemon/naemon.h>


static int handle_host_status(int cb, const merlin_host_status *p)
{
	char *host_name;
	char *output = NULL, *long_output = NULL, *sql_safe_unescaped_long_output = NULL, *perf_data = NULL;
	int result = 0, rpt_log = 0, perf_log = 0;

	if (cb == NEBCALLBACK_HOST_CHECK_DATA) {
		if (db_log_reports && (p->nebattr & (NEBATTR_CHECK_ALERT | NEBATTR_CHECK_FIRST)))
			rpt_log = 1;
		if (host_perf_table && p->state.perf_data && *p->state.perf_data) {
			perf_log = 1;
		}
	}

	if (!rpt_log && !perf_log)
		return 0;

	sql_quote(p->name, &host_name);
	if (rpt_log) {
		sql_quote(p->state.plugin_output, &output);
		if (rpt_log && p->state.long_plugin_output) {
			char *unescaped_long_output = NULL;
			size_t long_len = strlen(p->state.long_plugin_output) + 1;
			if ((unescaped_long_output = malloc(long_len)) == NULL) {
				lerr("failed to allocate memory for unescaped long output");
				return 1;
			}
			unescape_newlines(unescaped_long_output, p->state.long_plugin_output, long_len);
			sql_quote(unescaped_long_output, &sql_safe_unescaped_long_output);
			free(unescaped_long_output);
			unescaped_long_output = NULL;
		}
		sql_quote(p->state.long_plugin_output, &long_output);
	}
	if (perf_log)
		sql_quote(p->state.perf_data, &perf_data);

	if (rpt_log) {
		result = sql_query
			("INSERT INTO %s(timestamp, event_type, host_name, state, "
				"hard, retry, output, long_output, downtime_depth) "
				"VALUES(%lu, %d, %s, %d, %d, %d, %s, %s, %d)",
				sql_table_name(), p->state.last_check,
				NEBTYPE_HOSTCHECK_PROCESSED, host_name,
				p->state.current_state,
				p->state.state_type == HARD_STATE || p->state.current_state == STATE_UP,
				p->state.current_attempt, output,
				sql_safe_unescaped_long_output,
				p->state.scheduled_downtime_depth);
	}

	/*
	 * Stash host performance data separately, in case
	 * people people are using Merlin with Nagiosgrapher or
	 * similar performance data graphing solutions.
	 */
	if (perf_log) {
		result = sql_query
			("INSERT INTO %s(timestamp, host_name, perfdata) "
				"VALUES(%lu, %s, %s)",
				host_perf_table, p->state.last_check, host_name, perf_data);
	}

	free(host_name);
	safe_free(output);
	safe_free(long_output);
	safe_free(sql_safe_unescaped_long_output);
	safe_free(perf_data);
	return result;
}

static int handle_service_status(int cb, const merlin_service_status *p)
{
	char *host_name, *service_description;
	char *output = NULL, *long_output = NULL, *perf_data = NULL;
	char *sql_safe_unescaped_long_output = NULL;
	int result = 0, rpt_log = 0, perf_log = 0;

	if (cb == NEBCALLBACK_SERVICE_CHECK_DATA) {
		if (db_log_reports && (p->nebattr & (NEBATTR_CHECK_ALERT | NEBATTR_CHECK_FIRST)))
			rpt_log = 1;

		if (service_perf_table && p->state.perf_data && *p->state.perf_data)
			perf_log = 1;
	}

	if (!rpt_log && !perf_log)
		return 0;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->service_description, &service_description);
	if (rpt_log) {
		char *unescaped_long_output = NULL;
		sql_quote(p->state.plugin_output, &output);
		if(rpt_log && p->state.long_plugin_output) {
			size_t long_len = strlen(p->state.long_plugin_output) + 1;
			if ((unescaped_long_output = malloc(long_len)) == NULL) {
				lerr("failed to allocate memory for unescaped long output");
				return 1;
			}
			unescape_newlines(unescaped_long_output, p->state.long_plugin_output, long_len);
			sql_quote(unescaped_long_output, &sql_safe_unescaped_long_output);
			free(unescaped_long_output);
			unescaped_long_output = NULL;
		}

		sql_quote(p->state.long_plugin_output, &long_output);
	}

	if (perf_log)
		sql_quote(p->state.perf_data, &perf_data);

	if (rpt_log) {
		result = sql_query
			("INSERT INTO %s(timestamp, event_type, host_name, "
				"service_description, state, hard, retry, output, long_output, downtime_depth) "
				"VALUES(%lu, %d, %s, %s, %d, '%d', '%d', %s, %s, %d)",
				sql_table_name(), p->state.last_check,
				NEBTYPE_SERVICECHECK_PROCESSED, host_name,
				service_description, p->state.current_state,
				p->state.state_type == HARD_STATE || p->state.current_state == STATE_OK,
				p->state.current_attempt, output,
				sql_safe_unescaped_long_output,
				p->state.scheduled_downtime_depth);
	}

	/*
	 * Stash service performance data separately, in case
	 * people people are using Merlin with Nagiosgrapher or
	 * similar performance data graphing solutions.
	 */
	if (perf_log) {
		result = sql_query
			("INSERT INTO %s(timestamp, host_name, "
				"service_description, perfdata) "
				"VALUES(%lu, %s, %s, %s)",
				service_perf_table, p->state.last_check,
				host_name, service_description, perf_data);
	}

	free(host_name);
	free(service_description);
	safe_free(output);
	safe_free(long_output);
	safe_free(sql_safe_unescaped_long_output);
	safe_free(perf_data);
	return result;
}

static int rpt_downtime(void *data)
{
	nebstruct_downtime_data *ds = (nebstruct_downtime_data *)data;
	int depth, result;
	char *host_name;

	if (!db_log_reports)
		return 0;

	switch (ds->type) {
	case NEBTYPE_DOWNTIME_START:
	case NEBTYPE_DOWNTIME_STOP:
		break;
	default:
		return 0;
	}

	sql_quote(ds->host_name, &host_name);
	if (ds->service_description) {
		char *service_description;

		sql_quote(ds->service_description, &service_description);
		depth = ds->type == NEBTYPE_DOWNTIME_START;
		result = sql_query("INSERT INTO %s"
						   "(timestamp, event_type, host_name,"
						   "service_description, downtime_depth) "
						   "VALUES(%lu, %d, %s, %s, %d)",
						   sql_table_name(),
						   ds->timestamp.tv_sec, ds->type, host_name,
						   service_description, depth);
		free(service_description);
	} else {
		depth = ds->type == NEBTYPE_DOWNTIME_START;
		result = sql_query("INSERT INTO %s"
						   "(timestamp, event_type, host_name, downtime_depth)"
						   "VALUES(%lu, %d, %s, %d)",
						   sql_table_name(),
						   ds->timestamp.tv_sec, ds->type, host_name, depth);
	}
	free(host_name);

	return result;
}

static int rpt_process_data(void *data)
{
	nebstruct_process_data *ds = (nebstruct_process_data *)data;

	if (!db_log_reports)
		return 0;

	switch(ds->type) {
	case NEBTYPE_PROCESS_EVENTLOOPSTART:
		ds->type = NEBTYPE_PROCESS_START;
		break;
	case NEBTYPE_PROCESS_START:
	case NEBTYPE_PROCESS_SHUTDOWN:
		break;
	case NEBTYPE_PROCESS_RESTART:
		ds->type = NEBTYPE_PROCESS_SHUTDOWN;
		break;
	default:
		return 0;
	}

	return sql_query("INSERT INTO %s(timestamp, event_type) "
					 "VALUES(%lu, %d)",
					 sql_table_name(), ds->timestamp.tv_sec, ds->type);
}

static int handle_flapping(const nebstruct_flapping_data *p)
{
	int result = 0;
	char *host_name, *service_description = NULL;

	if (!db_log_reports)
		return 0;

	sql_quote(p->host_name, &host_name);
	sql_quote(p->service_description, &service_description);

	if (service_description) {
		if (db_log_reports) {
			result = sql_query
				("INSERT INTO %s(timestamp, event_type, host_name, service_description) VALUES(%lu, %d, %s, %s)",
				 sql_table_name(), p->timestamp.tv_sec, p->type, host_name,
				 service_description);

			if (result) {
				lerr("failed to insert flapping data (host: %s, service: %s, type: %d) into %s",
						host_name, service_description, p->type, sql_table_name());
			}
		}
		free(service_description);
	} else {
		if (db_log_reports) {
			result = sql_query
				("INSERT INTO %s(timestamp, event_type, host_name) VALUES(%lu, %d, %s)",
				 sql_table_name(), p->timestamp.tv_sec, p->type, host_name);

			if (result) {
				lerr("failed to insert flapping data (host: %s, type: %d) into %s",
						host_name, p->type, sql_table_name());
			}
		}
	}

	free(host_name);

	return result;
}

static int handle_contact_notification_method(const nebstruct_contact_notification_method_data *p)
{
	int result;
	char *contact_name, *host_name, *service_description;
	char *output, *ack_author, *ack_data, *command_name;

	if (!db_log_notifications)
		return 0;

	sql_quote(p->contact_name, &contact_name);
	sql_quote(p->host_name, &host_name);
	sql_quote(p->service_description, &service_description);
	sql_quote(p->output, &output);
	sql_quote(p->ack_author, &ack_author);
	sql_quote(p->ack_data, &ack_data);
	sql_quote(p->command_name, &command_name);

	result = sql_query
		("INSERT INTO notification "
		 "(notification_type, start_time, end_time, "
		 "contact_name, host_name, service_description, "
		 "command_name, reason_type, state, output,"
		 "ack_author, ack_data, escalated) "
		 "VALUES(%d, %lu, %lu, "
		 "%s, %s, %s, "
		 "%s, %d, %d, %s, "
		 "%s, %s, %d)",
		 p->notification_type, p->start_time.tv_sec, p->end_time.tv_sec,
		 contact_name, host_name,  safe_str(service_description),
		 command_name, p->reason_type, p->state, safe_str(output),
		 safe_str(ack_author), safe_str(ack_data), p->escalated);

	free(host_name);
	free(contact_name);
	safe_free(service_description);
	safe_free(output);
	safe_free(ack_author);
	safe_free(ack_data);
	free(command_name);

	return result;
}

int mrm_db_update(merlin_node *node, merlin_event *pkt)
{
	int errors = 0;

	if (!sql_is_connected(1))
		return 0;

	if (!pkt) {
		lerr("pkt is NULL in mrm_db_update()");
		return 0;
	}
	if (!pkt->body) {
		lerr("pkt->body is NULL in mrm_db_update()");
		return 0;
	}

	if (merlin_decode_event(node, pkt)) {
		return 0;
	}

	switch (pkt->hdr.type) {
	case NEBCALLBACK_PROGRAM_STATUS_DATA:
		break;
	case NEBCALLBACK_PROCESS_DATA:
		errors = rpt_process_data(pkt->body);
		break;
	case NEBCALLBACK_COMMENT_DATA:
		break;
	case NEBCALLBACK_DOWNTIME_DATA:
		errors = rpt_downtime((void *)pkt->body);
		break;
	case NEBCALLBACK_FLAPPING_DATA:
		errors = handle_flapping((void *)pkt->body);
		break;
	case NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA:
		errors = handle_contact_notification_method((void *)pkt->body);
		break;
	case NEBCALLBACK_HOST_CHECK_DATA:
	case NEBCALLBACK_HOST_STATUS_DATA:
		errors = handle_host_status((int)pkt->hdr.type, (void *)pkt->body);
		break;
	case NEBCALLBACK_SERVICE_CHECK_DATA:
	case NEBCALLBACK_SERVICE_STATUS_DATA:
		errors = handle_service_status((int)pkt->hdr.type, (void *)pkt->body);
		break;

	/* some callbacks are unhandled by design */
	case NEBCALLBACK_NOTIFICATION_DATA:
	case NEBCALLBACK_CONTACT_NOTIFICATION_DATA:
	case NEBCALLBACK_EXTERNAL_COMMAND_DATA:
		return 0;

	default:
		lerr("Unknown callback type %d. Weird, to say the least...",
			 pkt->hdr.type);
		return -1;
	}
	sql_free_result();

	return errors;
}
