#include "shared.h"

static FILE *merlin_log_fp;
static char *merlin_log_file;
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

	if (!strcmp(var, "log_file")) {
		merlin_log_file = strdup(val);
		fprintf(stderr, "Logging to '%s'\n", merlin_log_file);
		return 1;
	}

	return 0;
}

void log_deinit(void)
{
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
	if (!merlin_log_file || !strcmp(merlin_log_file, "stdout")) {
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

	if (!merlin_log_fp)
		log_init();

	/* if we can't log anywhere, return early */
	if (!merlin_log_fp && !isatty(fileno(stdout)))
		return;

	va_start(ap, fmt);
	len = vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	/* if vsnprinft fails we can't really do anything */
	if (len < 0)
		return;

	if (msg[len] == '\n')
		msg[len] = 0;

	/* always print log messages to stdout when we're debugging */
	if (isatty(fileno(stdout))) {
		printf("[%lu] %d: %s\n", time(NULL), severity, msg);
	}

	/* only print to log if it's something else than 'stdout' */
	if (merlin_log_fp && merlin_log_fp != stdout) {
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
