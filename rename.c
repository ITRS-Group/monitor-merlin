#include "sql.h"
#include "logutils.h"
#include "lparse.h"
#include <nagios/defaults.h>
#include <string.h>
#include <stdio.h>

#ifdef __GLIBC__
#include <malloc.h>
#endif

typedef struct renames {
	int id;
	char *from_host_name;
	char *from_service_description;
	char *to_host_name;
	char *to_service_description;
} renames;

static FILE *new_file;

static int64_t rename_len;
static char **from_name;
static char **to_name;

#define previous_is(str, val) \
	(!memcmp(str - strlen(val), val, strlen(val)))

static int
rename_db(renames *renames)
{
	int i;
	for (i = 0; i < rename_len; i++) {
		char *quoted_from_host, *quoted_to_host;
		sql_quote(renames[i].from_host_name, &quoted_from_host);
		sql_quote(renames[i].to_host_name, &quoted_to_host);
		if (renames[i].from_service_description) {
			char *quoted_from_service, *quoted_to_service;
			sql_quote(renames[i].from_service_description, &quoted_from_service);
			sql_quote(renames[i].to_service_description, &quoted_to_service);
			sql_query("UPDATE report_data SET host_name=%s, service_description=%s "
			          "WHERE host_name=%s AND service_description=%s",
			          quoted_to_host, quoted_to_service,
			          quoted_from_host, quoted_from_service);
			if (sql_table_exists("report_data_synergy") > 0) {
				sql_query("UPDATE report_data_synergy SET host_name=%s, service_description=%s "
						          "WHERE host_name=%s AND service_description=%s",
						          quoted_to_host, quoted_to_service,
						          quoted_from_host, quoted_from_service);
			}
			safe_free(quoted_from_service);
			safe_free(quoted_to_service);
		}
		else {
			sql_query("UPDATE report_data SET host_name=%s "
			          "WHERE host_name=%s",
			          quoted_to_host, quoted_from_host);
			if (sql_table_exists("report_data_synergy") > 0) {
				sql_query("UPDATE report_data_synergy SET host_name=%s "
				          "WHERE host_name=%s",
				          quoted_to_host, quoted_from_host);
			}
		}
		safe_free(quoted_from_host);
		safe_free(quoted_to_host);
	}
	sql_try_commit(-1);
	return 0;
}

/**
 * Let's ignore random log messages such as "Warning: Host 'foo' has no services associated with it"
 * There are, as far as I can see, two formats of log messages left:
 * external commands/notifications:
 *   (NOTIFICATION|COMMAND): [^;]+;host_name(;service_description)?
 * alerts/event handlers/initial state:
 *   (STATE|HANDLER|ALERT): host_name(;service_description)?
 */
static int
parse_line(char *str, unsigned int len)
{
	char *res_str = NULL;
	char *where_to_look;
	if (len <= 13) {
		fwrite(str, len, 1, new_file);
		fwrite("\n", 1, 1, new_file);
		return 0;
	}
	// we can skip the first 13 characters, as that is just timestamps.
	// as the longest event type we look for is shorter than 13 characters,
	// this not only makes us faster, but also makes it safe using previous_is
	where_to_look = str + 13;
	where_to_look = strchr(where_to_look, ':');
	if (!where_to_look) {
		fwrite(str, len, 1, new_file);
		fwrite("\n", 1, 1, new_file);
		return 0;
	}

	if (previous_is(where_to_look, "ALERT") || previous_is(where_to_look, "HANDLER") || previous_is(where_to_look, "STATE")) {
		int i;
		for(i = 0; i < rename_len; i++) {
			if (!strncmp(from_name[i], where_to_look + 2, strlen(from_name[i]))) {
				res_str = calloc(1, strlen(str) + strlen(to_name[i]) - strlen(from_name[i]) + 2);
				sprintf(res_str, "%.*s%s%s\n", ((int)(where_to_look - str)) + 2, str, to_name[i], where_to_look + strlen(from_name[i]) + 2);
				break;
			}
		}
	}
	else if (previous_is(where_to_look, "NOTIFICATION") || previous_is(where_to_look, "COMMAND")) {
		int i;
		where_to_look = strchr(where_to_look, ';');
		for (i = 0; i < rename_len; i++) {
			if (!strncmp(from_name[i], where_to_look + 1, strlen(from_name[i]))) {
				res_str = calloc(1, strlen(str) + strlen(to_name[i]) - strlen(from_name[i]) + 2);
				sprintf(res_str, "%.*s%s%s\n", (int)(where_to_look - str) + 1, str, to_name[i], where_to_look + strlen(from_name[i]) + 1);
				break;
			}
		}
	}
	if (res_str != NULL) {
		fwrite(res_str, strlen(res_str), 1, new_file);
		safe_free(res_str);
	} else {
		fwrite(str, len, 1, new_file);
		fwrite("\n", 1, 1, new_file);
	}

	return 0;
}

