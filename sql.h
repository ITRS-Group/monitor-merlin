#ifndef _SQL_H_
#define _SQL_H_

#include <nagios/nebstructs.h>
#include <mysql/mysql.h>

#define prefixcmp(a, b) strncmp(a, b, strlen(b))

typedef MYSQL_RES SQL_RESULT;
typedef MYSQL_ROW SQL_ROW;

extern int sql_config(const char *key, const char *value);
extern int sql_init(void);
extern int sql_close(void);
extern char *sql_escape(const char *str);
extern const char *sql_error(void);
extern int sql_errno(void);
extern SQL_RESULT *sql_get_result(void);
extern SQL_ROW sql_fetch_row(SQL_RESULT *result);
extern void sql_free_result(SQL_RESULT *result);
extern int sql_query(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));

extern const char *sql_table_name();

#endif
