#include <stdio.h>

#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>

#include "logging.h"
#include "shared.h"

#define LOGERR 1
#define LOGWARN 2
#define LOGINFO 4
#define LOGDEBUG 8

static FILE *log_fp;
static char *log_file;
static int log_opts = LOGERR | LOGWARN | LOGINFO | LOGDEBUG;

int log_grok_var(char *var, char *val)
{
	if (!val)
		return 0;

	if (!strcmp(var, "log_levels")) {
		struct opt_code {
			char *name;
			int val;
		} opt_codes[] = {
			{ "all", -1 },
			{ "err", LOGERR },
			{ "warn", LOGWARN },
			{ "info", LOGINFO },
			{ "debug", LOGDEBUG },
			{ NULL, 0 },
		};
		char *p = val;

		for (p = val; p && *p; p = next_word(p)) {
			int i, mod = 0;

			if (*p == '+' || *p == '-')
				mod = *p++;

			for (i = 0; i < ARRAY_SIZE(opt_codes); i++) {
				char *opt = opt_codes[i].name;

				if (!opt) /* failed to find a valid word */
					return 0;

				if (!strncmp(p, opt, strlen(opt))) {
					log_opts |= opt_codes[i].val;
					if (!mod) /* not '+' or '-', so add all levels below it */
						log_opts |= opt_codes[i].val - 1;

					if (mod == '-') /* remove one level */
						log_opts = log_opts & ~opt_codes[i].val;
				}
			}
		}

		return 1;
	}

	if (!strcmp(var, "log_file")) {
		log_file = strdup(val);
		return 1;
	}

	return 0;
}

void log_deinit(void)
{
	if (log_fp) {
		fflush(log_fp);
		if (log_fp != stdout && log_fp != stderr) {
			fsync(fileno(log_fp));
			fclose(log_fp);
		}
	}
}

int log_init(void)
{
	if (!log_file || !strcmp(log_file, "stdout"))
		log_fp = stdout;

	if (!strcmp(log_file, "stderr"))
		log_fp = stderr;

	if (log_fp)
		return 0;

	log_fp = fopen(log_file, "w+");

	if (!log_fp)
		return -1;

	return 0;
}

static void vlog(int severity, const char *fmt, va_list ap)
{
	va_list(ap2);

	va_copy(ap2, ap);
	if (!log_fp)
		log_init();

	if (!log_fp)
		log_fp = stdout;

	fprintf(log_fp, "[%lu] %d: ", time(NULL), severity);
	vfprintf(log_fp, fmt, ap2);
	fputc('\n', log_fp);
}

void loginfo(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(LOG_INFO, fmt, ap);
	va_end(ap);
}

void logwarn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(LOG_WARNING, fmt, ap);
	va_end(ap);
}

void logerr(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(LOG_ERR, fmt, ap);
	va_end(ap);
}

void logdebug(const char *fmt, ...)
{
	if (log_opts & LOGDEBUG) {
		va_list ap;
		va_start(ap, fmt);
		vlog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}

/* log a system call failure and return -1 to indicate failure */
int sflog(const char *syscall, const char *func, int line)
{
	if (errno) {
		lerr("%s() failed in %s @ %d: %d, %s",
			 syscall, func, line, errno, strerror(errno));
		return -1;
	}

	ldebug("%s(): Success in %s @ %d", syscall, func, line);
	return 0;
}
