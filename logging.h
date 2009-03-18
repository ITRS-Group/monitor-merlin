#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>
#include <syslog.h>


#define ldebug(fmt, args...) \
	log_msg(LOG_DEBUG, "%s:%s():%d: " fmt, __FILE__, __func__, __LINE__, ##args)
#define linfo(fmt, args...) \
	log_msg(LOG_INFO, "%s:%s():%d" fmt, __FILE__, __func__, __LINE__, ##args)
#define lmsg(fmt, args...) \
	log_msg(LOG_NOTICE, "%s:%s():%d" fmt, __FILE__, __func__, __LINE__, ##args)
#define lwarn(fmt, args...) \
	log_msg(LOG_WARNING, "%s:%s():%d" fmt, __FILE__, __func__, __LINE__, ##args)
#define lerr(fmt, args...) \
	log_msg(LOG_ERR, "%s:%s():%d" fmt, __FILE__, __func__, __LINE__, ##args)

#define logerr lerr

extern int log_init(void);
extern void log_deinit(void);
extern int log_grok_var(char *var, char *val);
extern void log_msg(int severity, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));

#endif
