#include "logging.h"
#include "shared.h"

#include <string.h>

static FILE *merlin_log_fp;
static char *merlin_log_file;
static int log_to_syslog = 0;
static int log_levels = (1 << LOG_ERR) | (1 << LOG_WARNING) | (1 << LOG_INFO);

int log_grok_var(char *var, char *val)
{
	if (!val)
		return 0;

	if (!strcmp(var, "log_levels") || !strcmp(var, "log_level")) {
		struct opt_code {
			char *name;
			int val;
		} opt_codes[] = {
			{ "all", -1 },
			{ "err", 1 << LOG_ERR },
			{ "warn", 1 << LOG_WARNING },
			{ "info", 1 << LOG_INFO },
			{ "debug", 1 << LOG_DEBUG },
		};
		char *p = val;

		for (p = val; p && *p; p = next_word(p)) {
			uint i, mod = 0;

			if (*p == '+' || *p == '-')
				mod = *p++;

			for (i = 0; i < ARRAY_SIZE(opt_codes); i++) {
				char *opt = opt_codes[i].name;

				if (!opt) /* failed to find a valid word */
					return 0;

				if (!prefixcmp(p, opt)) {
					if (!mod) /* not '+' or '-', so add all levels below it */
						log_levels = opt_codes[i].val * 2 - 1;
					else if (mod == '-') /* remove one level */
						log_levels = log_levels & ~opt_codes[i].val;
					else
						log_levels |= opt_codes[i].val;
				}
			}
		}

		return 1;
	}

	if (!strcmp(var, "use_syslog")) {
		log_to_syslog = (unsigned)strtoul(val, NULL, 10);
		return 1;
	}

	if (!strcmp(var, "log_file")) {
		merlin_log_file = strdup(val);
		if (debug)
			fprintf(stderr, "Logging to '%s'\n", merlin_log_file);
		return 1;
	}

	return 0;
}

void log_deinit(void)
{
	if (log_to_syslog && !is_module)
		closelog();

	if (merlin_log_fp) {
		fflush(merlin_log_fp);
		if (merlin_log_fp != stdout && merlin_log_fp != stderr) {
			fsync(fileno(merlin_log_fp));
			fclose(merlin_log_fp);
			merlin_log_fp = NULL;
		}
	}
}

int log_init(void)
{
	if (log_to_syslog && !is_module)
		openlog("merlind", 0, LOG_DAEMON);

	if (!merlin_log_file)
		return 0;

	if (!strcmp(merlin_log_file, "stdout")) {
		merlin_log_fp = stdout;
		return 0;
	}

	if (!strcmp(merlin_log_file, "stderr"))
		merlin_log_fp = stderr;

	if (merlin_log_fp)
		return 0;

	merlin_log_fp = fopen(merlin_log_file, "a");

	if (!merlin_log_fp)
		return -1;

	return 0;
}

void log_msg(int severity, const char *fmt, ...)
{
	va_list ap;
	int len;
	char msg[4096];

	/* return early if we shouldn't log stuff of this severity */
	if (!((1 << severity) & log_levels)) {
		return;
	}

	/* if we can't log anywhere, return early */
	if (!merlin_log_fp && !log_to_syslog && !isatty(fileno(stdout)))
		return;

	if (log_to_syslog) {
		if (is_module)
			openlog("merlin_mod", 0, LOG_DAEMON);
		va_start(ap, fmt);
		vsyslog(severity, fmt, ap);
		va_end(ap);
		if (is_module)
			closelog();
	}

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	/* if vsnprinft fails we can't really do anything */
	if (len < 0)
		return;

	/* Set to max size length if it was truncated */
	if (len > 4095)
		len=4095;

	if (msg[len] == '\n')
		msg[len] = 0;

	/* only print to log if it's something else than 'stdout' */
	if (merlin_log_fp) {
		fprintf(merlin_log_fp, "[%lu] %d: %s\n", time(NULL), severity, msg);
		/*
		 * systems where logging matters (a lot) can specify
		 * MERLIN_FLUSH_LOGFILES as CPPFLAGS when building
		 */
		fflush(merlin_log_fp);
#ifdef MERLIN_FLUSH_LOGFILES
		fsync(fileno(merlin_log_fp));
#endif
	}
}
