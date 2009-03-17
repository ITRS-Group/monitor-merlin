#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include "sql.h"
#include "logging.h"
#include <dbi/dbi.h>
#include <string.h>

static struct {
	char *host;
	char *name;
	char *user;
	char *pass;
	char *table;
	char *type;
	char *encoding;
	unsigned int port;
	dbi_conn conn;
	dbi_result result;
} db;

#undef ESC_BUFSIZE
#define ESC_BUFSIZE (8192 * 2)
#undef ESC_BUFS
#define ESC_BUFS 8 /* must be a power of 2 */
#define MAX_ESC_STRING ((ESC_BUFSIZE * 2) + 1)

#define esc(s) sql_escape(s)
size_t sql_escape(const char *src, char **dst)
{
	return dbi_conn_escape_string_copy(db.conn, src, dst);
}

/*
 * these two functions are only here to allow callers
 * access to error reporting without having to expose
 * the db-link to theh callers. It's also nifty as we
 * want to remain database layer agnostic
 */
const char *sql_error()
{
	const char *msg;

	dbi_conn_error(db.conn, &msg);

	return msg;
}

void sql_free_result(void)
{
	dbi_result_free(db.result);
}

int sql_query(const char *fmt, ...)
{
	unsigned char *query;
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vasprintf((char **)&query, fmt, ap);
	va_end(ap);

	if (len == -1 || !query) {
		linfo("sql_query: Failed to build query from format-string '%s'", fmt);
		return -1;
	}
	db.result = dbi_conn_query_null(db.conn, query, len);
	if (!db.result) {
		linfo("dbi_conn_query_null(): Failed to run [%s]: %s",
			  query, sql_error());
	}

	free(query);

	return !!db.result;
}

int sql_init(void)
{
	dbi_initialize(NULL);
	db.conn = dbi_conn_new(db.type ? db.type : "mysql");
	dbi_conn_set_option(db.conn, "host", db.host ? db.host : "localhost");
	dbi_conn_set_option(db.conn, "username", db.user ? db.user : "monitor");
	dbi_conn_set_option(db.conn, "password", db.pass ? db.pass : "monitor");
	dbi_conn_set_option(db.conn, "dbname", db.name ? db.name : "monitor_gui");
	if (db.port)
		dbi_conn_set_option_numeric(db.conn, "port", db.port);
	dbi_conn_set_option(db.conn, "encoding", db.encoding ? db.encoding : "UTF-8");

	if (dbi_conn_connect(db.conn) < 0) {
		lerr("Failed to connect to '%s' at '%s':'%d' using %s:%s as credentials: %s",
		     db.name, db.host, db.port, db.user, db.pass, sql_error());

		db.conn = NULL;
		return -1;
	}

	return 0;
}

int sql_close(void)
{
	dbi_conn_close(db.conn);
	return 0;
}

const char *sql_table_name(void)
{
	if (!db.table)
		return "report_data";

	return db.table;
}

int sql_config(const char *key, const char *value)
{
	if (!prefixcmp(key, "name"))
		db.name = strdup(value);
	else if (!prefixcmp(key, "user"))
		db.user = strdup(value);
	else if (!prefixcmp(key, "pass"))
		db.pass = strdup(value);
	else if (!prefixcmp(key, "host"))
		db.host = strdup(value);
	else if (!prefixcmp(key, "port")) {
		char *endp;

		db.port = (unsigned int)strtoul(value, &endp, 0);

		if (endp == value || *endp != 0)
			return -1;
	}
	else
		return -1; /* config error */

	return 0;
}
