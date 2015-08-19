#include <sys/types.h>
#include <signal.h>

#include "logging.h"
#include <naemon/naemon.h>
#include "shared.h"
#include "sql.h"
#include "state.h"
#include "lparse.h"
#include "logutils.h"
#include "cfgfile.h"
#include <stdint.h> /* standard fixed-size integer types. */
#include <inttypes.h> /* PRIxxx printf specifiers. */
#include <sys/time.h>
#define IGNORE_LINE 0

#define CONCERNS_HOST 50
#define CONCERNS_SERVICE 60

#define MAX_NVECS 16
#define HASH_TABLE_SIZE 128

#define PROGRESS_INTERVAL 25000 /* lines to parse between progress updates */


static const char *progname;
static char *db_table;
static int only_notifications;
static unsigned long long imported, totsize, totlines, skipped;
static int lines_since_progress, do_progress, list_files;
static struct timeval import_start;
static time_t daemon_start, daemon_stop, incremental;
static int daemon_is_running;
static uint skipped_files;
static int repair_table;

static time_t ltime; /* the timestamp from the current log-line */

static struct string_code event_codes[] = {
	add_ignored("Error"),
	add_ignored("Warning"),
	add_ignored("LOG ROTATION"),
	add_ignored("HOST FLAPPING ALERT"),
	add_ignored("SERVICE FLAPPING ALERT"),
	add_ignored("SERVICE EVENT HANDLER"),
	add_ignored("HOST EVENT HANDLER"),
	add_ignored("SERVICE NOTIFICATION SUPPRESSED"),
	add_ignored("HOST NOTIFICATION SUPPRESSED"),
	add_ignored("SERVICE CONTACT NOTIFICATION SUPPRESSED"),
	add_ignored("HOST CONTACT NOTIFICATION SUPPRESSED"),
	add_ignored("LOG VERSION"),
	add_ignored("livestatus"),
	add_ignored("TIMEPERIOD TRANSITION"),
	add_ignored("wproc"),
	add_ignored("qh"),
	add_ignored("nerd"),

	add_code(5, "HOST NOTIFICATION", NEBTYPE_NOTIFICATION_END + CONCERNS_HOST),
	add_code(6, "SERVICE NOTIFICATION", NEBTYPE_NOTIFICATION_END + CONCERNS_SERVICE),
	add_code(3, "PASSIVE HOST CHECK", NEBTYPE_HOSTCHECK_PROCESSED),
	add_code(4, "PASSIVE SERVICE CHECK", NEBTYPE_SERVICECHECK_PROCESSED),
	add_code(0, "EXTERNAL COMMAND", NEBTYPE_EXTERNALCOMMAND_END),
	add_code(5, "HOST ALERT", NEBTYPE_HOSTCHECK_PROCESSED),
	add_code(5, "INITIAL HOST STATE", NEBTYPE_HOSTCHECK_PROCESSED),
	add_code(5, "CURRENT HOST STATE", NEBTYPE_HOSTCHECK_PROCESSED),
	add_code(6, "SERVICE ALERT", NEBTYPE_SERVICECHECK_PROCESSED),
	add_code(6, "INITIAL SERVICE STATE", NEBTYPE_SERVICECHECK_PROCESSED),
	add_code(6, "CURRENT SERVICE STATE", NEBTYPE_SERVICECHECK_PROCESSED),
	add_code(3, "HOST DOWNTIME ALERT", NEBTYPE_DOWNTIME_LOAD + CONCERNS_HOST),
	add_code(4, "SERVICE DOWNTIME ALERT", NEBTYPE_DOWNTIME_LOAD + CONCERNS_SERVICE),
	{ 0, NULL, 0, 0 },
};

static struct string_code command_codes[] = {
	add_cdef(1, DEL_HOST_DOWNTIME),
	add_cdef(1, DEL_SVC_DOWNTIME),
	add_cdef(8, SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME),
	add_cdef(8, SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME),
	add_cdef(8, SCHEDULE_HOSTGROUP_HOST_DOWNTIME),
	add_cdef(8, SCHEDULE_HOSTGROUP_SVC_DOWNTIME),
	add_cdef(8, SCHEDULE_HOST_DOWNTIME),
	add_cdef(8, SCHEDULE_HOST_SVC_DOWNTIME),
	add_cdef(8, SCHEDULE_SERVICEGROUP_HOST_DOWNTIME),
	add_cdef(8, SCHEDULE_SERVICEGROUP_SVC_DOWNTIME),
	add_cdef(8, SCHEDULE_SVC_DOWNTIME),