static int
rename_log(renames *renames, char *log_dir, char *log_file)
{
	int i;
	if (log_dir)
		add_naglog_path(log_dir);
	if (log_file)
		add_naglog_path(log_file);
	for(i = 0; i < num_nfile; i++) {
		char new_path[512];
		struct naglog_file *nf = &nfile[i];
		sprintf(new_path, "%s.new", nf->path);
		linfo("Renaming in %s", nf->path);
		new_file = fopen(new_path, "wb");
		lparse_path(nf->path, nf->size, parse_line);
		fclose(new_file);
		rename(new_path, nf->path);
	}
	return 0;
}

static int
find_renames(renames **rename_ptr)
{
	int i;
	db_wrap_result *result;
	renames *renames;

	rename_len = 0;

	sql_query("SELECT COUNT(1) FROM rename_log");
	result = sql_get_result();
	if (!result) {
		return 1;
	} else {
		if (result->api->step(result) == 0) {
			result->api->get_int64_ndx(result, 0, &rename_len);
		} else {
			return 1;
		}
		sql_free_result();
	}

	sql_query("SELECT id, from_host_name, from_service_description, to_host_name, to_service_description FROM rename_log ORDER BY id ASC");
	result = sql_get_result();
	if (!result)
		return 1;

	*rename_ptr = calloc(1, sizeof(**rename_ptr) * rename_len);
	renames = *rename_ptr;
	i = 0;
	while (!result->api->step(result)) {
		const char *tmp_str;
		size_t tmp_len;
		result->api->get_int32_ndx(result, 0, &renames[i].id);
		result->api->get_string_ndx(result, 1, &tmp_str, &tmp_len);
		renames[i].from_host_name = calloc(1, tmp_len + 1);
		strncat(renames[i].from_host_name, tmp_str, tmp_len);
		result->api->get_string_ndx(result, 2, &tmp_str, &tmp_len);
		if (tmp_len) {
			renames[i].from_service_description = calloc(1, tmp_len + 1);
			strncat(renames[i].from_service_description, tmp_str, tmp_len);
		}
		result->api->get_string_ndx(result, 3, &tmp_str, &tmp_len);
		renames[i].to_host_name = calloc(1, tmp_len + 1);
		strncat(renames[i].to_host_name, tmp_str, tmp_len);
		result->api->get_string_ndx(result, 4, &tmp_str, &tmp_len);
		if (tmp_len) {
			renames[i].to_service_description = calloc(1, tmp_len + 1);
			strncat(renames[i].to_service_description, tmp_str, tmp_len);
		}
		i++;
	}
	sql_free_result();

	from_name = malloc(sizeof(*from_name) * rename_len);
	to_name = malloc(sizeof(*to_name) * rename_len);
	for (i = 0; i < rename_len; i++) {
		if (renames[i].from_service_description) {
			from_name[i] = calloc(1, strlen(renames[i].from_host_name) + strlen(renames[i].from_service_description) + 2);
			to_name[i] = calloc(1, strlen(renames[i].to_host_name) + strlen(renames[i].to_service_description) + 2);
			sprintf(from_name[i], "%s;%s", renames[i].from_host_name, renames[i].from_service_description);
			sprintf(to_name[i], "%s;%s", renames[i].to_host_name, renames[i].to_service_description);
		}
		else {
			from_name[i] = strdup(renames[i].from_host_name);
			to_name[i] = strdup(renames[i].to_host_name);
		}
	}
	return 0;
}

static int
clear_renames(renames *renames)
{
	int i;
	char *buf, *tmp;

	if (rename_len <= 0)
		return 0;

	// how long is a number? let's overallocate wildly.
	tmp = buf = calloc(1, rename_len * 20);
	for (i = 0; i < rename_len; i++) {
		int new = sprintf(tmp, ",%i", renames[i].id);
		tmp += new;
	}
	sql_query("DELETE FROM rename_log WHERE id IN (%s)", ++buf);
	safe_free(--buf);
	return 0;
}

