#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>

#define ldebug logdebug
#define linfo loginfo
#define lwarn logwarn
#define lerr logerr
#define lmsg log_msg

#define SFLOG(s) sflog(s, __func__, __LINE__);

void log_deinit(void);
int log_grok_var(char *var, char *val);
void loginfo(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
void logwarn(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
void logerr(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
void logdebug(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
void log_msg(int level, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
int sflog(const char *syscall, const char *func, int line);

#endif
