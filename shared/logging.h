#ifndef INCLUDE_mrln_logging_h__
#define INCLUDE_mrln_logging_h__

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#ifdef DEBUG_LOGGING
# define ldebug(fmt, args...) \
	log_msg(LOG_DEBUG, "%s:%s():%d: " fmt, __FILE__, __func__, __LINE__, ##args)
# define linfo(fmt, args...) \
	log_msg(LOG_INFO, "%s:%s():%d " fmt, __FILE__, __func__, __LINE__, ##args)
# define lmsg(fmt, args...) \
	log_msg(LOG_NOTICE, "%s:%s():%d " fmt, __FILE__, __func__, __LINE__, ##args)
# define lwarn(fmt, args...) \
	log_msg(LOG_WARNING, "%s:%s():%d " fmt, __FILE__, __func__, __LINE__, ##args)
# define lerr(fmt, args...) \
	log_msg(LOG_ERR, "%s:%s():%d " fmt, __FILE__, __func__, __LINE__, ##args)
#else
# define ldebug(fmt, args...) log_msg(LOG_DEBUG, fmt, ##args)
# define linfo(fmt, args...) log_msg(LOG_INFO, fmt, ##args)
# define lmsg(fmt, args...) log_msg(LOG_NOTICE, fmt, ##args)
# define lwarn(fmt, args...) log_msg(LOG_WARNING, fmt, ##args)
# define lerr(fmt, args...) log_msg(LOG_ERR, fmt, ##args)
#endif

extern int log_init(void);
extern void log_deinit(void);
extern int log_grok_var(char *var, char *val);
extern void log_msg(int severity, const char *fmt, ...)
	__attribute__((__format__(__printf__, 2, 3)));
#endif