	/*
	 * These really have one more field than listed here. We omit one
	 * to make author and comment concatenated with a semi-colon by default.
	 */
	add_cdef(6, ACKNOWLEDGE_SVC_PROBLEM),
	add_cdef(5, ACKNOWLEDGE_HOST_PROBLEM),
	{ 0, NULL, 0, 0 },
};


static void handle_sql_result(int errors, const char *table)
{
	if (!errors || !sql_table_crashed)
		return;

	if (repair_table) {
		printf("Repairing table '%s'. This may take a very long time. Please be patient\n", table);
		sql_repair_table(table);
	}
	else {
		crash("Database table '%s' appears to have crashed. Please run\n  mysqlrepair %s.%s",
		      table, sql_db_name(), table);
	}
}

static int insert_host_result(nebstruct_host_check_data *ds)
{
	int result;
	char *host_name = NULL, *output = NULL;

	if (!host_has_new_state(ds->host_name, ds->state, ds->state_type)) {
		linfo("state not changed for host '%s'", ds->host_name);
		return 0;
	}

	sql_quote(ds->host_name, &host_name);
	sql_quote(ds->output, &output);
	result = sql_query
		("INSERT INTO %s("
		 "timestamp, event_type, host_name, state, "
		 "hard, retry, output"
		 ") VALUES(%lu, %d, %s, %d, %d, %d, %s)",
		 db_table,
		 ds->timestamp.tv_sec, ds->type, host_name, ds->state,
		 ds->state_type == HARD_STATE || ds->state == 0, ds->current_attempt,
		 output);

	free(host_name);
	free(output);

	return result;
}

static int insert_service_result(nebstruct_service_check_data *ds)
{
	int result;
	char *host_name, *service_description, *output;

	if (!service_has_new_state(ds->host_name, ds->service_description, ds->state, ds->state_type)) {
		linfo("state not changed for service '%s' on host '%s'",
			  ds->service_description, ds->host_name);
		return 0;
	}

	sql_quote(ds->host_name, &host_name);
	sql_quote(ds->service_description, &service_description);
	sql_quote(ds->output, &output);
	result = sql_query
		("INSERT INTO %s ("
		 "timestamp, event_type, host_name, service_description, state, "
		 "hard, retry, output) "
		 "VALUES(%lu, %d, %s, %s, '%d', '%d', '%d', %s)",
		 db_table,
		 ds->timestamp.tv_sec, ds->type, host_name,
		 service_description, ds->state,
		 ds->state_type == HARD_STATE || ds->state == 0, ds->current_attempt,
		 output);
	free(host_name);
	free(service_description);
	free(output);
	return result;
}

static int sql_insert_downtime(nebstruct_downtime_data *ds)
{
	int result;
	char *host_name, *service_description;

	sql_quote(ds->host_name, &host_name);
	if (ds->service_description) {
		sql_quote(ds->service_description, &service_description);

		result = sql_query
			("INSERT INTO %s("
			 "timestamp, event_type, host_name,"
			 "service_description, downtime_depth) "
			 "VALUES(%lu, %d, %s, %s, %d)",
			 db_table,
			 ds->timestamp.tv_sec, ds->type, host_name,
			 service_description, ds->type == NEBTYPE_DOWNTIME_START);
		free(service_description);
	} else {
		result = sql_query
			("INSERT INTO %s("
			 "timestamp, event_type, host_name, downtime_depth)"
			 "VALUES(%lu, %d, %s, %d)",
			 db_table,
			 ds->timestamp.tv_sec, ds->type, host_name,
			 ds->type == NEBTYPE_DOWNTIME_START);
	}
	free(host_name);
	return result;
}

static int insert_process_data(nebstruct_process_data *ds)
{
	switch(ds->type) {
	case NEBTYPE_PROCESS_START:
	case NEBTYPE_PROCESS_SHUTDOWN:
		break;
	case NEBTYPE_PROCESS_RESTART:
		ds->type = NEBTYPE_PROCESS_SHUTDOWN;
		break;
	default:
		return 0;
	}

	return sql_query
		("INSERT INTO %s(timestamp, event_type) "
		 "VALUES(%lu, %d)",
		 db_table, ds->timestamp.tv_sec, ds->type);
}

static inline void print_strvec(char **v, int n)
{
	int i;

	for (i = 0; i < n; i++)
		printf("v[%2d]: %s\n", i, v[i]);
}


