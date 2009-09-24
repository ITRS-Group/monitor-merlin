#ifndef INCLUDE_sql_h__
#define INCLUDE_sql_h__

#include <dbi/dbi.h>

#define prefixcmp(a, b) strncmp(a, b, strlen(b))

typedef dbi_result SQL_RESULT;

extern int sql_config(const char *key, const char *value);
extern int sql_is_connected(void);
extern int sql_init(void);
extern int sql_close(void);
extern int sql_reinit(void);
extern size_t sql_escape(const char *src, char **dst);
extern size_t sql_quote(const char *src, char **dst);
extern int sql_error(const char **msg);
extern int sql_errno(void);
extern void sql_free_result(void);
extern int sql_query(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern dbi_result sql_get_result(void);
extern const char *sql_table_name(void);
extern const char *sql_db_name(void);
extern const char *sql_db_user(void);
extern const char *sql_db_pass(void);
extern const char *sql_db_host(void);

#endif
