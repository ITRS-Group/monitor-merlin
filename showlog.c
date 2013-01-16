#define _GNU_SOURCE 1
#include <sys/types.h>
#include <signal.h>
#include <stdarg.h>

#include "shared.h"
#include "lparse.h"
#include "logutils.h"
#include "auth.h"
#include "status.h"

#define MAX_NVECS 16
#define HASH_TABLE_SIZE 128

static const char *progname;
static time_t first_time, last_time; /* first and last timestamp to show */
static time_t last_ltime, ltime; /* timestamp of last and current log-line */
static time_t last_start_time; /* time of last start event */
static uint last_severity, severity = -1;
static const char *image_url = "/ninja/application/views/themes/default/icons/16x16";
static int reverse_parse_files;
static unsigned long long ltime_skews, skip, limit;
static int hide_state_dupes; /* if set, we hide duplicate state messages */
static int count;
static unsigned long printed_lines;

#define EVT_PROCESS  (1 << 0)
#define EVT_NOTIFY   (1 << 1)
#define EVT_ALERT    (1 << 2)
#define EVT_COMMAND  (1 << 3)
#define EVT_STATE    (1 << 4)
#define EVT_FLAPPING (1 << 5)
#define EVT_DOWNTIME (1 << 6)
#define EVT_LROTATE  (1 << 7)
#define EVT_EHANDLER (1 << 8)
#define EVT_START    (1 << 9)
#define EVT_STOP     (1 << 10)
#define EVT_LIVESTATUS (1 << 11)
#define EVT_QH       (1 << 12)
#define EVT_NERD     (1 << 13)
#define EVT_WPROC    (1 << 14)

#define EVT_HOST    (1 << 20)
#define EVT_SERVICE (1 << 21)

/* show everything but livestatus by default */
static uint event_filter = ~(EVT_LIVESTATUS);
static int host_state_filter = -1;
static int service_state_filter = -1;
static int statetype_filter = (1 << HARD_STATE) | (1 << SOFT_STATE);

#define add_event(string, eventcode) add_code(0, string, eventcode)
static struct string_code event_codes[] = {
	add_event("Error", EVT_PROCESS),
	add_event("Warning", EVT_PROCESS),
	add_event("LOG ROTATION", EVT_LROTATE),
	add_event("LOG VERSION", EVT_PROCESS),
	add_event("EXTERNAL COMMAND", EVT_COMMAND),
	add_event("livestatus", EVT_LIVESTATUS),
	add_event("qh", EVT_QH | EVT_PROCESS),
	add_event("nerd", EVT_NERD | EVT_PROCESS),
	add_event("NERD", EVT_NERD | EVT_PROCESS),
	add_event("wproc", EVT_WPROC | EVT_PROCESS),

	add_code(5, "HOST ALERT", EVT_ALERT | EVT_HOST),
	add_code(5, "INITIAL HOST STATE", EVT_STATE | EVT_HOST),
	add_code(5, "CURRENT HOST STATE", EVT_STATE | EVT_HOST),
	add_code(5, "HOST EVENT HANDLER", EVT_EHANDLER | EVT_HOST),
	add_code(5, "HOST NOTIFICATION", EVT_NOTIFY | EVT_HOST),
	add_code(6, "SERVICE ALERT", EVT_ALERT | EVT_SERVICE),
	add_code(6, "INITIAL SERVICE STATE", EVT_STATE | EVT_SERVICE),
	add_code(6, "CURRENT SERVICE STATE", EVT_STATE | EVT_SERVICE),
	add_code(6, "SERVICE EVENT HANDLER", EVT_EHANDLER | EVT_SERVICE),
	add_code(6, "SERVICE NOTIFICATION", EVT_NOTIFY | EVT_SERVICE),
	add_code(3, "HOST DOWNTIME ALERT", EVT_DOWNTIME | EVT_HOST),
	add_code(3, "HOST FLAPPING ALERT", EVT_FLAPPING | EVT_HOST),
	add_code(4, "SERVICE DOWNTIME ALERT", EVT_DOWNTIME | EVT_SERVICE),
	add_code(4, "SERVICE FLAPPING ALERT", EVT_FLAPPING | EVT_SERVICE),
	{ 0, NULL, 0, 0 },
};

static void exit_nicely(int code)
{
	if (count) {
		printf("%lu\n", printed_lines);
	}
	exit(code);
}

static void print_time_iso8601(struct tm *t)
{
	printf("[%d-%02d-%02d %02d:%02d:%02d] ",
		   t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
		   t->tm_hour, t->tm_min, t->tm_sec);
}

