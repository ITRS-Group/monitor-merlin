#define _GNU_SOURCE 1
#include <sys/types.h>
#include <signal.h>

#include <nagios/broker.h>
#include <nagios/nebcallbacks.h>
#include "shared.h"
#include "sql.h"
#include "state.h"
#include "lparse.h"
#include "logutils.h"
#include "cfgfile.h"
#include <stdint.h> /* standard fixed-size integer types. */
#include <inttypes.h> /* PRIxxx printf specifiers. */
#define IGNORE_LINE 0

#define CONCERNS_HOST 50
#define CONCERNS_SERVICE 60

#define MAX_NVECS 16
#define HASH_TABLE_SIZE 128

/* for some reason these aren't defined inside Nagios' headers */
#define SERVICE_OK 0
#define SERVICE_WARNING 1
#define SERVICE_CRITICAL 2
#define SERVICE_UNKNOWN 3

#define PROGRESS_INTERVAL 25000 /* lines to parse between progress updates */


static const char *progname;
static char *db_table;
static int only_notifications;
static unsigned long long imported, totsize, totlines, skipped;
static int lines_since_progress, do_progress, list_files;
static struct timeval import_start;
static time_t daemon_start, daemon_stop, incremental;
static int daemon_is_running;
static uint max_dt_depth, skipped_files;

static time_t next_dt_purge; /* when next to purge expired downtime */
#define DT_PURGE_GRACETIME 300 /* seconds to add to next_dt_purge */

static time_t ltime; /* the timestamp from the current log-line */

static uint dt_start, dt_stop, dt_skip;
#define dt_depth (dt_start - dt_stop)
static dkhash_table *host_downtime;
static dkhash_table *service_downtime;
static int downtime_id;

struct downtime_entry {
	int id;
	int code;
	char *host;
	char *service;
	time_t start;
	time_t stop;
	int fixed;
	time_t duration;
	time_t started;
	time_t ended;
	int purged;
	int trigger;
	int slot;
	struct downtime_entry *next;
};

#define NUM_DENTRIES 1024
static struct downtime_entry **dentry;
static time_t last_downtime_start;

static struct string_code event_codes[] = {
	add_ignored("Error"),
	add_ignored("Warning"),
	add_ignored("LOG ROTATION"),
	add_ignored("HOST FLAPPING ALERT"),
	add_ignored("SERVICE FLAPPING ALERT"),
	add_ignored("SERVICE EVENT HANDLER"),
	add_ignored("HOST EVENT HANDLER"),
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
		 ds->state_type == HARD_STATE, ds->current_attempt,
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
		 ds->state_type == HARD_STATE, ds->current_attempt,
		 output);
	free(host_name);
	free(service_description);
	free(output);
	return result;
}

static int sql_insert_downtime(nebstruct_downtime_data *ds)
{
	int depth = 0, result;
	char *host_name, *service_description;

	switch (ds->type) {
	case NEBTYPE_DOWNTIME_START:
		/*
		 * If downtime is starting, it will always be at least
		 * 1 deep. Since the report UI doesn't care about the
		 * actual depth but only whether downtime is in effect
		 * or not we can get away with cheating here.
		 */
		depth = 1;
	case NEBTYPE_DOWNTIME_STOP:
		break;
	case NEBTYPE_DOWNTIME_DELETE:
		/*
		 * if we're deleting a downtime that hasn't started yet, nothing
		 * should be added to the database. Otherwise, transform it to a
		 * NEBTYPE_DOWNTIME_STOP event to mark the downtime as stopped.
		 */
		if (ds->start_time > time(NULL))
			return 0;
		ds->type = NEBTYPE_DOWNTIME_STOP;
		break;
	default:
		return 0;
	}

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
			 service_description, depth);
		free(service_description);
	} else {
		result = sql_query
			("INSERT INTO %s("
			 "timestamp, event_type, host_name, downtime_depth)"
			 "VALUES(%lu, %d, %s, %d)",
			 db_table,
			 ds->timestamp.tv_sec, ds->type, host_name, depth);
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
	sql_query("INSERT INTO %s SELECT * FROM report_data_extras", db_table);
}