static void show_progress(void)
{
	time_t eta, elapsed;
	float pct_done, real_pct_done;

	totlines += lines_since_progress;
	lines_since_progress = 0;

	if (!do_progress)
		return;

	elapsed = time(NULL) - import_start.tv_sec;
	if (!elapsed)
		elapsed = 1;

	real_pct_done = (float)imported / (float)(totsize - skipped) * 100;
	pct_done = ((float)(imported + skipped) / (float)totsize) * 100;
	eta = (elapsed / real_pct_done) * (100.0 - real_pct_done);

	printf("Importing data: %.2f%% (%s) done ",
		   pct_done, human_bytes(imported + skipped));
	if (elapsed > 10) {
		printf("ETA: ");
		if (eta > 60)
			printf("%lum%lus", eta / 60, eta % 60);
		else
			printf("%lus", eta);
	}
	printf("        \r");
	fflush(stdout);
}

static void end_progress(void)
{
	struct timeval tv;

	if (list_files)
		return;

	gettimeofday(&tv, NULL);

	/*
	 * If any of the logfiles doesn't have a newline
	 * at end of file, imported will be slightly off.
	 * We set it hard here so as to make sure that
	 * the final progress output stops at exactly 100%
	 */
	imported = totsize - skipped;

	show_progress();
	putchar('\n');
	printf("%s, %llu lines imported in %s.",
		   human_bytes(totsize), totlines, tv_delta(&import_start, &tv));
	if (skipped)
		printf(" %s in %u files skipped.", human_bytes(skipped), skipped_files);
	putchar('\n');
}

static int indexes_disabled;
static void disable_indexes(void)
{
	if (indexes_disabled)
		return;

	/*
	 * if we're more than 95% done before inserting anything,
	 * such as might be the case when running an incremental
	 * import, we might as well not bother with disabling
	 * the indexes, since enabling them again can take quite
	 * a long time
	 */
	if (((float)(skipped + imported) / (float)totsize) * 100 >= 95.0)
		return;

	/*
	 * We lock the table we'll be working with and disable
	 * indexes on it. Otherwise doing the actual inserts
	 * will take just about forever, as MySQL has to update
	 * and flush the index cache between each operation.
	 */
	if (sql_query("ALTER TABLE %s DISABLE KEYS", db_table))
		crash("Failed to disable keys: %s", sql_error_msg());
	if (sql_query("LOCK TABLES %s WRITE, report_data_extras WRITE", db_table))
		crash("Failed to lock table %s: %s", db_table, sql_error_msg());

	indexes_disabled = 1;
}

static void insert_extras(void)
{
	sql_query("INSERT INTO %s (`timestamp`, `event_type`, `flags`, `attrib`, `host_name`, `service_description`, `state`, `hard`, `retry`, `downtime_depth`, `output`) SELECT `timestamp`, `event_type`, `flags`, `attrib`, `host_name`, `service_description`, `state`, `hard`, `retry`, `downtime_depth`, `output` FROM report_data_extras;", db_table);
}

static void enable_indexes(void)
{
	db_wrap_result *result = NULL;
	int64_t entries;
	time_t start;

	/* if we haven't disabled the indexes we can quit early */
	if (!indexes_disabled)
		return;

	sql_query("SELECT count(1) FROM %s", db_table);
	if (!(result = sql_get_result()))
		entries = 0;
	else {
			    if (0 == result->api->step(result)) {
			        result->api->get_int64_ndx(result, 0, &entries);
		} else {
		    entries = 0;
		}
		sql_free_result();
	}

	signal(SIGINT, SIG_IGN);
	sql_query("UNLOCK TABLES");
	start = time(NULL);
	printf("Creating sql table indexes. This will likely take ~%"PRIi64" seconds\n",
		   (entries / 50000) + 1);
	sql_query("ALTER TABLE %s ENABLE KEYS", db_table);
	printf("%lu database entries indexed in %lu seconds\n",
		   entries, time(NULL) - start);
}

static int insert_downtime_event(int type, char *host, char *service)
{
	nebstruct_downtime_data ds;
	int result;

	if (!is_interesting_service(host, service))
		return 0;

	if (!use_database || only_notifications)
		return 0;

	memset(&ds, 0, sizeof(ds));

	ds.type = type;
	ds.timestamp.tv_sec = ltime;
	ds.host_name = host;
	ds.service_description = service;
	ds.downtime_id = 0;

	disable_indexes();
	result = sql_insert_downtime(&ds);
	if (result < 0)
		lp_crash("Failed to insert downtime:\n  type=%d, host=%s, service=%s",
				 type, host, service);

	return result;
}

typedef struct import_notification {
	int type, reason, state;
} import_notification;

