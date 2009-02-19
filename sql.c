#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include "sql.h"
#include "logging.h"

static struct {
	char *host;
	char *name;
	char *user;
	char *pass;
	char *table;
	unsigned int port;
	MYSQL *link;
} db;

#undef ESC_BUFSIZE
#define ESC_BUFSIZE (8192 * 2)
#undef ESC_BUFS
#define ESC_BUFS 8 /* must be a power of 2 */
#define MAX_ESC_STRING ((ESC_BUFSIZE * 2) + 1)

#define esc(s) sql_escape(s)
char *sql_escape(const char *str)
{
	static int idx = 0;
	static char buf_ary[ESC_BUFS][ESC_BUFSIZE];
	char *buf;
	int len;

	if (!str || !*str)
		return "";

	len = strlen(str);

	if (len >= MAX_ESC_STRING) {
		lerr("len > MAX_ESC_STRING in sql_escape (%d > %d)", len, MAX_ESC_STRING);
		return "";
	}

	buf = buf_ary[idx++ & (ESC_BUFS - 1)];
	idx &= ESC_BUFS - 1;
	mysql_real_escape_string(db.link, buf, str, len);

	return buf;
}

/*
 * these two functions are only here to allow callers
 * access to error reporting without having to expose
 * the db-link to theh callers. It's also nifty as we
 * want to remain database layer agnostic
 */
const char *sql_error()
{
	return mysql_error(db.link);
}

int sql_errno(void)
{
	return mysql_errno(db.link);
}

SQL_RESULT *sql_get_result(void)
{
	return mysql_use_result(db.link);
}

SQL_ROW sql_fetch_row(MYSQL_RES *result)
{
	return mysql_fetch_row(result);
}

void sql_free_result(SQL_RESULT *result)
{
	mysql_free_result(result);
}

int sql_query(const char *fmt, ...)
{
	char *query;
	int len, result = 0;
	va_list ap;

	va_start(ap, fmt);
	len = vasprintf(&query, fmt, ap);
	va_end(ap);

	if (len == -1 || !query) {
		linfo("sql_query: Failed to build query from format-string '%s'", fmt);
		return -1;
	}
	if ((result = mysql_real_query(db.link, query, len)))
		linfo("mysql_query(): Failed to run [%s]: %s",
			  query, mysql_error(db.link));

	free(query);

	return result;
}

int sql_init(void)
{
	if (!(db.link = mysql_init(NULL)))
		return -1;

	if (!db.host) {
		db.host = "";
		db.port = 0;
	}

	if (!(mysql_real_connect(db.link, db.host, db.user, db.pass,
							 db.name, db.port, NULL, 0)))
	{
		lerr("Failed to connect to '%s' at '%s':'%d' using %s:%s as credentials: %s",
		     db.name, db.host, db.port, db.user, db.pass, mysql_error(db.link));

		db.link = NULL;
		return -1;
	}

	return 0;
}

int sql_close(void)
{
	mysql_close(db.link);
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
	if (!prefixcmp(key, "db_database"))
		db.name = strdup(value);
	else if (!prefixcmp(key, "db_user"))
		db.user = strdup(value);
	else if (!prefixcmp(key, "db_pass"))
		db.pass = strdup(value);
	else if (!prefixcmp(key, "db_host"))
		db.host = strdup(value);
	else if (!prefixcmp(key, "db_table")) {
		db.table = strdup(value);
	}
	else if (!prefixcmp(key, "db_port")) {
		char *endp;

		db.port = (unsigned int)strtoul(value, &endp, 0);

		if (endp == value || *endp != 0)
			return -1;
	}
	else
		return -1; /* config error */

	return 0;
}
