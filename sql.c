#define _GNU_SOURCE
#include "daemon.h"

#include <stdio.h> /* debuggering only. */

/* where to (optionally) stash performance data */
char *host_perf_table = NULL;
char *service_perf_table = NULL;
static long int commit_interval, commit_queries;
static time_t last_commit;
unsigned long total_queries = 0;
static int db_type;

#define MERLIN_DBT_MYSQL 0
#define MERLIN_DBT_ORACLE 1
#define MERLIN_DBT_PGSQL 2
#define MERLIN_DBT_SQLITE 3

/*
 * File-scoped definition of the database settings we've tried
 * (or succeeded) connecting with
 */
static struct {
	char const *host;
	char const *name;
	char const *user;
	char const *pass;
	char const *table;
	char const *type;
	char const *encoding;
	int port; /* signed int for compatibility with dbi_conn_set_option_numeric() (and similar)*/
	db_wrap *conn;
	db_wrap_result * result;
	int logSQL;
} db = {
NULL/*host*/,
NULL/*name*/,
NULL/*user*/,
NULL/*pass*/,
NULL/*table*/,
NULL/*type*/,
NULL/*encoding*/,
0U/*port*/,
NULL/*conn*/,
NULL/*result*/,
0/*logSQL*/
};



/*
 * Quotes a string and escapes all meta-characters inside the string.
 * If src is NULL or !*src then 0 is returned and *dest is not modified.
 * *dst must be free()'d by the caller.
 */
size_t sql_quote(const char *src, char **dst)
{
	size_t ret;

	if (!sql_is_connected(1)) {
		*dst = NULL;
		return 0;
	}

	assert(db.conn != NULL);
	ret = db.conn->api->sql_quote(db.conn, src, (src?strlen(src):0U), dst);
	if (!ret) {
		*dst = NULL;
	}

	return ret;
}

/*
 * these two functions are only here to allow callers
 * access to error reporting without having to expose
 * the db-link to theh callers. It's also nifty as we
 * want to remain database layer agnostic.
 *
 * The returned bytes are owned by the underlying DB driver and are
 * not guaranteed to be valid after the next call into the db
 * driver. It is up to the client to copy them, if needed, BEFORE
 * making ANY other calls into the DB API.
 */
int sql_error(const char **msg)
{
	int dbrc = 0;

	if (!db.conn) {
		*msg = "no database connection";
		return DB_WRAP_E_UNKNOWN_ERROR;
	}

	db.conn->api->error_info(db.conn, msg, NULL, &dbrc);
	return dbrc;
}

/**
 * Convenience form of sql_error() which returns the error
 * string directly, or returns an unspecified string if
 * no connection is established.
 */
const char *sql_error_msg(void)
{
	const char *msg = NULL;
	sql_error(&msg);
	return msg;
}

void sql_free_result(void)
{
	if (db.result) {
		db.result->api->finalize(db.result);
		db.result = NULL;
	}
}

db_wrap_result * sql_get_result(void)
{
	return db.result;
}

void sql_try_commit(int query)
{
	static int queries;
	time_t now = time(NULL);

	if (!db.conn || !use_database || !db.conn->api->commit)
		return;

	if (query > 0)
		queries += query;

	if (queries &&
	    (query == -1 ||
	     (commit_interval && last_commit + commit_interval <= now) ||
	     (commit_queries && queries >= commit_queries)
	    )
	   )
	{
		int result;

		ldebug("Committing %d queries", queries);
		result = db.conn->api->commit(db.conn);
		last_commit = now;
		total_queries += queries;
		queries = 0;
	}
}

static int run_query(char *query, size_t len, int rerunIGNORED)
{
	db_wrap_result *res = NULL;
	int rc;

	if (!db.conn && sql_init() < 0) {
		lerr("DB: No connection. Skipping query [%s]\n", query);
		return -1;
	}

	assert(db.conn != NULL);
	rc = db.conn->api->query_result(db.conn, query, len, &res);

	if (db.logSQL) {
		ldebug("MERLIN SQL: [%s]\n\tResult code: %d, result object @%p\n", query, rc, res);
		if (rc) {
			ldebug("Error code: %d\n", rc);
			/*
			 * Reminder: the OCI driver adds a newline of its own to
			 * error text.
			*/
		}
	}

	if (rc) {
		assert(NULL == res);
		return rc;
	}

	sql_try_commit(1);

	assert(res != NULL);
	assert(db.result == NULL);
	db.result = res;
	return rc;
}