static int parse_import_notification(char *str, import_notification *n)
{
	char *state_str = str;

	n->reason = parse_notification_reason(str);
	if (n->reason != NOTIFICATION_NORMAL) {
		char *space, *paren;

		space = strchr(str, ' ');
		if (!space)
			return -1;
		paren = strchr(space, ')');
		if (!paren)
			return -1;
		*paren = '\0';

		state_str = space + 2;
	}

	n->type = SERVICE_NOTIFICATION;
	n->state = parse_service_state_gently(state_str);
	if (n->state < 0) {
		n->type = HOST_NOTIFICATION;
		n->state = parse_host_state_gently(state_str);
	}

	return 0;
}

static int insert_notification(struct string_code *sc)
{
	int base_idx, result;
	char *contact_name, *host_name, *service_description;
	char *command_name, *output;
	struct import_notification n;

	if (!only_notifications)
		return 0;

	if (sc->code - NEBTYPE_NOTIFICATION_END == CONCERNS_SERVICE) {
		base_idx = 1;
	} else {
		base_idx = 0;
	}
	if (parse_import_notification(strv[base_idx + 2], &n) < 0) {
		handle_unknown_event(strv[base_idx + 2]);
		return 0;
	}

	if (!use_database)
		return 0;

	disable_indexes();
	sql_quote(strv[0], &contact_name);
	sql_quote(strv[1], &host_name);
	if (base_idx) {
		sql_quote(strv[2], &service_description);
	} else {
		service_description = NULL;
	}
	sql_quote(strv[base_idx + 3], &command_name);
	sql_quote(strv[base_idx + 4], &output);
	result = sql_query
		("INSERT INTO %s("
		 "notification_type, start_time, end_time, contact_name, "
		 "host_name, service_description, "
		 "command_name, output, "
		 "state, reason_type) "
		 "VALUES("
		 "%d, %lu, %lu, %s, "
		 "%s, %s, "
		 "%s, %s, "
		 "%d, %d)",
		 db_table,
		 n.type, ltime, ltime, contact_name,
		 host_name, safe_str(service_description),
		 command_name, output,
		 n.state, n.reason);
	free(contact_name);
	free(host_name);
	safe_free(service_description);
	free(command_name);
	free(output);
	return result;
}

static int insert_service_check(struct string_code *sc)
{
	nebstruct_service_check_data ds;

	if (!is_interesting_service(strv[0], strv[1]))
		return 0;

	memset(&ds, 0, sizeof(ds));

	ds.timestamp.tv_sec = ltime;
	ds.type = sc->code;
	ds.host_name = strv[0];
	ds.service_description = strv[1];
	if (sc->nvecs == 4) {
		/* passive service check result */
		if (*strv[2] >= '0' && *strv[2] <= '9')
			ds.state = atoi(strv[2]);
		else
			ds.state = parse_service_state(strv[2]);
		ds.state_type = HARD_STATE;
		ds.current_attempt = 1;
		ds.output = strv[3];
	} else {
		ds.state = parse_service_state(strv[2]);
		ds.state_type = soft_hard(strv[3]);
		ds.current_attempt = atoi(strv[4]);
		ds.output = strv[5];
	}

	if (!use_database || only_notifications)
		return 0;

	disable_indexes();
	return insert_service_result(&ds);
}

static int insert_host_check(struct string_code *sc)
{
	nebstruct_host_check_data ds;

	if (!is_interesting_host(strv[0]))
		return 0;

	memset(&ds, 0, sizeof(ds));

	ds.timestamp.tv_sec = ltime;
	ds.type = sc->code;
	ds.host_name = strv[0];
	if (sc->nvecs == 3) {
		if (*strv[1] >= '0' && *strv[1] <= '9')
			ds.state = atoi(strv[1]);
		else
			ds.state = parse_host_state(strv[1]);
		/* passive host check result */
		ds.output = strv[2];
		ds.current_attempt = 1;
		ds.state_type = HARD_STATE;
	} else {
		ds.state = parse_host_state(strv[1]);
		ds.state_type = soft_hard(strv[2]);
		ds.current_attempt = atoi(strv[3]);
		ds.output = strv[4];
	}

	if (!use_database || only_notifications)
		return 0;

	disable_indexes();
	return insert_host_result(&ds);
}

static int insert_process_event(int type)
{
	nebstruct_process_data ds;

	if (!use_database || only_notifications)
		return 0;

	memset(&ds, 0, sizeof(ds));
	ds.timestamp.tv_sec = ltime;
	ds.type = type;
	disable_indexes();
	return insert_process_data(&ds);
}

