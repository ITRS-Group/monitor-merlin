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

size_t sql_quote(const char *src, char **dst)
{
	if (!src) {
		*dst = NULL;
		return 0;
	}
	return dbi_conn_quote_string_copy(db.conn, src, dst);
}

size_t sql_escape(const char *src, char **dst)
{
	size_t len;
	len = dbi_conn_quote_string_copy(db.conn, src, dst);
	*dst[len] = 0;
	(*dst)++;
	return len - 2;
}

/*
 * these two functions are only here to allow callers
 * access to error reporting without having to expose
 * the db-link to theh callers. It's also nifty as we
 * want to remain database layer agnostic
 */
const char *sql_error(void)
{
	const char *msg;

	if (!db.conn)
		return "no db connection";

	dbi_conn_error(db.conn, &msg);

	return msg;
}

void sql_free_result(void)
{
	if (db.result) {
		dbi_result_free(db.result);
		db.result = NULL;
	}
}

dbi_result sql_get_result(void)
{
	return db.result;
}

int sql_query(const char *fmt, ...)
{
	char *query;
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vasprintf(&query, fmt, ap);
	va_end(ap);

	if (len == -1 || !query) {
		linfo("sql_query: Failed to build query from format-string '%s'", fmt);
		return -1;
	}
	db.result = dbi_conn_query_null(db.conn, (unsigned char *)query, len);
	if (!db.result) {
		linfo("dbi_conn_query_null(): Failed to run [%s]: %s",
			  query, sql_error());
	}

	free(query);

	return !!db.result;
}

int sql_init(void)
{
	int result;
	dbi_driver driver;

	result = dbi_initialize(NULL);
	if (result < 1) {
		lerr("Failed to initialize any libdbi drivers");
		return -1;
	}

	if (!db.type)
		db.type = "mysql";

	driver = dbi_driver_open(db.type);
	if (!driver) {
		lerr("Failed to open libdbi driver '%s'", db.type);
		return -1;
	}

	db.conn = dbi_conn_open(driver);
	if (!db.conn) {
		lerr("Failed to create a database connection instance");
		return -1;
	}

	result = dbi_conn_set_option(db.conn, "host", db.host ? db.host : "localhost");
	result |= dbi_conn_set_option(db.conn, "username", db.user ? db.user : "merlin");
	result |= dbi_conn_set_option(db.conn, "password", db.pass ? db.pass : "merlin");
	result |= dbi_conn_set_option(db.conn, "dbname", db.name ? db.name : "merlin");
	if (db.port)
		result |= dbi_conn_set_option_numeric(db.conn, "port", db.port);
	result |= dbi_conn_set_option(db.conn, "encoding", db.encoding ? db.encoding : "UTF-8");
	if (result) {
		lerr("Failed to set one or more database connection options");
	}

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
	return db.pass ? db.pass : "localhost";
}

const char *sql_table_name(void)
{
	return db.table ? db.table : "report_data";
}

int sql_config(const char *key, const char *value)
{
	char *value_cpy;

	if (value)
		value_cpy = strdup(value);
	else
		value_cpy = NULL;

	if (!prefixcmp(key, "name") || !prefixcmp(key, "database"))
		db.name = value_cpy;
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
	else
		return -1; /* config error */

	return 0;
}