static void print_time_div_iso8601(struct tm *t)
{
	printf("%d-%02d-%02d %02d:00 ",
		   t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour);
}

static void print_time_duration(struct tm *t)
{
	printf("[%6lu] ", ltime - last_ltime);
}

static void print_time_raw(struct tm *t)
{
	printf("[%lu] ", ltime);
}

static const char *human_duration(time_t now, time_t then)
{
	static char buf[100];
	const char suffix[] = "mhdw";
	const int dividers[ARRAY_SIZE(suffix) - 1] = { 60, 3600, 3600 * 24, 3600 * 24 * 7 };
	int vals[ARRAY_SIZE(suffix) - 1];
	int weeks, days, hours, minutes, seconds, i;
	time_t delta = now - then;

	if (!delta)
		return "0s";

	for (i = ARRAY_SIZE(dividers) - 1; delta && i >= 0; i--) {
		vals[i] = delta / dividers[i];
		delta %= dividers[i];
	}
	weeks = vals[3];
	days = vals[2];
	hours = vals[1];
	minutes = vals[0];
	seconds = (int)delta;
	if (weeks) {
		sprintf(buf, "%dw %dd %dh %dm %ds", weeks, days, hours, minutes, seconds);
	} else if (days) {
		sprintf(buf, "%dd %dh %dm %ds", days, hours, minutes, seconds);
	} else if (hours) {
		sprintf(buf, "%dh %dm %ds", hours, minutes, seconds);
	} else if (minutes) {
		sprintf(buf, "%dm %ds", minutes, seconds);
	} else {
		sprintf(buf, "%ds", seconds);
	}
	return buf;
}

static void print_runtime(struct tm *t)
{
	printf("[%s] ", human_duration(ltime, last_start_time));
}

static struct {
	char *name;
	void (*func)(struct tm *);
	void (*func_div)(struct tm *);
} time_format_selections[] = {
	{ "iso8601", print_time_iso8601, print_time_div_iso8601 },
	{ "raw", print_time_raw, NULL },
	{ "duration", print_time_duration, NULL },
	{ "runtime", print_runtime, NULL },
	{ NULL, NULL, NULL },
};
static void (*print_time)(struct tm *) = print_time_iso8601;
static void (*print_time_div)(struct tm *) = print_time_div_iso8601;

static void parse_time_format(const char *selection)
{
	int i;

	if (selection) for (i = 0; time_format_selections[i].name; i++) {
		if (strcasecmp(selection, time_format_selections[i].name))
			continue;
		print_time = time_format_selections[i].func;
		print_time_div = time_format_selections[i].func_div;
		return;
	}

	crash("Illegal timeformat selection: '%s'\n", selection);
}

static inline void pre_print_mangle_line(struct tm *t, char *line, uint len)
{
	uint i;

	for (i = 0; i < len; i++) {
		if (!line[i])
			line[i] = ';';
	}

	localtime_r(&ltime, t);
}


static void print_line_count(int type, struct tm *t, char *line, uint len)
{
	return;
}

static void print_line_ascii(int type, struct tm *t, char *line, uint len)
{
	print_time(t);
	puts(line);
}


static void print_line_ansi(int type, struct tm *t, char *line, uint len)
{
	const char *color = NULL;

	switch (type) {
	case EVT_ALERT | EVT_HOST:
	case EVT_STATE | EVT_HOST:
		if (severity == HOST_UP)
			color = CLR_GREEN;
		else
			color = CLR_RED;
		break;

	case EVT_ALERT | EVT_SERVICE:
	case EVT_STATE | EVT_SERVICE:
		switch (severity) {
		case SERVICE_OK: color = CLR_GREEN; break;
		case SERVICE_WARNING: color = CLR_YELLOW; break;
		case SERVICE_CRITICAL: color = CLR_RED; break;
		case SERVICE_UNKNOWN: color = CLR_BROWN; break;
		}
		break;

	case EVT_DOWNTIME | EVT_HOST:
	case EVT_DOWNTIME | EVT_SERVICE:
		color = CLR_BRIGHT_CYAN;
		break;

	case EVT_FLAPPING | EVT_HOST:
	case EVT_FLAPPING | EVT_SERVICE:
		color = CLR_CYAN;
		break;

	case EVT_NOTIFY | EVT_HOST:
	case EVT_NOTIFY | EVT_SERVICE:
		color = CLR_BRIGHT_RED;
		break;

	case EVT_PROCESS:
		color = CLR_MAGENTA;
		break;

	case EVT_LROTATE:
		color = CLR_BOLD;
		break;

	case EVT_COMMAND:
		color = CLR_BRIGHT_MAGENTA;
		break;

	case EVT_START: case EVT_STOP:
		color = CLR_BRIGHT_BLUE;
		break;
	}

	if (color) {
		printf("%s", color);
		print_time(t);
		printf("%s%s\n", line, CLR_RESET);
	} else {
		print_time(t);
		puts(line);
	}
}