#if 0
static int insert_acknowledgement(struct string_code *sc)
{
	return 0;
}
#else
# define insert_acknowledgement(foo) /* nothing */ ;
#endif

static int insert_downtime(struct string_code *sc)
{
	int type;
	char *host, *service = NULL;

	host = strv[0];
	if (sc->nvecs == 4) {
		service = strv[1];
	}
	/*
	 * to stop a downtime we can either get STOPPED or
	 * CANCELLED. So far, I've only ever seen STARTED
	 * for when it actually starts though, and since
	 * the Nagios daemon is reponsible for launching
	 * it, it's unlikely there are more variants of
	 * that string
	 */
	type = NEBTYPE_DOWNTIME_STOP;
	if (!strcmp(strv[sc->nvecs - 2], "STARTED"))
		type = NEBTYPE_DOWNTIME_START;

	switch (type) {
	case NEBTYPE_DOWNTIME_START:
		insert_downtime_event(NEBTYPE_DOWNTIME_START, host, service);
		break;

	case NEBTYPE_DOWNTIME_STOP:
		insert_downtime_event(NEBTYPE_DOWNTIME_STOP, host, service);
		break;

	default:
		return -1;
	}

	return 0;
}

static inline void handle_start_event(void)
{
	if (!daemon_is_running)
		insert_process_event(NEBTYPE_PROCESS_START);

	daemon_start = ltime;
	daemon_is_running = 1;
}

static inline void handle_stop_event(void)
{
	if (daemon_is_running) {
		insert_process_event(NEBTYPE_PROCESS_SHUTDOWN);
		daemon_is_running = 0;
	}
	daemon_stop = ltime;
}

static int parse_line(char *line, uint len)
{
	char *ptr, *colon;
	int result = 0, nvecs = 0;
	struct string_code *sc;
	static time_t last_ltime = 0;

	imported += len + 1; /* make up for 1 lost byte per newline */
	line_no++;

	/* ignore empty lines */
	if (!len)
		return 0;

	if (++lines_since_progress >= PROGRESS_INTERVAL)
		show_progress();

	/* skip obviously bogus lines */
	if (len < 12 || *line != '[') {
		warn("line %d; len too short, or line doesn't start with '[' (%s)", line_no, line);
		return -1;
	}

	ltime = strtoul(line + 1, &ptr, 10);
	if (line + 1 == ptr) {
		lp_crash("Failed to parse log timestamp from '%s'. I can't handle malformed logdata", line);
		return -1;
	}

	if (ltime < last_ltime) {
//		warn("ltime < last_ltime (%lu < %lu) by %lu. Compensating...",
//			 ltime, last_ltime, last_ltime - ltime);
		ltime = last_ltime;
	}
	else
		last_ltime = ltime;

	/*
	 * Incremental will be 0 if not set, or 1 if set but
	 * the database is currently empty.
	 * Note that this will not always do the correct thing,
	 * as downtime entries that might have been scheduled for
	 * purging may never show up as "stopped" in the database
	 * with this scheme. As such, incremental imports absolutely
	 * require that nothing is in scheduled downtime when the
	 * import is running (well, started really, but it amounts
	 * to the same thing).
	 */
	if (ltime < incremental)
		return 0;

	while (*ptr == ']' || *ptr == ' ')
		ptr++;

	if (!is_interesting(ptr))
		return 0;

	if (!(colon = strchr(ptr, ':'))) {
		/* stupid heuristic, but might be good for something,
		 * somewhere, sometime. if nothing else, it should suppress
		 * annoying output */
		if (is_start_event(ptr)) {
			handle_start_event();
			return 0;
		}
		if (is_stop_event(ptr)) {
			handle_stop_event();
			return 0;
		}

		/*
		 * An unhandled event. We should probably crash here
		 */
		handle_unknown_event(line);
		return -1;
	}

	/* an event happened without us having gotten a start-event */
	if (!daemon_is_running) {
		insert_process_event(NEBTYPE_PROCESS_START);
		daemon_start = ltime;
		daemon_is_running = 1;
	}

	if (!(sc = get_event_type(ptr, colon - ptr))) {
		handle_unknown_event(line);
		return -1;
	}

	if (sc->code == IGNORE_LINE)
		return 0;

	/*
	 * break out early if we know we won't handle this event
	 * There's no point in parsing a potentially huge amount
	 * of lines we're not even interested in
	 */
	switch (sc->code) {
	case NEBTYPE_NOTIFICATION_END + CONCERNS_HOST:
	case NEBTYPE_NOTIFICATION_END + CONCERNS_SERVICE:
		if (only_notifications)
			break;
		return 0;
	default:
		if (only_notifications)
			return 0;
		break;
	}

	*colon = 0;
	ptr = colon + 1;
	while (*ptr == ' ')
		ptr++;

	if (sc->nvecs) {
		int i;

		nvecs = vectorize_string(ptr, sc->nvecs);

		if (nvecs != sc->nvecs) {
			/* broken line */
			warn("Line %d in %s seems to not have all the fields it should",
				 line_no, cur_file->path);
			return -1;
		}

		for (i = 0; i < sc->nvecs; i++) {
			if (!strv[i]) {
				/* this should never happen */
				warn("Line %d in %s seems to be broken, or we failed to parse it into a vector",
					 line_no, cur_file->path);
				return -1;
			}
		}
	}

	switch (sc->code) {
		char *semi_colon;

	case NEBTYPE_EXTERNALCOMMAND_END:
		semi_colon = strchr(ptr, ';');
		if (!semi_colon)
			return 0;
		if (!(sc = get_command_type(ptr, semi_colon - ptr))) {
			return 0;
		}
		if (sc->code == RESTART_PROGRAM) {
			handle_stop_event();
			return 0;
		}

		nvecs = vectorize_string(semi_colon + 1, sc->nvecs);
		if (nvecs != sc->nvecs) {
			warn("nvecs discrepancy: %d vs %d (%s)\n", nvecs, sc->nvecs, ptr);
		}
		if (sc->code == ACKNOWLEDGE_HOST_PROBLEM ||
			sc->code == ACKNOWLEDGE_SVC_PROBLEM)
		{
			insert_acknowledgement(sc);
		}
		break;

	case NEBTYPE_HOSTCHECK_PROCESSED:
		result = insert_host_check(sc);
		break;

	case NEBTYPE_SERVICECHECK_PROCESSED:
		result = insert_service_check(sc);
		break;

	case NEBTYPE_DOWNTIME_LOAD + CONCERNS_HOST:
	case NEBTYPE_DOWNTIME_LOAD + CONCERNS_SERVICE:
		result = insert_downtime(sc);
		break;

	case NEBTYPE_NOTIFICATION_END + CONCERNS_HOST:
	case NEBTYPE_NOTIFICATION_END + CONCERNS_SERVICE:
		result = insert_notification(sc);
		break;

	case IGNORE_LINE:
		return 0;
	}

	handle_sql_result(result, db_table);
	return 0;
}

