#define _GNU_SOURCE
#include "daemon.h"

#include <stdio.h> /* debuggering only. */
#define MARKER printf("MARKER: %s:%d:%s():\t",__FILE__,__LINE__,__func__); printf

/* where to (optionally) stash performance data */
char *host_perf_table = NULL;
char *service_perf_table = NULL;

#define FIXME(X)

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
		int port /* need to be signed int for compatibility with dbi_conn_set_option_numeric() (and similar)*/;
		db_wrap * conn;
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



static time_t last_connect_attempt;

/*
 * Quotes a string and escapes all meta-characters inside the string.
 * If src is NULL or !*src then 0 is returned and *dest is not modified.
 * *dst must be free()'d by the caller.
 */
size_t sql_quote(const char *src, char **dst)
{
	size_t ret;

	if (!db.conn) {
		sql_init();
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

static int run_query(char *query, size_t len, int rerunIGNORED)
{
	db_wrap_result *res = NULL;
	int rc = db.conn->api->query_result(db.conn, query, len, &res);

	if (db.logSQL) {
		fprintf(stderr, "MERLIN SQL: [%s]\n\tResult code: %d, result object @%p\n", query, rc, res);
		if (rc) {
			fprintf(stderr, "\tError code: %d\n", rc);
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
		lerr("Not using a database, but daemon still issued a query");
		return -1;
	}

	/*
	 * don't even bother trying to run the query if the database
	 * isn't online and we recently tried to connect to it
	 */
	if (last_connect_attempt + 30 > time(NULL) && !sql_is_connected())
		return -1;

	/* free any leftover result and run the new query */
	sql_free_result();

	len = vasprintf(&query, fmt, ap);
	if (len == -1 || !query) {
		lerr("sql_query: Failed to build query from format-string '%s'", fmt);
		return -1;
	}

	if (run_query(query, len, 0) != 0) {
		const char *error_msg;
		FIXME("db_wrap API currently only returns error string, not error code.");
		FIXME("Add db_error int back in once db_wrap API can do it.");
		int db_error = sql_error(&error_msg);

		lwarn("dbi_conn_query_null(): Failed to run [%s]: %s. Error-code is %d.)",
			  query, error_msg, db_error);
#if 0 //FIXME("Refactor/rework the following for the new db layer..."); Current code can endlessly loop
		/*
		 * if we failed because the connection has gone away, we try
		 * reconnecting once and rerunning the query before giving up.
		 */
		switch (db_error) {
#if 0
		case DBI_ERROR_USER:
		case DBI_ERROR_BADTYPE:
		case DBI_ERROR_BADIDX:
		case DBI_ERROR_BADNAME:
		case DBI_ERROR_UNSUPPORTED:
		case DBI_ERROR_NONE:
			break;
#endif
			    /*
			      These values are only for MySQL. Oracle might use
			      these to mean different things!
			    */
		case 1062: /* 'duplicate key' with MySQL. don't rerun */
		case 1146: /* 'table missing' with MySQL. don't rerun */
			if (!strstr(db.type, "mysql"))
				break;
		default:
			lwarn("Attempting to reconnect to database and re-run the query");
			if (!sql_reinit()) {
				if (!run_query(query, len, 1))
					lwarn("Successfully ran the previously failed query");
				/* database backlog code goes here */
			}
		}
#endif
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

int sql_is_connected(void)
{
	return (db.conn && db.conn->api->is_connected(db.conn)) ? 1 : 0;
}

int sql_init(void)
{
	const char *env;
	int result;

	if (!use_database)
		return 0;

	if (last_connect_attempt + 30 >= time(NULL))
		return -1;
	last_connect_attempt = time(NULL);

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
		db.type = "dbi:mysql";
	}

	db_wrap_conn_params connparam = db_wrap_conn_params_empty;
	connparam.host = db.host;
	connparam.dbname = db.name;
	connparam.username = db.user;
	connparam.password = db.pass;
	if (db.port)
		connparam.port = db.port;

	result = db_wrap_driver_init(db.type, &connparam, &db.conn)
		/* FIXME: use driver-independent init function, instead of dbi directly */
		;
	if (result) {
		lerr("Failed to connect to db '%s' at host '%s':'%d' as user %s using driver %s.",
			 db.name, db.host, db.port, db.user, db.type );
		return -1;
	}

	if (db.logSQL) {
		fprintf(stderr, "MERLIN DB: Connected to db [%s] using driver [%s]\n",
				 db.name, db.type);
	}
	result = db.conn->api->option_set(db.conn, "encoding", db.encoding ? db.encoding : "UTF-8");

	if (result) {
		lerr("Warning: Failed to set one or more database connection options");
	}

	result = db.conn->api->connect(db.conn);
	if (result) {
		const char *error_msg;
		sql_error(&error_msg);
		lerr("Failed to connect to '%s' at '%s':'%d' as %s:%s: %s",
			         db.name, db.host, db.port, db.user, db.pass, error_msg);

		sql_close();
		return -1;
	}

	last_connect_attempt = 0;
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

	value_cpy = value ? strdup(value) : NULL;

		FIXME("we leak these copied values if we set a particular value more than once.");
	if (!prefixcmp(key, "name") || !prefixcmp(key, "database"))
		db.name = value_cpy;
	else if (!prefixcmp(key, "logsql")) {
		db.logSQL = (value && *value && (*value!='0')) ? 1 : 0;
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
	else {
		if (value_cpy)
			free(value_cpy);
		return -1; /* config error */
	}

	return 0;
}

#undef FIXME
#undef MARKER
