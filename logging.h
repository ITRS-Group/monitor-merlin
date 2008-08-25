#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <syslog.h>


#define ldebug(fmt, args...) \
	log_msg(LOG_DEBUG, "%s:%s():%d: " fmt, __FILE__, __func__, __LINE__, ##args)
#define linfo(fmt, args...) \
	log_msg(LOG_INFO, fmt, ##args)
#define lmsg(fmt, args...) \
	log_msg(LOG_NOTICE, fmt, ##args)
#define lwarn(fmt, args...) \
	log_msg(LOG_WARNING, fmt, ##args)
#define lerr(fmt, args...) \
	log_msg(LOG_ERR, fmt, ##args)

void log_deinit(void);
int log_grok_var(char *var, char *val);
void log_msg(int severity, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
int sflog(const char *syscall, const char *func, int line);

#endif
