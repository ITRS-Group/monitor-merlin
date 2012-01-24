#include "shared.h"

static FILE *log_fp;
static char *log_file;
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
	char msg[4096];

	/* return early if we shouldn't log stuff of this severity */
	if (!((1 << severity) & log_levels)) {
		return;
	}

	if (!log_fp)
		log_init();

	/* if we can't log anywhere, return early */
	if (!log_fp && !isatty(fileno(stdout)))
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
	if (log_fp && log_fp != stdout) {
		fprintf(log_fp, "[%lu] %d: %s\n", time(NULL), severity, msg);
		/*
		 * systems where logging matters (a lot) can specify
		 * MERLIN_FLUSH_LOGFILES as CPPFLAGS when building
		 */
		fflush(log_fp);
#ifdef MERLIN_FLUSH_LOGFILES
		fsync(fileno(log_fp));
#endif
	}
}

static FILE *hexlog_fp;
void open_hexlog(const char *path)
{
	hexlog_fp = fopen(path, "a");
}

void hexlog_event(int in, merlin_node *node, merlin_event *pkt)
{
	unsigned i;
	char hex[] = "0123456789abcdef";

	if (!hexlog_fp)
		return;

	fprintf(hexlog_fp, "%s ### %s %s\n", isotime(NULL, ISOTIME_PREC_MAX),
			in ? "From" : "To  ", node->name);
	fprintf(hexlog_fp, "protocol : %u\n", pkt->hdr.protocol);
	fprintf(hexlog_fp, "type     : %u (%s)\n", pkt->hdr.type,
			callback_name(pkt->hdr.type));
	fprintf(hexlog_fp, "code     : %u\n", pkt->hdr.code);
	fprintf(hexlog_fp, "selection: %u\n", pkt->hdr.selection);
	fprintf(hexlog_fp, "len      : %u\n", pkt->hdr.len);
	fprintf(hexlog_fp, "sent     : %lu.%lu\n",
			(unsigned long)pkt->hdr.sent.tv_usec, (unsigned long)pkt->hdr.sent.tv_sec);
	for (i = 0; i < pkt->hdr.len; i++) {
		fprintf(hexlog_fp, "\\x%c%c ",
				hex[pkt->body[i] >> 4], hex[pkt->body[i] & 0xf]);
		if (!(i % 16)) {
			fputc('\n', hexlog_fp);
		}
	}
	/* now print the ascii representation */
	for (i = 0; i < pkt->hdr.len; i++) {
		fputc(isprint(pkt->body[i]) ? pkt->body[i] : '.', hexlog_fp);
		if (!(i % 16)) {
			fputc('\n', hexlog_fp);
		}
	}
}