static void print_time_break(struct tm *t)
{
	struct tm h;

	memcpy(&h, t, sizeof(h));
	h.tm_min = h.tm_sec = 0;
	if (reverse_parse_files) {
		/* using mktime and localtime_r again here means we never
		 * have to worry about changing date, month or year in
		 * case we overshoot by one */
		time_t when = mktime(&h) + 3600;
		localtime_r(&when, &h);
	}

	printf("<h2>");
	print_time_div(&h);
	puts("</h2>");
}

#define write_and_quote(a) do{ \
	fwrite(line, 1, i, stdout); \
	fwrite(a, 1, strlen(a), stdout); \
	line = ++tmp; \
	i = 0; \
}while(0);

static void print_line_html(int type, struct tm *t, char *line, uint len)
{
	const char *image = NULL;
	static time_t last_time_break = 0;
	char *tmp;
	int i = 0;

	switch (type) {
	case EVT_ALERT | EVT_HOST:
	case EVT_STATE | EVT_HOST:
		if (severity == HOST_UP)
			image = "shield-ok.png";
		else
			image = "shield-critical.png";
		break;

	case EVT_ALERT | EVT_SERVICE:
	case EVT_STATE | EVT_SERVICE:
		switch (severity) {
		case SERVICE_OK: image = "shield-ok.png"; break;
		case SERVICE_WARNING: image = "shield-warning.png"; break;
		case SERVICE_CRITICAL: image = "shield-critical.png"; break;
		case SERVICE_UNKNOWN: image = "shield-unknown.png"; break;
		}
		break;

	case EVT_DOWNTIME | EVT_HOST:
	case EVT_DOWNTIME | EVT_SERVICE:
		image = "scheduled-downtime.png";
		break;

	case EVT_FLAPPING | EVT_HOST:
	case EVT_FLAPPING | EVT_SERVICE:
		image = "flapping.gif";
		break;

	case EVT_NOTIFY | EVT_HOST:
	case EVT_NOTIFY | EVT_SERVICE:
		image = "notify-send.png";
		break;

	case EVT_COMMAND:
		image = "command.png";
		break;

	case EVT_LROTATE:
		image = "logrotate.png";
		break;

	case EVT_EHANDLER | EVT_HOST:
		image = "hostevent.gif";
		break;

	case EVT_EHANDLER | EVT_SERVICE:
		image = "serviceevent.gif";
		break;

	case EVT_START:
		image = "start.png";
		break;

	case EVT_STOP:
		image = "stop.png";
		break;
	}

	if (!image)
		image = "shield-info.png";

	if (last_time_break != ltime / 3600) {
		print_time_break(t);
		last_time_break = ltime / 3600;
	}

	printf("<img src=\"%s/%s\" alt=\"%s\" /> ", image_url, image, image);
	print_time(t);
	tmp = line;
	while (*tmp != '\0') {
		switch (*tmp) {
		 case '&':
			write_and_quote("&amp;");
			break;
		 case '"':
			write_and_quote("&quot;");
			break;
		 case '\'':
			write_and_quote("&apos;");
			break;
		 case '<':
			write_and_quote("&lt;");
			break;
		 case '>':
			write_and_quote("&gt;");
			break;
		 default:
			tmp++;
			i++;
			break;
		}
	}
	fwrite(line, 1, i, stdout);
	puts("<br />");
}


static void (*real_print_line)(int type, struct tm *, char *, uint) = print_line_ascii;
static void print_line(int type, char *line, uint len)
{
	static int last_type = 0;
	static char *last_line = NULL;
	static uint last_len = 0;
	struct tm t;

	/* are we still skipping? If so, return early */
	if (skip) {
		skip--;
		return;
	}

	pre_print_mangle_line(&t, line, len);
	if (print_time == print_time_duration) {
		int cur_severity = severity;
		if (last_line) {
			severity = last_severity;
			printed_lines++;
			real_print_line(last_type, &t, last_line, last_len);
			severity = cur_severity;
			free(last_line);
		}
		last_severity = severity;
		if (line)
			last_line = strdup(line);
		last_type = type;
		last_len = len;
		last_ltime = ltime;
	} else {
		printed_lines++;
		real_print_line(type, &t, line, len);
	}

	/* if we've printed all the lines we should, just exit */
	if (limit && !--limit)
		exit_nicely(0);
}


