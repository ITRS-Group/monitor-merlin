#include "shared.h"

#define LOGERR 1
#define LOGWARN 2
#define LOGINFO 4
#define LOGDEBUG 8

static FILE *log_fp;
static char *log_file;
static int log_opts = LOGERR | LOGWARN | LOGINFO;

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
			{ "err", LOGERR },
			{ "warn", LOGWARN },
			{ "info", LOGINFO },
			{ "debug", LOGDEBUG },
		};
		char *p = val;

		log_opts = 0;

		for (p = val; p && *p; p = next_word(p)) {
			int i, mod = 0;

			if (*p == '+' || *p == '-')
				mod = *p++;

			for (i = 0; i < ARRAY_SIZE(opt_codes); i++) {
				char *opt = opt_codes[i].name;

				if (!opt) /* failed to find a valid word */
					return 0;

				if (!prefixcmp(p, opt)) {
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
		fprintf(stderr, "Logging to '%s'\n", log_file);
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
			log_fp = NULL;
		}
	}
}

int log_init(void)
{
	if (!log_file || !strcmp(log_file, "stdout")) {
		log_fp = stdout;
		return 0;
	}

	if (!strcmp(log_file, "stderr"))
		log_fp = stderr;

	if (log_fp)
		return 0;

	log_fp = fopen(log_file, "a");

	if (!log_fp)
		return -1;

	return 0;
}

void log_msg(int severity, const char *fmt, ...)
{
	va_list ap;
	int len;

	if (!log_fp)
		log_init();
	if (!log_fp)
		log_fp = stdout;

	fprintf(log_fp, "[%lu] %d: ", time(NULL), severity);

	va_start(ap, fmt);
	len = vfprintf(log_fp, fmt, ap);
	va_end(ap);
	if (fmt[len] != '\n')
		fputc('\n', log_fp);
	fflush(log_fp);
	fsync(fileno(log_fp));
}

void log_event_count(const char *prefix, merlin_event_counter *cnt, float t)
{
	log_msg(LOGINFO, "Handled %lld '%s' events in %.3f seconds in: %lld, out: %lld",
	        cnt->read + cnt->sent + cnt->dropped + cnt->logged, prefix, t,
	        cnt->read, cnt->sent + cnt->dropped + cnt->logged);
	if (!(cnt->sent + cnt->dropped + cnt->logged))
		return;
	log_msg(LOGINFO, "'%s' event details: read %lld, sent %lld, dropped %lld, logged %lld",
	        prefix, cnt->read, cnt->sent, cnt->dropped, cnt->logged);
}