static void enable_indexes(void)
{
		db_wrap_result * result = NULL;
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

static int insert_downtime_event(int type, char *host, char *service, int id)
{
	nebstruct_downtime_data ds;
	int result;

	if (!is_interesting_service(host, service))
		return 0;

	dt_start += type == NEBTYPE_DOWNTIME_START;
	dt_stop += type == NEBTYPE_DOWNTIME_STOP;
	if (dt_depth > max_dt_depth)
		max_dt_depth = dt_depth;

	if (!use_database || only_notifications)
		return 0;

	memset(&ds, 0, sizeof(ds));

	ds.type = type;
	ds.timestamp.tv_sec = ltime;
	ds.host_name = host;
	ds.service_description = service;
	ds.downtime_id = id;

	disable_indexes();
	result = sql_insert_downtime(&ds);
	if (result < 0)
		lp_crash("Failed to insert downtime:\n  type=%d, host=%s, service=%s, id=%d",
				 type, host, service, id);

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

static void dt_print(char *tpc, time_t when, struct downtime_entry *dt)
{
	if (!debug_level)
		return;

	printf("%s: time=%lu started=%lu start=%lu stop=%lu duration=%lu id=%d ",
		   tpc, when, dt->started, dt->start, dt->stop, dt->duration, dt->id);
	printf("%s", dt->host);
	if (dt->service)
		printf(";%s", dt->service);
	putchar('\n');
}

static struct downtime_entry *last_dte;
static struct downtime_entry *del_dte;

static void remove_downtime(struct downtime_entry *dt);
static int del_matching_dt(void *data)
{
	struct downtime_entry *dt = data;

	if (del_dte->id == dt->id) {
		dt_print("ALSO", 0, dt);
		remove_downtime(dt);
		return DKHASH_WALK_REMOVE;
	}

	return 0;
}

static void stash_downtime_command(struct downtime_entry *dt)
{
	dt->slot = dt->start % NUM_DENTRIES;
	dt->next = dentry[dt->slot];
	dentry[dt->slot] = dt;
}

static void remove_downtime(struct downtime_entry *dt)
{
	if (!is_interesting_service(dt->host, dt->service))
		return;

	insert_downtime_event(NEBTYPE_DOWNTIME_STOP, dt->host, dt->service, dt->id);

	dt_print("RM_DT", ltime, dt);
	dt->purged = 1;
}

static struct downtime_entry *
dt_matches_command(struct downtime_entry *dt, char *host, char *service)
{
	for (; dt; dt = dt->next) {
		time_t diff;

		if (ltime > dt->stop || ltime < dt->start) {
			continue;
		}

		switch (dt->code) {
		case SCHEDULE_SVC_DOWNTIME:
			if (service && strcmp(service, dt->service))
				continue;

			/* fallthrough */
		case SCHEDULE_HOST_DOWNTIME:
		case SCHEDULE_HOST_SVC_DOWNTIME:
			if (strcmp(host, dt->host)) {
				continue;
			}

		case SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME:
		case SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME:
			/* these two have host set in dt, but
			 * it will not match all the possible hosts */

			/* fallthrough */
		case SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
		case SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
		case SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
		case SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
			break;
		default:
			lp_crash("dt->code not set properly\n");
		}

		/*
		 * Once we get here all the various other criteria have
		 * been matched, so we need to check if the daemon was
		 * running when this downtime was supposed to have
		 * started, and otherwise use the daemon start time
		 * as the value to diff against
		 */
		if (daemon_stop < dt->start && daemon_start > dt->start) {
			debug("Adjusting dt->start (%lu) to (%lu)\n",
				  dt->start, daemon_start);
			dt->start = daemon_start;
			if (dt->trigger && dt->duration)
				dt->stop = dt->start + dt->duration;
		}

		diff = ltime - dt->start;
		if (diff < 3 || dt->trigger || !dt->fixed)
			return dt;
	}

	return NULL;
}

static struct downtime_entry *
find_downtime_command(char *host, char *service)
{
	int i;
	struct downtime_entry *shortcut = NULL;

	if (last_dte && last_dte->start == ltime) {
		shortcut = last_dte;
//		return last_dte;
	}
	for (i = 0; i < NUM_DENTRIES; i++) {
		struct downtime_entry *dt;
		dt = dt_matches_command(dentry[i], host, service);
		if (dt) {
			if (shortcut && dt != shortcut)
				if (debug_level)
					printf("FIND shortcut no good\n");
			last_dte = dt;
			return dt;
		}
	}

	debug("FIND not\n");
	return NULL;
}

static int print_downtime(void *data)
{
	struct downtime_entry *dt = (struct downtime_entry *)data;

	dt_print("UNCLOSED", ltime, dt);

	return 0;
}

static inline void set_next_dt_purge(time_t base, time_t add)
{
	if (!next_dt_purge || next_dt_purge > base + add)
		next_dt_purge = base + add;

	if (next_dt_purge <= ltime)
		next_dt_purge = ltime + 1;
}

static inline void add_downtime(char *host, char *service, int id)
{
	struct downtime_entry *dt, *cmd, *old;
	dkhash_table *the_table;

	if (!is_interesting_service(host, service))
		return;

	dt = malloc(sizeof(*dt));
	cmd = find_downtime_command(host, service);
	if (!cmd) {
		warn("DT with no ext cmd? %lu %s;%s", ltime, host, service);
		memset(dt, 0, sizeof(*dt));
		dt->duration = 7200; /* the default downtime duration in nagios */
		dt->start = ltime;
		dt->stop = dt->start + dt->duration;
	}
	else
		memcpy(dt, cmd, sizeof(*dt));

	dt->host = strdup(host);
	dt->id = id;
	dt->started = ltime;

	set_next_dt_purge(ltime, dt->duration);

	if (service) {
		dt->service = strdup(service);
		the_table = service_downtime;
	}
	else {
		dt->service = NULL;
		the_table = host_downtime;
	}

	old = dkhash_get(the_table, dt->host, dt->service);
	if (old) {
		dkhash_remove(the_table, old->host, old->service);
		free(old->host);
		if (old->service)
			free(old->service);
		free(old);
	}
	dkhash_insert(the_table, dt->host, dt->service, dt);

	dt_print("IN_DT", ltime, dt);
	insert_downtime_event(NEBTYPE_DOWNTIME_START, dt->host, dt->service, dt->id);
}

static time_t last_host_dt_del, last_svc_dt_del;
static int register_downtime_command(struct string_code *sc, int nvecs)
{
	struct downtime_entry *dt;
	char *start_time, *end_time, *duration = NULL;
	char *host = NULL, *service = NULL, *fixed, *triggered_by = NULL;
	time_t foo;

	/*
	 * this could cause crashes if we let it go on, so
	 * bail early if we didn't parse enough fields from
	 * the file.
	 */
	if (nvecs < sc->nvecs) {
		return -1;
	}

	switch (sc->code) {
	case DEL_HOST_DOWNTIME:
		last_host_dt_del = ltime;
		return 0;
	case DEL_SVC_DOWNTIME:
		last_svc_dt_del = ltime;
		return 0;

	case SCHEDULE_HOST_DOWNTIME:
		if (strtotimet(strv[5], &foo))
			duration = strv[4];
		/* fallthrough */
	case SCHEDULE_AND_PROPAGATE_HOST_DOWNTIME:
	case SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME:
	case SCHEDULE_HOST_SVC_DOWNTIME:
		host = strv[0];
		/* fallthrough */
	case SCHEDULE_HOSTGROUP_HOST_DOWNTIME:
	case SCHEDULE_HOSTGROUP_SVC_DOWNTIME:
	case SCHEDULE_SERVICEGROUP_HOST_DOWNTIME:
	case SCHEDULE_SERVICEGROUP_SVC_DOWNTIME:
		start_time = strv[1];
		end_time = strv[2];
		fixed = strv[3];
		if (strtotimet(strv[5], &foo))
			triggered_by = strv[4];
		if (!duration)
			duration = strv[5];

		break;

	case SCHEDULE_SVC_DOWNTIME:
		host = strv[0];
		service = strv[1];
		start_time = strv[2];
		end_time = strv[3];
		fixed = strv[4];
		if (strtotimet(strv[6], &foo)) {
			triggered_by = strv[5];
			duration = strv[6];
		}
		else {
			duration = strv[5];
		}
		break;

	default:
		lp_crash("Unknown downtime type: %d", sc->code);
	}

	if (!(dt = calloc(sizeof(*dt), 1)))
		lp_crash("calloc(%u, 1) failed: %s", (uint)sizeof(*dt), strerror(errno));

	dt->code = sc->code;
	if (host)
		dt->host = strdup(host);
	if (service)
		dt->service = strdup(service);

	dt->trigger = triggered_by ? !!(*triggered_by - '0') : 0;
	dt->start = dt->stop = 0;
	strtotimet(start_time, &dt->start);
	strtotimet(end_time, &dt->stop);

	/*
	 * if neither of these is set, we can't use this command,
	 * so log it as an unknown event and move on. We really
	 * shouldn't crash here no matter what anyways.
	 */
	if (!dt->start && !dt->stop) {
		devectorize_string(strv, nvecs);
		warn("No dt->start or dt->stop in: %s", strv[0]);
		return -1;
	}

	/*
	 * sometimes downtime commands can be logged according to
	 * log version 1, while the log still claims to be version 2.
	 * Apparently, this happens when using a daemon supporting
	 * version 2 logging but a downtime command is added that
	 * follows the version 1 standard.
	 * As such, we simply ignore the result of the "duration"
	 * field conversion and just accept that it might not work.
	 * If it doesn't, we force-set it to 7200, since that's what
	 * Nagios uses as a default, and we'll need two of duration,
	 * start_time and end_time in order to make some sense of
	 * this downtime entry
	 */
	if (strtotimet(duration, &dt->duration) < 0)
		dt->duration = 7200;
	dt->fixed = *fixed - '0';

	/*
	 * we know we have a duration and at least one of stop
	 * and start. Calculate the other if one is missing.
	 */
	if (!dt->stop) {
		dt->stop = dt->start + dt->duration;
	} else if (!dt->start) {
		dt->start = dt->stop - dt->duration;
	}

	/* make sure we're not starting timeperiod in the past */
	if (dt->start < ltime) {
		dt->start = ltime;
		if (dt->stop <= dt->start)
			return 0;

		/* if fixed, we alter duration. Otherwise we alter 'stop' */
		if (dt->fixed == 1)
			dt->duration = dt->stop - dt->start;
		else
			dt->stop = dt->start + dt->duration;
	}

	/*
	 * ignore downtime scheduled to take place in the future.
	 * It will be picked up by the module anyways
	 */
	if (dt->start > time(NULL)) {
		free(dt);
		return 0;
	}

	if (dt->duration > time(NULL)) {
		warn("Bizarrely large duration (%lu)", dt->duration);
	}
	if (dt->start < ltime) {
		if (dt->duration && dt->duration > ltime - dt->start)
			dt->duration -= ltime - dt->start;

		dt->start = ltime;
	}
	if (dt->stop < ltime || dt->stop < dt->start)  {
		/* retroactively scheduled downtime, or just plain wrong */
		dt->stop = dt->start;
		dt->duration = 0;
	}

	if (dt->fixed && dt->duration != dt->stop - dt->start) {
//		warn("duration doesn't match stop - start: (%lu : %lu)",
//			 dt->duration, dt->stop - dt->start);

		dt->duration = dt->stop - dt->start;
	}
	else if (dt->duration > 86400 * 14) {
		warn("Oddly long duration: %lu", dt->duration);
	}

	debug("start=%lu; stop=%lu; duration=%lu; fixed=%d; trigger=%d; host=%s service=%s\n",
		  dt->start, dt->stop, dt->duration, dt->fixed, dt->trigger, dt->host, dt->service);

	stash_downtime_command(dt);
	return 0;
}

static int insert_downtime(struct string_code *sc)
{
	int type;
	struct downtime_entry *dt = NULL;
	int id = 0;
	time_t dt_del_cmd;
	char *host, *service = NULL;

	host = strv[0];
	if (sc->nvecs == 4) {
		service = strv[1];
		dt = dkhash_get(service_downtime, host, service);
	}
	else {
		dt = dkhash_get(host_downtime, host, NULL);
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
		if (ltime - last_downtime_start > 1)
			downtime_id++;

		id = downtime_id;
		add_downtime(host, service, id);
		last_downtime_start = ltime;
		break;

	case NEBTYPE_DOWNTIME_STOP:
		if (!dt) {
			/*
			 * this can happen when overlapping downtime entries
			 * occur, and the start event for the second (or nth)
			 * downtime starts before the first downtime has had
			 * a stop event. It basically means we've almost
			 * certainly done something wrong.
			 */
			//printf("no dt. ds.host_name == '%s'\n", ds.host_name);
			//fprintf(stderr, "CRASHING: %s;%s\n", ds.host_name, ds.service_description);
			//crash("DOWNTIME_STOP without matching DOWNTIME_START");
			dt_skip++;
			return 0;
		}

		dt_del_cmd = !dt->service ? last_host_dt_del : last_svc_dt_del;

		if ((ltime - dt_del_cmd) > 1 && dt->duration - (ltime - dt->started) > 60) {
			debug("Short dt duration (%lu) for %s;%s (dt->duration=%lu)\n",
				   ltime - dt->started, dt->host, dt->service, dt->duration);
		}
		if (ltime - dt->started > dt->duration + DT_PURGE_GRACETIME)
			dt_print("Long", ltime, dt);

		remove_downtime(dt);
		/*
		 * Now delete whatever matching downtimes we can find.
		 * this must be here, or we'll recurse like crazy into
		 * remove_downtime(), possibly exhausting the stack
		 * frame buffer
		 */
		del_dte = dt;
		if (!dt->service)
			dkhash_walk_data(host_downtime, del_matching_dt);
		else
			dkhash_walk_data(service_downtime, del_matching_dt);
		break;

	default:
		return -1;
	}

	return 0;
}

static int dt_purged;
static int purge_expired_dt(void *data)
{
	struct downtime_entry *dt = data;

	if (dt->purged) {
		dt_skip++;
		return 0;
	}

	set_next_dt_purge(dt->started, dt->duration);

	if (ltime + DT_PURGE_GRACETIME > dt->stop) {
		dt_purged++;
		debug("PURGE %lu: purging expired dt %d (start=%lu; started=%lu; stop=%lu; duration=%lu; host=%s; service=%s",
			  ltime, dt->id, dt->start, dt->started, dt->stop, dt->duration, dt->host, dt->service);
		remove_downtime(dt);
		return DKHASH_WALK_REMOVE;
	}
	else {
		dt_print("PURGED_NOT_TIME", ltime, dt);
	}

	return 0;
}

static int purged_downtimes;
static void purge_expired_downtime(void)
{
	int tot_purged = 0;

	next_dt_purge = 0;
	dt_purged = 0;
	dkhash_walk_data(host_downtime, purge_expired_dt);
	if (dt_purged)
		debug("PURGE %d host downtimes purged", dt_purged);
	tot_purged += dt_purged;
	dt_purged = 0;
	dkhash_walk_data(service_downtime, purge_expired_dt);
	if (dt_purged)
		debug("PURGE %d service downtimes purged", dt_purged);
	tot_purged += dt_purged;
	if (tot_purged)
		debug("PURGE total %d entries purged", tot_purged);

	if (next_dt_purge)
		debug("PURGE next downtime purge supposed to run @ %lu, in %lu seconds",
			  next_dt_purge, next_dt_purge - ltime);

	purged_downtimes += tot_purged;
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
	int nvecs = 0;
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

	if (next_dt_purge && ltime >= next_dt_purge)
		purge_expired_downtime();

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
		if (sc->code != ACKNOWLEDGE_HOST_PROBLEM &&
			sc->code != ACKNOWLEDGE_SVC_PROBLEM)
		{
			register_downtime_command(sc, nvecs);
		} else {
			insert_acknowledgement(sc);
		}
		break;

	case NEBTYPE_HOSTCHECK_PROCESSED:
		return insert_host_check(sc);

	case NEBTYPE_SERVICECHECK_PROCESSED:
		return insert_service_check(sc);

	case NEBTYPE_DOWNTIME_LOAD + CONCERNS_HOST:
	case NEBTYPE_DOWNTIME_LOAD + CONCERNS_SERVICE:
		return insert_downtime(sc);

	case NEBTYPE_NOTIFICATION_END + CONCERNS_HOST:
	case NEBTYPE_NOTIFICATION_END + CONCERNS_SERVICE:
		return insert_notification(sc);

	case IGNORE_LINE:
		return 0;
	}

	return 0;
}

static int parse_one_line(char *str, uint len)
{
	const char *msg;

	if (parse_line(str, len) && use_database && sql_error(&msg))
		lp_crash("sql error: %s", msg);

	return 0;
}

static int hash_one_line(char *line, uint len)
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
	dentry = calloc(sizeof(*dentry), NUM_DENTRIES);
	if (!strv || !dentry)
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
			debug_level++;
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

	host_downtime = dkhash_create(HASH_TABLE_SIZE);
	service_downtime = dkhash_create(HASH_TABLE_SIZE);

	if (state_init() < 0)
		crash("Failed to initialize state machinery");

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
	purge_expired_downtime();
	end_progress();

	if (debug_level) {
		if (dt_depth) {
			printf("Unclosed host downtimes:\n");
			puts("------------------------");
			dkhash_walk_data(host_downtime, print_downtime);
			printf("Unclosed service downtimes:\n");
			puts("---------------------------");
			dkhash_walk_data(service_downtime, print_downtime);

			printf("dt_depth: %d\n", dt_depth);
		}
		printf("purged downtimes: %d\n", purged_downtimes);
		printf("max simultaneous host downtime hashes: %u\n",
		       dkhash_num_entries_max(host_downtime));
		printf("max simultaneous service downtime hashes: %u\n",
		       dkhash_num_entries_max(service_downtime));
		printf("max downtime depth: %u\n", max_dt_depth);
	}

	if (use_database) {
		if (!only_notifications)
			insert_extras(); /* must be before indexing */
		enable_indexes();
		sql_close();
	}

	if (warnings && debug_level)
		fprintf(stderr, "Total warnings: %d\n", warnings);

	if (debug_level || dt_start > dt_stop) {
		uint count;
		fprintf(stderr, "Downtime data %s\n  started: %d\n  stopped: %d\n  delta  : %d\n  skipped: %d\n",
		        dt_depth ? "mismatch!" : "consistent", dt_start, dt_stop, dt_depth, dt_skip);
		if ((count = dkhash_num_entries(host_downtime))) {
			fprintf(stderr, "host_downtime as %u entries remaining\n", count);
		}
		if ((count = dkhash_num_entries(service_downtime))) {
			fprintf(stderr, "service_downtime has %u entries remaining\n", count);
		}
	}

	print_unhandled_events();

	return 0;
}