static int parse_line(char *orig_line, uint len)
{
	char *ptr, *colon, *line;
	int nvecs = 0;
	struct string_code *sc;
	int hard;
	static time_t prev_ltime = 0;

	line_no++;

	/* ignore empty lines */
	if (!len)
		return 0;

	/* skip obviously bogus lines */
	if (len < 12 || *orig_line != '[') {
		warn("line %d; len too short, or line doesn't start with '[' (%s)",
			 line_no, orig_line);
		return -1;
	}

	ltime = strtoul(orig_line + 1, &ptr, 10);
	if (orig_line + 1 == ptr) {
		lp_crash("Failed to parse log timestamp from '%s'. I can't handle malformed logdata",
			  orig_line);
		return -1;
	}

	/* only print lines in the interesting interval */
	if (ltime < first_time || ltime > last_time)
		return 0;

	/*
	 * if ltime is less than the previously parsed ltime,
	 * increment the skew count. otherwise, update prev_ltime
	 * so timestamps stay incremental and skews are counted
	 * accurately
	 */
	if (ltime < prev_ltime) {
		ltime_skews++;
	} else {
		prev_ltime = ltime;
	}

	while (*ptr == ']' || *ptr == ' ')
		ptr++;

	line = ptr;
	len -= line - orig_line;

	if (!is_interesting(ptr))
		return 0;

	if (!(colon = strchr(ptr, ':'))) {
		/* stupid heuristic, but might be good for something,
		 * somewhere, sometime. if nothing else, it should suppress
		 * annoying output */
		if (!(event_filter & EVT_PROCESS))
			return 0;

		if (is_start_event(ptr)) {
			last_start_time = ltime;
			print_line(EVT_START, line, len);
		} else if (is_stop_event(ptr)) {
			print_line(EVT_STOP, line, len);
			last_start_time = 0;
		}

		return 0;
	}

	/* we found a line, so the daemon is obviously started */
	if (!last_start_time)
		last_start_time = ltime - 1;

	if (!(sc = get_event_type(ptr, colon - ptr))) {
		return 0;
	}

	if (sc->code == IGNORE_LINE)
		return 0;
	if ((sc->code & event_filter) != (uint)sc->code)
		return 0;

	severity = -1;
	ptr = colon + 1;
	while (*ptr == ' ')
		ptr++;

	if (sc->nvecs) {
		int i;

		nvecs = vectorize_string(ptr, sc->nvecs);

		if (nvecs != sc->nvecs) {
			/* broken line */
			warn("Line %d in %s seems to not have all the fields it should",
				 line_no, cur_file->path);
			return -1;
		}

		for (i = 0; i < sc->nvecs; i++) {
			if (!strv[i]) {
				/* this should never happen */
				warn("Line %d in %s seems to be broken, or we failed to parse it into a vector",
					 line_no, cur_file->path);
				return -1;
			}
		}
	}

	switch (sc->code) {
	case EVT_ALERT | EVT_HOST:
	case EVT_STATE | EVT_HOST:
		hard = soft_hard(strv[2]);
		if (!(statetype_filter & (1 << hard)))
			return 0;
		severity = parse_host_state(strv[1]);
		if (!(host_state_filter & (1 << severity)))
			return 0;
		if (!auth_host_ok(strv[0]))
			return 0;
		if (!is_interesting_host(strv[0]))
			return 0;
		if (hide_state_dupes && !host_has_new_state(strv[0], severity, hard))
			return 0;

		break;

	case EVT_ALERT | EVT_SERVICE:
	case EVT_STATE | EVT_SERVICE:
		hard = soft_hard(strv[3]);
		if (!(statetype_filter & (1 << hard)))
			return 0;
		severity = parse_service_state(strv[2]);
		if (!(service_state_filter & (1 << severity)))
			return 0;
		if (!auth_service_ok(strv[0], strv[1]))
			return 0;
		if (!is_interesting_service(strv[0], strv[1]))
			return 0;
		if (hide_state_dupes && !service_has_new_state(strv[0], strv[1], severity, hard))
			return 0;

		break;

	case EVT_FLAPPING | EVT_HOST:
	case EVT_DOWNTIME | EVT_HOST:
	case EVT_EHANDLER | EVT_HOST:
		if (!auth_host_ok(strv[0]))
			return 0;
		if (!is_interesting_host(strv[0]))
			return 0;
		break;

	case EVT_FLAPPING | EVT_SERVICE:
	case EVT_DOWNTIME | EVT_SERVICE:
	case EVT_EHANDLER | EVT_SERVICE:
		if (!auth_service_ok(strv[0], strv[1]))
			return 0;
		if (!is_interesting_service(strv[0], strv[1]))
			return 0;
		break;

	case EVT_NOTIFY | EVT_HOST:
		if (!auth_host_ok(strv[1]))
			return 0;
		if (!is_interesting_host(strv[1]))
			return 0;

	case EVT_NOTIFY | EVT_SERVICE:
		if (!auth_service_ok(strv[1], strv[2]))
			return 0;
		if (!is_interesting_service(strv[1], strv[2]))
			return 0;
	}

	print_line(sc->code, line, len);
	return 0;
}

