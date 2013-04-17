#ifndef INCLUDE_sql_h__
#define INCLUDE_sql_h__

#include <stdarg.h>
#include "db_wrap.h"
#define prefixcmp(a, b) strncmp(a, b, strlen(b))

extern char *host_perf_table;
extern char *service_perf_table;
extern unsigned long total_queries;
extern int sql_table_crashed;

/*typedef dbi_result SQL_RESULT;*/

extern int sql_config(const char *key, const char *value);
extern int sql_is_connected(int reconnect);
extern int sql_repair_table(const char *table);
extern int sql_init(void);
extern int sql_close(void);
extern int sql_reinit(void);
extern size_t sql_quote(const char *src, char **dst);
extern int sql_error(const char **msg);
extern const char *sql_error_msg(void);
extern void sql_free_result(void);
extern int sql_query(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern int sql_vquery(const char *fmt, va_list ap);
extern db_wrap_result * sql_get_result(void);
extern void sql_try_commit(int query);
extern const char *sql_table_name(void);
extern const char *sql_db_name(void);
extern const char *sql_db_user(void);
extern const char *sql_db_pass(void);
extern const char *sql_db_host(void);
extern unsigned int sql_db_port(void);
extern const char *sql_db_type(void);
extern const char *sql_db_conn_str(void);
#endif