static int parse_one_line(char *str, uint len)
{
	const char *msg;

	if (parse_line(str, len) && use_database && sql_error(&msg))
		lp_crash("sql error: %s", msg);

	return 0;
}

static int hash_one_line(char *line, __attribute__((unused)) uint len)
{
	return add_interesting_object(line);
}

static int hash_interesting(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		lp_crash("failed to stat %s: %s", path, strerror(errno));

	lparse_path(path, st.st_size, hash_one_line);

	return 0;
}


__attribute__((__format__(__printf__, 1, 2)))
static void usage(const char *fmt, ...)
{
	if (fmt && *fmt) {
		va_list ap;

		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
		va_end(ap);
	}

	printf("Usage %s [options] [logfiles]\n\n", progname);
	printf("  [logfiles] refers to all the nagios logfiles you want to import\n");
	printf("  If --nagios-cfg is given or can be inferred no logfiles need to be supplied\n");
	printf("\nOptions:\n");
	printf("  --help                             this cruft\n");
	printf("  --no-progress                      don't display progress output\n");
	printf("  --no-sql                           don't access the database\n");
	printf("  --db-name                          database name\n");
	printf("  --db-table                         database table name\n");
	printf("  --db-user                          database user\n");
	printf("  --db-pass                          database password\n");
	printf("  --db-host                          database host\n");
	printf("  --db-port                          database port\n");
	printf("  --db-type                          database type\n");
	printf("  --db-conn-str                      database connection string\n");
	printf("  --[no-]repair]                     should we autorepair tables?\n");
	printf("  --incremental[=<when>]             do an incremental import (since $when)\n");
	printf("  --truncate-db                      truncate database before importing\n");
	printf("  --only-notifications               only import notifications\n");
	printf("  --nagios-cfg=</path/to/nagios.cfg> path to nagios.cfg\n");
	printf("  --list-files                       list files to import\n");
	printf("\n\n");

	if (fmt && *fmt)
		exit(1);

	exit(0);
}