/*
 * hashes one line from an "interesting"-file. We use (void *)1
 * to mark this as "present in hash-table" as we have no real
 * data to lookup but still want hash_find{,2} to return non-NULL
 * when it finds a match
 */
static int hash_one_line(char *line, uint len)
{
	return add_interesting_object(line);
}

static int hash_interesting(const char *path)
{
	struct stat st;

	if (stat(path, &st) < 0)
		lp_crash("failed to stat %s: %s", path, strerror(errno));

	lparse_path(path, st.st_size, hash_one_line);

	return 0;
}

static void parse_host_state_filter(char *p)
{
	host_state_filter = 0;
	for (; *p; p++) {
		switch (*p) {
		case 'a': case '*':
			host_state_filter = -1;
			break;
		case 'u':
			host_state_filter |= 1 << HOST_UNREACHABLE;
			break;
		case 'd':
			host_state_filter |= 1 << HOST_DOWN;
			break;
		case 'r':
			host_state_filter |= 1 << HOST_UP;
			break;
		}
	}
}

static void parse_service_state_filter(char *p)
{
	service_state_filter = 0;
	for (; *p; p++) {
		switch (*p) {
		case 'a': case '*':
			service_state_filter = -1;
			break;
		case 'r':
			service_state_filter |= 1 << SERVICE_OK;
			break;
		case 'w':
			service_state_filter |= 1 << SERVICE_WARNING;
			break;
		case 'c':
			service_state_filter |= 1 << SERVICE_CRITICAL;
			break;
		case 'u':
			service_state_filter |= 1 << SERVICE_UNKNOWN;
		}
	}
}


__attribute__((__format__(__printf__, 1, 2)))
static void usage(const char *fmt, ...)
{
	int i;

	if (fmt && *fmt) {
		va_list ap;

		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}

	printf("usage: %s [options] [logfiles]\n\n", progname);
	printf("  <logfiles> refers to all the nagios logfiles you want to search through\n");
	printf("  If --nagios-cfg is given or can be inferred no logfiles need to be supplied\n");
	printf("Options:\n");
	printf("  --reverse                               parse (and print) logs in reverse\n");
	printf("  --list-files                            list the interesting logfiles\n");
	printf("  --help                                  this cruft\n");
	printf("  --debug                                 print debugging information\n");
	printf("  --html                                  print html output\n");
	printf("  --ansi                                  force-colorize the output\n");
	printf("  --ascii                                 don't colorize the output\n"),
	printf("  --user=<username>                       show only logs this user can see\n");
	printf("  --cgi-cfg=</path/to/cgi.cfg>            path to cgi.cfg\n");
	printf("  --nagios-cfg=</path/to/nagios.cfg>      path to nagios.cfg\n");
	printf("  --object-cache=</path/to/objects.cache> path to objects.cache\n");
	printf("  --image-url=<image url>                 url to images. Implies --html\n");
	printf("  --hide-state-dupes                      hide duplicate status messages\n");
	printf("  --hide-flapping                         hide flapping messages\n");
	printf("  --hide-downtime                         hide downtime messages\n");
	printf("  --hide-process                          hide process messages\n");
	printf("  --hide-command                          hide external command messages\n");
	printf("  --hide-notifications                    hide notification messages\n");
	printf("  --hide-logrotation                      hide log rotation messages\n");
	printf("  --hide-initial                          hide INITIAL and CURRENT states\n");
	printf("  --hide-all                              hide all events\n");
	printf("  --show-ltime-skews                      print logtime clock skews\n");
	printf("  --skip=<integer>                        number of filtered in messages to skip\n");
	printf("  --limit=<integer>                       max number of messages to print\n");
	printf("  --host=<host_name>                      show log entries for the named host\n");
	printf("  --service=<hostname;servicedescription> show log entries for the named service\n");
	printf("  --first=<timestamp>                     first log-entry to show\n");
	printf("  --last=<timestamp>                      last log-entry to show\n");
	printf("  --state-type=[hard|soft]                state-types to show. default is all\n");
	printf("  --host-states=[*ardu]                   host-states to show. can be mixed\n");
	printf("                                              'a' and '*' shows 'all'\n");
	printf("                                              'r' shows 'recovery'\n");
	printf("                                              'd' shows 'down'\n");
	printf("                                              'u' shows 'unreachable'\n");
	printf("  --service-states=[*arwcu]               service-states to show. can be mixed\n");
	printf("                                              'a' and '*' shows 'all'\n");
	printf("                                              'r' shows 'recovery'\n");
	printf("                                              'w' shows 'warning'\n");
	printf("                                              'c' shows 'critical'\n");
	printf("                                              'u' shows 'unknown'\n");
	printf("  --time-format=[");
	for (i = 0; time_format_selections[i].name; i++) {
		printf("%s", time_format_selections[i].name);
		if (time_format_selections[i + 1].name)
			printf("|");
	}
	printf("]\n                                          set timeformat for log-entries\n");

	putchar('\n');

	if (fmt && *fmt)
		exit(1);

	exit(0);
}

