#ifndef _SQL_H_
#define _SQL_H_

#include <dbi/dbi.h>

#define prefixcmp(a, b) strncmp(a, b, strlen(b))

typedef dbi_result SQL_RESULT;

extern int sql_config(const char *key, const char *value);
extern int sql_init(void);
extern int sql_close(void);
extern size_t sql_escape(const char *src, char **dst);
extern size_t sql_quote(const char *src, char **dst);
extern const char *sql_error(void);
extern int sql_errno(void);
extern void sql_free_result(void);
extern int sql_query(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));

extern const char *sql_table_name();

#endif