int main(int argc, char **argv)
{
	int i, truncate_db = 0;
	const char *nagios_cfg = NULL;
	char *db_name, *db_user, *db_pass;
	char *db_conn_str, *db_host, *db_port, *db_type;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	use_database = 1;
	db_name = db_user = db_pass = NULL;
	db_conn_str = db_host = db_port = db_type = NULL;

	do_progress = isatty(fileno(stdout));

	strv = calloc(sizeof(char *), MAX_NVECS);
	if (!strv)
		crash("Failed to alloc initial structs");


	for (num_nfile = 0,i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];
		int arg_len, eq_opt = 0;

		if ((opt = strchr(arg, '='))) {
			*opt++ = '\0';
			eq_opt = 1;
		}
		else if (i < argc - 1) {
			opt = argv[i + 1];
		}

		if (!prefixcmp(arg, "-h") || !prefixcmp(arg, "--help")) {
			usage(NULL);
		}
		if (!prefixcmp(arg, "--incremental")) {
			incremental = 1;

			/*
			 * nifty for debugging --incremental skipping log-files
			 * The value will be overwritten unless --no-sql is also
			 * in effect
			 */
			if (eq_opt) {
				incremental = strtoul(opt, NULL, 0);
				if (!incremental)
					usage("--incremental= requires a parameter");
				/*
				 * since we use '1' to mean "determine automatically",
				 * we magic a '1' from userspace to '2'. In practice,
				 * this just means the user doesn't need to know a
				 * thing about this program's internals.
				 */
				if (incremental == 1)
					incremental = 2;
			}
			continue;
		}
		if (!prefixcmp(arg, "--no-sql")) {
			use_database = 0;
			continue;
		}
		if (!prefixcmp(arg, "--no-repair")) {
			repair_table = 0;
			continue;
		}
		if (!prefixcmp(arg, "--repair")) {
			repair_table = 1;
			continue;
		}
		if (!prefixcmp(arg, "--only-notifications")) {
			only_notifications = 1;
			db_table = db_table ? db_table : "notification";
			continue;
		}
		if (!prefixcmp(arg, "--no-progress")) {
			do_progress = 0;
			continue;
		}
		if (!prefixcmp(arg, "--debug") || !prefixcmp(arg, "-d")) {
			do_progress = 0;
			logs_debug_level++;
			continue;
		}
		if (!prefixcmp(arg, "--truncate-db")) {
			truncate_db = 1;
			continue;
		}
		if (!prefixcmp(arg, "--list-files")) {
			list_files = 1;
			do_progress = 0;
			continue;
		}
		if (!prefixcmp(arg, "--nagios-cfg")) {
			if (!opt || !*opt) {
				crash("%s requires the path to nagios.cfg as argument", arg);
			}
			nagios_cfg = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-name")) {
			if (!opt || !*opt)
				crash("%s requires a database name as an argument", arg);
			db_name = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-user")) {
			if (!opt || !*opt)
				crash("%s requires a database username as argument", arg);
			db_user = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-pass")) {
			if (!opt || !*opt)
				crash("%s requires a database username as argument", arg);
			db_pass = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-table")) {
			if (!opt || !*opt)
				crash("%s requires a database table name as argument", arg);
			db_table = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-conn-str")) {
			if (!opt || !*opt)
				crash("%s requires a connection string as argument", arg);
			db_conn_str = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-host")) {
			if (!opt || !*opt)
				crash("%s requires a host as argument", arg);
			db_host = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-port")) {
			if (!opt || !*opt)
				crash("%s requires a port as argument", arg);
			db_port = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--db-type")) {
			if (!opt || !*opt)
				crash("%s requires a database type as an argument", arg);
			db_type = opt;
			if (opt && !eq_opt)
				i++;
			continue;
		}
		if (!prefixcmp(arg, "--interesting") || !prefixcmp(arg, "-i")) {
			if (!opt || !*opt)
				crash("%s requires a filename as argument", arg);
			hash_interesting(opt);
			if (opt && !eq_opt)
				i++;
			continue;
		}

		/* non-argument, so treat as a config- or log-file */
		arg_len = strlen(arg);
		if (arg_len >= 10 && !strcmp(&arg[arg_len - 10], "nagios.cfg")) {
			nagios_cfg = arg;
		} else {
			add_naglog_path(arg);
		}
	}

	/* fallback for op5 systems */
	if (!nagios_cfg && !num_nfile) {
		nagios_cfg = "/opt/monitor/etc/nagios.cfg";
	}
	if (nagios_cfg) {
		struct cfg_comp *conf;
		uint vi;

		conf = cfg_parse_file(nagios_cfg);
		for (vi = 0; vi < conf->vars; vi++) {
			struct cfg_var *v = conf->vlist[vi];
			if (!strcmp(v->key, "log_file")) {
				add_naglog_path(v->value);
			}
			if (!strcmp(v->key, "log_archive_path")) {
				add_naglog_path(v->value);
			}
		}
	}

	if (!list_files && use_database && (!truncate_db && !incremental)) {
		printf("Defaulting to incremental mode\n");
		incremental = 1;
	}

	if (use_database) {
		db_user = db_user ? db_user : "merlin";
		db_pass = db_pass ? db_pass : "merlin";
		db_type = db_type ? db_type : "mysql";
		sql_config("user", db_user);
		sql_config("pass", db_pass);
		sql_config("type", db_type);
		if (db_conn_str) {
			sql_config("conn_str", db_conn_str);
		} else {
			db_name = db_name ? db_name : "merlin";
			db_table = db_table ? db_table : "report_data";
			db_host = db_host ? db_host : "localhost";
			sql_config("database", db_name);
			sql_config("host", db_host);
			sql_config("port", db_port);
		}

		sql_config("commit_interval", "0");
		sql_config("commit_queries", "10000");

		if (sql_init() < 0) {
			crash("sql_init() failed. db=%s, table=%s, user=%s, db msg=[%s]",
				  db_name, db_table, db_user, sql_error_msg());
		}
		if (truncate_db)
			sql_query("TRUNCATE %s", db_table);

		if (incremental == 1) {
			db_wrap_result * result = NULL;
			sql_query("SELECT MAX(%s) FROM %s.%s",
					  only_notifications ? "end_time" : "timestamp",
					  db_name, db_table);

			if (!(result = sql_get_result()))
				crash("Failed to get last timestamp: %s\n", sql_error_msg());
			/*
			 * someone might use --incremental with an empty
			 * database. We shouldn't crash in that case
			 */
			if (0 == result->api->step(result)) {
				/* reminder: incremental is time_t and may be either uint32_t or uint64.
				 Thus we use an extra int object here to avoid passing an invalid pointer
				 to (&incremental) on platforms where time_t is not uint32_t.
				 */
				int32_t inctime = 0;
				result->api->get_int32_ndx(result, 0, &inctime);
				incremental = inctime;
			}
			sql_free_result();
		}
	}

	log_grok_var("logfile", "/dev/null");
	log_grok_var("log_levels", "warn");

	if (!num_nfile)
		usage("No files or directories specified, or nagios.cfg not found");

	if (log_init() < 0)
		crash("log_init() failed");

	qsort(nfile, num_nfile, sizeof(*nfile), nfile_cmp);

	state_init();

	/* go through them once to count the total size for progress output */
	for (i = 0; i < num_nfile; i++) {
		totsize += nfile[i].size;
	}

	if (!list_files) {
		gettimeofday(&import_start, NULL);
		printf("Importing %s of data from %d files\n",
			   human_bytes(totsize), num_nfile);
	}

	for (i = 0; i < num_nfile; i++) {
		struct naglog_file *nf = &nfile[i];
		cur_file = nf;
		show_progress();

		/*
		 * skip parsing files if they're not interesting, such
		 * as during incremental imports.
		 * 'incremental' will be 0 if we're doing a full import,
		 * 1 if we're doing an incremental but the database is
		 * empty and will contain the timestamp of the latest
		 * entry in the database if we're doing an incremental
		 * import to a populated table.
		 * Note that we can never skip the last file in the list,
		 * although the lparse routine should sift through it
		 * pretty quickly in case it has nothing interesting.
		 */
		if (i + 1 < num_nfile && incremental > nfile[i + 1].first) {
			skipped_files++;
			skipped += nf->size;
			continue;
		}
		if (list_files) {
			printf("%s\n", nf->path);
			continue;
		}
		debug("importing from %s (%lu : %u)\n", nf->path, nf->first, nf->cmp);
		line_no = 0;
		lparse_path(nf->path, nf->size, parse_one_line);
		imported++; /* make up for one lost byte per file */
	}

	ltime = time(NULL);
	end_progress();

	if (use_database) {
		if (!only_notifications)
			insert_extras(); /* must be before indexing */
		enable_indexes();
		sql_close();
	}

	if (warnings && logs_debug_level)
		fprintf(stderr, "Total warnings: %d\n", warnings);

	print_unhandled_events();

	state_deinit();
	return 0;
}