static void
usage(char *name)
{
	printf("Usage: %s [options]\n\n", name);
	printf("Fetch a list of recent host and service renames, and update\n"
	       "log files and database tables.\n");
	printf("Remember: you should shut down monitor while fixing logs!\n\n"
	       "Options:\n");
	printf("  --rename-all      Rename in database, and in current and archived log files\n"
	       "  --rename-archived Rename in all archived log files\n"
	       "  --rename-log      Rename current log file\n"
	       "  --rename-db       Rename everything in database\n"
	       "  --save-renames    Do the same renames on next execution\n");
	printf("  --log-archive     Use the given directory for parsing archived logs. Default:\n"
	       "                    %s\n", DEFAULT_LOG_ARCHIVE_PATH);
	printf("  --log-file        Parse the given log file. Default:\n"
	       "                    %s\n", DEFAULT_LOG_FILE);
	printf("  --db-type         Database type\n"
	       "  --db-host         Database host\n"
	       "  --db-name         Database name\n"
	       "  --db-user         Database username\n"
	       "  --db-pass         Database password\n"
	       "  --help            Show this text and exit\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	int errs = 0;
	int i;
	int do_rename_log = 0, do_rename_db = 0, do_rename_archived = 0;
	int save_renames = 0;
	char *log_dir = NULL, *log_file = NULL;
	char *db_type = NULL, *db_name = NULL, *db_user = NULL, *db_pass = NULL, *db_host = NULL;
	renames *renames = NULL;
	struct timeval start, stop;

	gettimeofday(&start, NULL);

	log_grok_var("log_level", "all");
	log_grok_var("log_file", "stdout");
	log_init();

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--help")) {
			usage(argv[0]);
		}
		else if (!strcmp(argv[i], "--rename-all")) {
			do_rename_log = 1;
			do_rename_archived = 1;
			do_rename_db = 1;
		}
		else if (!strcmp(argv[i], "--rename-log")) {
			do_rename_log = 1;
		}
		else if (!strcmp(argv[i], "--rename-archived")) {
			do_rename_archived = 1;
		}
		else if (!strcmp(argv[i], "--rename-db")) {
			do_rename_db = 1;
		}
		else if (!strcmp(argv[i], "--save-renames")) {
			save_renames = 1;
		}
		else if (!prefixcmp(argv[i], "--db-type=")) {
			db_type = strdup(argv[i] + strlen("--db-type="));
		}
		else if (!prefixcmp(argv[i], "--db-name=")) {
			db_name = strdup(argv[i] + strlen("--db-name="));
		}
		else if (!prefixcmp(argv[i], "--db-host=")) {
			db_host = strdup(argv[i] + strlen("--db-host="));
		}
		else if (!prefixcmp(argv[i], "--db-user=")) {
			db_user = strdup(argv[i] + strlen("--db-user="));
		}
		else if (!prefixcmp(argv[i], "--db-pass=")) {
			db_pass = strdup(argv[i] + strlen("--db-pass="));
		}
		else if (!prefixcmp(argv[i], "--log-dir=")) {
			log_dir = strdup(argv[i] + strlen("--log-dir="));
		}
		else if (!prefixcmp(argv[i], "--log-file=")) {
			log_file = strdup(argv[i] + strlen("--log-file="));
		}
		else {
			printf("Unknown argument: %s\n", argv[i]);
			usage(argv[0]);
		}
	}
	if (!do_rename_db && !do_rename_log && !do_rename_archived)
		usage(argv[0]);

	if (log_dir == NULL)
		log_dir = strdup(DEFAULT_LOG_ARCHIVE_PATH);
	if (log_file == NULL)
		log_file = strdup(DEFAULT_LOG_FILE);

	use_database = 1;
	if (db_user)
		sql_config("user", db_user);
	if (db_pass)
		sql_config("pass", db_pass);
	if (db_name)
		sql_config("database", db_name);
	if (db_host)
		sql_config("host", db_host);
	if (db_type)
		sql_config("type", db_type);

	sql_config("commit_interval", "0");
	sql_config("commit_queries", "10000");

	if (sql_init()) {
		lerr("Couldn't connect to database. Aborting.");
		exit(1);
	}

	linfo("Getting objects to rename.");
	errs = find_renames(&renames);
	if (errs) {
		lerr("There was an error. Aborting.");
		exit(1);
	}
	linfo("Found %ld renames.", rename_len);
	if (!rename_len) {
		linfo("Nothing to do. Exiting.");
		exit(0);
	}

	if (!errs && do_rename_db) {
		linfo("Renaming database entries...");
		errs += rename_db(renames);
	}
	if (!errs && (do_rename_log || do_rename_archived)) {
		linfo("Renaming logs...");
		errs += rename_log(renames, (do_rename_archived ? log_dir : NULL), (do_rename_log ? log_file : NULL));
	}

	if (!errs && !save_renames) {
		linfo("Deleting rename backlog.");
		errs += clear_renames(renames);
	}
	sql_try_commit(-1);
	sql_close();
	if (!errs)
		linfo("Done.");
	gettimeofday(&stop, NULL);
	linfo("Rename completed in %s.", tv_delta(&start, &stop));
#ifdef __GLIBC__
	malloc_stats();
#endif
	safe_free(log_dir);
	safe_free(log_file);
	safe_free(renames);
	return errs;
}