int sql_vquery(const char *fmt, va_list ap)
{
	int len;
	char *query;

	if (!fmt)
		return 0;

	if (!use_database) {
		return -1;
	}

	/*
	 * don't even bother trying to run the query if the database
	 * isn't online and we recently tried to connect to it
	 */
	if (!sql_is_connected(1)) {
		ldebug("DB: Not connected and re-init failed. Skipping query");
		return -1;
	}

	/* free any leftover result and run the new query */
	sql_free_result();

	len = vasprintf(&query, fmt, ap);
	if (len == -1 || !query) {
		lerr("sql_query: Failed to build query from format-string '%s'", fmt);
		return -1;
	}

	if (run_query(query, len, 0) != 0) {
		const char *error_msg;
		int db_error = sql_error(&error_msg);
		int reconnect = 0;

		lerr("Failed to run query [%s] due to error-code %d: %s",
		     query, db_error, error_msg);
#ifdef ENABLE_LIBDBI
		if (db_type == MERLIN_DBT_MYSQL) {
			/*
			 * if we failed because the connection has gone away, we try
			 * reconnecting once and rerunning the query before giving up.
			 * Otherwise we just ignore it and go on
			 */
			switch (db_error) {
			case 1062: /* duplicate key */
			case 1068: /* duplicate primary key */
			case 1146: /* table missing */
			case 2029: /* null pointer */
				break;

			default:
				reconnect = 1;
				break;
			}
		}
#endif
#ifdef ENABLE_OCILIB
		if (db_type == MERLIN_DBT_ORACLE) {
			reconnect = 0;
		}
#endif
		if (reconnect) {
			lwarn("Attempting to reconnect to database and re-run the query");
			if (!sql_reinit()) {
				if (!run_query(query, len, 1))
					lwarn("Successfully ran the previously failed query");
				/* database backlog code goes here */
			}
		}
	}

	free(query);

	return !db.result;
}

int sql_query(const char *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = sql_vquery(fmt, ap);
	va_end(ap);

	return ret;
}

int sql_is_connected(int reconnect)
{
	int ret = (db.conn && db.conn->api->is_connected(db.conn)) ? 1 : 0;

	if (ret || !reconnect)
		return ret;

	return sql_init() == 0;
}

int sql_init(void)
{
	const char *env;
	int result, log_attempt = 0;
	static time_t last_logged = 0;

	if (!use_database)
		return -1;

	if (sql_is_connected(0)) {
		ldebug("sql_init(): Already connected. Not reconnecting");
		return 0;
	}

	if (last_logged + 30 >= time(NULL))
		log_attempt = 0;
	else {
		log_attempt = 1;
		last_logged = time(NULL);
	}

	env = getenv("MERLIN_LOG_SQL");
	if (env && *env != 0) {
		db.logSQL = 1;
	}

	/* free any remaining result set */
	sql_free_result();

	db.name = sql_db_name();
	db.host = sql_db_host();
	db.user = sql_db_user();
	db.pass = sql_db_pass();
	db.table = sql_table_name();
	if (!db.type) {
		db.type = "mysql";
	}

	if (!strcmp(db.type, "mysql") || !strcmp(db.type, "dbi:mysql")) {
		db_type = MERLIN_DBT_MYSQL;
	} else if (!strcmp(db.type, "oci") || !strcmp(db.type, "oracle") || !strcmp(db.type, "ocilib")) {
		db_type = MERLIN_DBT_ORACLE;
	} else if (!strcmp(db.type, "psql") || !strcmp(db.type, "postgresql") || !strcmp(db.type, "pgsql")) {
		db_type = MERLIN_DBT_PGSQL;
	} else if (!strcmp(db.type, "sqlite")) {
		db_type = MERLIN_DBT_SQLITE;
	}

	db_wrap_conn_params connparam = db_wrap_conn_params_empty;
	connparam.host = db.host;
	connparam.dbname = db.name;
	connparam.username = db.user;
	connparam.password = db.pass;
	if (db.port)
		connparam.port = db.port;

	result = db_wrap_driver_init(db.type, &connparam, &db.conn);
	if (result) {
		if (log_attempt) {
			lerr("Failed to connect to db '%s' at host '%s':'%d' as user %s using driver %s.",
				 db.name, db.host, db.port, db.user, db.type );
			lerr("result: %d; errno: %d (%s)", result, errno, strerror(errno));
		}
		return -1;
	}

	result = db.conn->api->option_set(db.conn, "encoding", db.encoding ? db.encoding : "latin1");

	if (result && log_attempt) {
		lwarn("Warning: Failed to set one or more database connection options");
	}

	result = db.conn->api->connect(db.conn);
	if (result) {
		if (log_attempt) {
			const char *error_msg;
			sql_error(&error_msg);
			lerr("DB: Failed to connect to '%s' at '%s':'%d' as %s:%s: %s",
				 db.name, db.host, db.port, db.user, db.pass, error_msg);
		}
		sql_close();
		return -1;
	} else if (log_attempt) {
		ldebug("DB: Connected to db [%s] using driver [%s]",
			   db.name, db.type);
	}

	/*
	 * set auto-commit to ON if we have no commit parameters
	 * Drivers that doesn't support it or doesn't need it shouldn't
	 * have the "set_auto_commit()" function.
	 */
	if (db.conn->api->set_auto_commit) {
		int set = !(commit_interval | commit_queries);

		if (db.conn->api->set_auto_commit(db.conn, set) < 0) {
			if (set) {
				/* fake auto-commit with commit_queries = 1 */
				commit_queries = 1;
			}
			if (log_attempt) {
				lwarn("DB: set_auto_commit(%d) failed.", set);
				if (set) {
					lwarn("DB: setting commit_interval = 1 as workaround");
				}
			}
		} else if (log_attempt) {
			ldebug("DB: commit_queries: %ld; commit_interval: %ld",
				  commit_queries, commit_interval);
		}
		last_commit = time(NULL);
	}

	last_logged = 0;
	return 0;
}