#define OPT_SHOW 0
#define OPT_HIDE 1
#define show_hide_code(s, opt) { 0, s, sizeof(#s) - 1, opt }
int show_hide(char *arg, char *opt)
{
	int what;
	uint filter = 0, show_filter = 0;

	if (!prefixcmp(arg, "--hide"))
		what = OPT_HIDE;
	else if (!prefixcmp(arg, "--show"))
		what = OPT_SHOW;
	else
		return -1;

	if (arg[6] == '-') {
		arg = arg + 7;
	} else {
		arg = opt;
	}

	for (;;) {
		char *comma = strchr(arg, ',');
		if (comma)
			*comma = 0;

		/*
		 * 'filter' must be OR'ed to here, since we only OR or
		 * reverse-AND it once after the loop is done
		 */
		if (!prefixcmp(arg, "flapping")) {
			filter |= EVT_FLAPPING;
			show_filter |= EVT_HOST | EVT_SERVICE;
		} else if (!prefixcmp(arg, "downtime")) {
			filter |= EVT_DOWNTIME;
			show_filter |= EVT_HOST | EVT_SERVICE;
		} else if (!prefixcmp(arg, "process")) {
			filter |= EVT_PROCESS;
		} else if (!prefixcmp(arg, "command")) {
			filter |= EVT_COMMAND;
		} else if (!prefixcmp(arg, "notification")) {
			filter |= EVT_NOTIFY;
			show_filter |= (EVT_HOST | EVT_SERVICE);
		} else if (!prefixcmp(arg, "logrotat")) {
			filter |= EVT_LROTATE;
		} else if (!prefixcmp(arg, "livestatus")) {
			filter |= EVT_LIVESTATUS;
		} else if (!prefixcmp(arg, "initial")) {
			filter |= EVT_STATE;
			show_filter |= (EVT_HOST | EVT_SERVICE);
		} else if (!prefixcmp(arg, "all")) {
			filter |= -1;
		} else {
			usage("Unknown %s option: %s\n",
				  what == OPT_SHOW ? "show" : "hide", comma);
		}

		if (!comma)
			break;
		*comma = ',';
		arg = comma + 1;
	}

	if (what == OPT_SHOW) {
		event_filter |= filter | show_filter;
	} else {
		event_filter &= ~filter;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int i, show_ltime_skews = 0, list_files = 0;
	unsigned long long tot_lines = 0;
	const char *nagios_cfg = NULL, *cgi_cfg = NULL, *object_cache = NULL;
	int hosts_are_interesting = 0;

	progname = strrchr(argv[0], '/');
	progname = progname ? progname + 1 : argv[0];

	strv = calloc(sizeof(char *), MAX_NVECS);
	if (!strv)
		crash("Failed to alloc initial structs");

	if (isatty(fileno(stdout))) {
		real_print_line = print_line_ansi;
		event_filter &= ~(EVT_LROTATE | EVT_PROCESS);
	}

	for (i = 1; i < argc; i++) {
		char *opt, *arg = argv[i];
		int arg_len, eq_opt = 0;

		if ((opt = strchr(arg, '='))) {
			*opt++ = '\0';
			eq_opt = 1;
		}
		else if (i < argc - 1) {
			opt = argv[i + 1];
		}

		if (!strcmp(arg, "--count")) {
			count = 1;
			real_print_line = print_line_count;
			continue;
		}
		if (!strcmp(arg, "--reverse")) {
			reverse_parse_files = 1;
			continue;
		}
		if (!strcmp(arg, "--html")) {
			real_print_line = print_line_html;
			continue;
		}
		if (!strcmp(arg, "--ansi")) {
			real_print_line = print_line_ansi;
			continue;
		}
		if (!strcmp(arg, "--ascii")) {
			real_print_line = print_line_ascii;
			continue;
		}
		if (!strcmp(arg, "--debug") || !strcmp(arg, "-d")) {
			debug_level++;
			continue;
		}
		if (!strcmp(arg, "--help")) {
			usage(NULL);
			continue;
		}
		if (!strcmp(arg, "--list-files")) {
			list_files = 1;
			continue;
		}
		/* these must come before the more general "show/hide" below */
		if (!prefixcmp(arg, "--show-ltime-skews")) {
			show_ltime_skews = 1;
			continue;
		}
		if (!prefixcmp(arg, "--hide-state-dupes")) {
			hide_state_dupes = 1;
			continue;
		}
		if (!prefixcmp(arg, "--hide") || !prefixcmp(arg, "--show")) {
			if (show_hide(arg, eq_opt ? opt : NULL) < 0) {
				usage("Illegal option for '%s': %s\n", arg, opt);
			}
			continue;
		}


		if (!prefixcmp(arg, "--")) {
			if (!opt)
				usage("Option '%s' requires an argument\n", arg);
			if (!eq_opt)
				i++;
		}

		/* options parsed below require arguments */
		if (!strcmp(arg, "--user")) {
			auth_set_user(opt);
			continue;
		}
		if (!prefixcmp(arg, "--object-cache")) {
			object_cache = opt;
			continue;
		}
		if (!strcmp(arg, "--nagios-cfg")) {
			nagios_cfg = opt;
			continue;
		}
		if (!strcmp(arg, "--cgi-cfg")) {
			cgi_cfg = opt;
			continue;
		}
		if (!strcmp(arg, "--skip")) {
			skip = strtoull(opt, NULL, 0);
			continue;
		}
		if (!strcmp(arg, "--limit")) {
			limit = strtoull(opt, NULL, 0);
			continue;
		}
		if (!strcmp(arg, "--host")) {
			event_filter |= EVT_HOST;
			hosts_are_interesting = 1;
			add_interesting_object(opt);
			continue;
		}
		if (!strcmp(arg, "--service")) {
			event_filter |= EVT_SERVICE;
			if (!hosts_are_interesting)
				event_filter &= ~(EVT_HOST);
			add_interesting_object(opt);
			continue;
		}
		if (!strcmp(arg, "--image-url")) {
			real_print_line = print_line_html;
			image_url = opt;
			continue;
		}
		if (!strcmp(arg, "--interesting") || !strcmp(arg, "-i")) {
			if (!opt || !*opt)
				usage("%s requires a filename as argument", arg);
			hash_interesting(opt);
			continue;
		}
		if (!strcmp(arg, "--first") || !strcmp(arg, "--last")) {
			time_t when;

			if (!opt || !*opt)
				crash("%s requires a timestamp as argument", arg);
			when = strtoul(opt, NULL, 0);
			if (opt && !eq_opt)
				i++;
			if (!strcmp(arg, "--first"))
				first_time = when;
			else
				last_time = when;
			continue;
		}
		if (!strcmp(arg, "--state-type")) {
			if (!strcasecmp(opt, "hard"))
				statetype_filter = (1 << HARD_STATE);
			if (!strcasecmp(opt, "soft"))
				statetype_filter = (1 << SOFT_STATE);
			continue;
		}
		if (!strcmp(arg, "--host-states")) {
			event_filter |= EVT_HOST;
			hosts_are_interesting = 1;
			parse_host_state_filter(opt);
			continue;
		}
		if (!strcmp(arg, "--service-states")) {
			event_filter |= EVT_SERVICE;
			parse_service_state_filter(opt);
			continue;
		}
		if (!strcmp(arg, "--time-format")) {
			parse_time_format(opt);
			continue;
		}

		/* non-argument, so treat as config- or log-file */
		arg_len = strlen(arg);
		if (arg_len > 7 && !strcmp(&arg[arg_len - 7], "cgi.cfg")) {
			cgi_cfg = arg;
		} else if (!strcmp(&arg[strlen(arg) - 10], "nagios.cfg")) {
			nagios_cfg = arg;
		} else {
			add_naglog_path(arg);
		}
	}

	if (limit && print_time == print_time_duration) {
		limit++;
	}

	if (debug_level)
		print_interesting_objects();

	/* fallback for op5 systems */
	if (auth_get_user() || (!nagios_cfg && !num_nfile)) {
		struct cfg_comp *conf;

		conf = cfg_parse_file(cgi_cfg ? cgi_cfg : "/opt/monitor/etc/cgi.cfg");
		if (conf) {
			uint i;
			for (i = 0; i < conf->vars; i++) {
				struct cfg_var *v = conf->vlist[i];
				if (!nagios_cfg && !strcmp(v->key, "main_config_file")) {
					nagios_cfg = strdup(v->value);
				}
				if (!prefixcmp(v->key, "authorized_for_")) {
					auth_parse_permission(v->key, v->value);
				}
			}
			cfg_destroy_compound(conf);
		} else {
			if (cgi_cfg) {
				crash("Failed to parse cgi.cfg file '%s'\n", cgi_cfg);
			}
			if (auth_get_user()) {
				crash("--user given, but no suitable cgi.cfg file found\n");
			}
		}
	}

	if (!nagios_cfg && !num_nfile) {
		nagios_cfg = "/opt/monitor/etc/nagios.cfg";
	}
	if (nagios_cfg) {
		struct cfg_comp *conf;
		uint vi;

		conf = cfg_parse_file(nagios_cfg);
		if (!conf)
			usage("Failed to parse nagios' main config file '%s'\n", nagios_cfg);
		for (vi = 0; vi < conf->vars; vi++) {
			struct cfg_var *v = conf->vlist[vi];
			if (!strcmp(v->key, "log_file")) {
				add_naglog_path(v->value);
			}
			if (!strcmp(v->key, "log_archive_path")) {
				add_naglog_path(v->value);
			}
			if (!object_cache && !strcmp(v->key, "object_cache_file")) {
				object_cache = v->value;
			}
		}
	}

	if (!num_nfile)
		usage(NULL);

	if (auth_get_user() && object_cache) {
		auth_init(object_cache);
	}

	if (hide_state_dupes)
		state_init();

	/* make sure first_time and last_time are set */
	last_time = last_time ? last_time : time(NULL);
	first_time = first_time ? first_time : 1;

	/* flip them if the user made an error (common when reverse-importing) */
	if (last_time < first_time) {
		int temp = last_time;
		last_time = first_time;
		first_time = temp;
	}

	/* make sure we always have last_ltime */
	last_ltime = first_time;

	if (reverse_parse_files)
		qsort(nfile, num_nfile, sizeof(*nfile), nfile_rev_cmp);
	else
		qsort(nfile, num_nfile, sizeof(*nfile), nfile_cmp);

	for (i = 0; i < num_nfile; i++) {
		struct naglog_file *nf = &nfile[i];
		time_t last; /* last possible timestamp in current file */

		if (reverse_parse_files)
			last = i ? nfile[i - 1].first : time(NULL);
		else
			last = i + 1 < num_nfile ? nfile[i + 1].first : time(NULL);

		if (first_time > last || last_time < nf->first) {
			continue;
		}

		cur_file = nf;
		debug("showing %s (%lu : %u)\n", nf->path, nf->first, nf->cmp);
		tot_lines += line_no;
		line_no = 0;
		if (list_files) {
			printf("%s\n", nf->path);
		} else {
			lparse_path_real(reverse_parse_files, nf->path, nf->size, parse_line);
		}
	}

	if (print_time == print_time_duration) {
		/* duration should be calculated til the end of the period */
		ltime = last_time;
		printed_lines++;
		print_line(0, NULL, 0);
	}

	if (!count) {
		if (show_ltime_skews) {
			printf("%llu ltime skews in %llu lines. %f%%\n",
				   ltime_skews, tot_lines,
				   ((float)ltime_skews / (float)tot_lines) * 100);
		}

		if (warnings && debug_level)
			fprintf(stderr, "Total warnings: %d\n", warnings);

		print_unhandled_events();
	}

	exit_nicely(0);
	return 0;
}