int sql_close(void)
{
	if (!use_database)
		return 0;

	sql_free_result();
	if (db.conn) {
		db.conn->api->finalize(db.conn);
		db.conn = NULL;
	}

	return 0;
}


int sql_reinit(void)
{
	sql_close();
	return sql_init();
}

/*
 * Some nifty helper functions which still allow us to keep the
 * db struct in file scope
 */

const char *sql_db_name(void)
{
	return db.name ? db.name : "merlin";
}

const char *sql_db_user(void)
{
	return db.user ? db.user : "merlin";
}

const char *sql_db_pass(void)
{
	return db.pass ? db.pass : "merlin";
}

const char *sql_db_host(void)
{
	return db.host ? db.host : "localhost";
}

const char *sql_db_type(void)
{
	return db.type ? db.type : "mysql";
}

const char *sql_table_name(void)
{
	return db.table ? db.table : "report_data";
}


/*
 * Config parameters from the "database" section end up here.
 *
 * The option "logsql" tells this API to log all SQL commands
 * to stderr if value is not NULL and does not start with the
 * character '0' (zero).
 */
int sql_config(const char *key, const char *value)
{
	char *value_cpy;
	int err;

	value_cpy = value ? strdup(value) : NULL;

	if (!prefixcmp(key, "name") || !prefixcmp(key, "database"))
		db.name = value_cpy;
	else if (!prefixcmp(key, "logsql")) {
		db.logSQL = strtobool(value);
		free(value_cpy);
	}
	else if (!prefixcmp(key, "user"))
		db.user = value_cpy;
	else if (!prefixcmp(key, "pass"))
		db.pass = value_cpy;
	else if (!prefixcmp(key, "host"))
		db.host = value_cpy;
	else if (!prefixcmp(key, "type"))
		db.type = value_cpy;
	else if (!prefixcmp(key, "port") && value) {
		char *endp;
		free(value_cpy);
		db.port = (unsigned int)strtoul(value, &endp, 0);

		if (endp == value || *endp != 0)
			return -1;
	}
	else if (!strcmp(key, "host_perfdata_table"))
		host_perf_table = value_cpy;
	else if (!strcmp(key, "service_perfdata_table"))
		service_perf_table = value_cpy;
	else if (!strcmp(key, "perfdata_table"))
		host_perf_table = service_perf_table = value_cpy;
	else if (!strcmp(key, "commit_interval")) {
		err = grok_seconds(value, &commit_interval);
		ldebug("DB: commit_interval set to %ld seconds", commit_interval);
		free(value_cpy);
		return err;
	}
	else if (!strcmp(key, "commit_queries")) {
		char *endp;
		commit_queries = strtoul(value, &endp, 0);
		ldebug("DB: commit_queries set to %ld queries", commit_queries);
		free(value_cpy);
	}
	else {
		if (value_cpy)
			free(value_cpy);
		return -1; /* config error */
	}

	return 0;
}
