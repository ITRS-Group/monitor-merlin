#include <stdio.h>
#include "logging.h"
#include "shared.h"
#include "ipc.h"

/** global variables present in both daemon and module **/
int debug = 0;  /* doesn't actually do anything right now */
int is_module = 1; /* the daemon sets this to 0 immediately */
int pulse_interval = 10;
int use_database = 0;
char *merlin_config_file = NULL;
merlin_nodeinfo *self = NULL;
char *binlog_dir = NULL;

#ifndef ISSPACE
# define ISSPACE(c) (c == ' ' || c == '\t')
#endif


char *next_word(char *str)
{
	while (!ISSPACE(*str) && *str != 0)
		str++;

	while (ISSPACE(*str) || *str == ',')
		str++;

	if (*str)
		return str;

	return NULL;
}

/*
 * Crack a string into several pieces based on the delimiter
 */
strvec *str_explode(char *str, int delim)
{
	int i = 0, entries = 1;
	char *p;
	struct strvec *ret = NULL;

	if (!str || !*str)
		return NULL;

	p = str;
	while ((p = strchr(p + 1, delim))) {
		entries++;
	}

	ret = malloc(sizeof(*ret));
	ret->entries = entries;
	ret->str = malloc(entries * sizeof(char *));
	ret->str[i++] = p = str;
	while ((p = strchr(p, delim))) {
		*p++ = 0;
		ret->str[i++] = p;
	}

	return ret;
}

/*
 * "yes", "true", "on" and any non-zero integer makes us return 1
 * For every other case, we return 0.
 */
int strtobool(const char *str)
{
	int c = tolower(*str);

	if (!str || !*str)
		return 0;

	if (c == 'y' || c == 't' || c == '1')
		return 1;
	if (c == 'o' && tolower(str[1]) == 'n')
		return 1;

	return !!atoi(str);
}

/*
 * grok second-based intervals, with suffixes.
 * "1h 3m 4s" should return 3600 + 180 + 3 = 3783.
 * "0.5h 0.5m" should return 1800 + 30 = 1830
 * Subsecond precision is not possible (obviously...)
 *
 * Limited to "week" as its highest possible suffix and
 * quite clumsy and forgiving in its parsing. Since we'll
 * most likely be dealing with very short strings I don't
 * care overly about that though.
 */
int grok_seconds(const char *p, long *result)
{
	const char *real_end, suffix[] = "smhdw";
	int factors[] = { 1, 60, 3600, 3600 * 24, 3600 * 24 * 7 };
	long res = 0;

	if (!p)
		return -1;

	real_end = p + strlen(p);

	while (p && *p && p < real_end) {
		int factor;
		double val;
		char *endp, *pos;

		/* skip whitespace between one suffix and the next value */
		while (*p == ' ' || *p == '\t')
			p++;

		/* trailing whitespace in *p */
		if (!*p) {
			*result = res;
			return 0;
		}

		val = strtod(p, &endp);
		if (!val && endp == p) {
			/* invalid value */
			return -1;
		}

		/* valid value. set p to endp and look for a suffix */
		p = endp;
		while (*p == ' ' || *p == '\t')
			p++;

		/* trailing whitespace (again) */
		if (!*p) {
			res += val;
			*result = res;
			return 0;
		}

		/* if there's no suffix we just move on */
		pos = strchr(suffix, *p);
		if (!pos) {
			res += val;
			continue;
		}

		factor = pos - suffix;
		val *= factors[factor];
		res += val;

		while (*p && *p != ' ' && *p != '\t' && (*p < '0' || *p > '9'))
			p++;

		if (!*p)
			break;
	}

	*result = res;

	return 0;
}


/*
 * Returns an ISO 8601 formatted date string (YYYY-MM-DD HH:MM:SS.UUU)
 * with the desired precision.
 * Semi-threadsafe since we round-robin rotate between two buffers for
 * the return value
 */
const char *isotime(struct timeval *tv, int precision)
{
	static char buffers[2][32];
	static int bufi = 0;
	struct timeval now;
	struct tm tm;
	char *buf;
	int bufsize;
	size_t len;

	bufsize = sizeof(buffers[0]) - 1;

	if (!tv) {
		gettimeofday(&now, NULL);
		tv = &now;
	}

	buf = buffers[bufi++ % ARRAY_SIZE(buffers)];
	localtime_r(&tv->tv_sec, &tm);

	switch (precision) {
	case ISOTIME_PREC_YEAR:
		len = strftime(buf, sizeof(buffers[0]) - 1, "%Y", &tm);
		break;
	case ISOTIME_PREC_MONTH:
		len = strftime(buf, sizeof(buffers[0]) - 1, "%Y-%m", &tm);
		break;
	case ISOTIME_PREC_DAY:
		len = strftime(buf, sizeof(buffers[0]) - 1, "%F", &tm);
		break;
	case ISOTIME_PREC_HOUR:
		len = strftime(buf, sizeof(buffers[0]) - 1, "%F %H", &tm);
		break;
	case ISOTIME_PREC_MINUTE:
		len = strftime(buf, sizeof(buffers[0]) - 1, "%F %H:%M", &tm);
		break;
	case ISOTIME_PREC_SECOND:
	case ISOTIME_PREC_USECOND:
	default: /* second precision is the default */
		len = strftime(buf, sizeof(buffers[0]) - 1, "%F %H:%M:%S", &tm);
		break;
	}

	if (precision != ISOTIME_PREC_USECOND)
		return buf;
	snprintf(&buf[len], bufsize - len, ".%4lu", (unsigned long)tv->tv_usec);

	return buf;
}


/*
 * converts an arbitrarily long string of data into its
 * hexadecimal representation
 */
char *tohex(const unsigned char *data, int len)
{
	/* number of bufs must be a power of 2 */
	static char bufs[4][41], hex[] = "0123456789abcdef";
	static int bufno;
	char *buf;
	int i;

	buf = bufs[bufno & (ARRAY_SIZE(bufs) - 1)];
	for (i = 0; i < 20 && i < len; i++) {
		unsigned int val = *data++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return bufs[bufno++ & (ARRAY_SIZE(bufs) - 1)];
}

#define CB_ENTRY(s) { NEBCALLBACK_##s##_DATA, #s, sizeof(#s) - 1 }
static struct {
	int id;
	char *name;
	uint name_len;
} callback_list[NEBCALLBACK_NUMITEMS] = {
	CB_ENTRY(PROCESS),
	CB_ENTRY(TIMED_EVENT),
	CB_ENTRY(LOG),
	CB_ENTRY(SYSTEM_COMMAND),
	CB_ENTRY(EVENT_HANDLER),
	CB_ENTRY(NOTIFICATION),
	CB_ENTRY(SERVICE_CHECK),
	CB_ENTRY(HOST_CHECK),
	CB_ENTRY(COMMENT),
	CB_ENTRY(DOWNTIME),
	CB_ENTRY(FLAPPING),
	CB_ENTRY(PROGRAM_STATUS),
	CB_ENTRY(HOST_STATUS),
	CB_ENTRY(SERVICE_STATUS),
	CB_ENTRY(ADAPTIVE_PROGRAM),
	CB_ENTRY(ADAPTIVE_HOST),
	CB_ENTRY(ADAPTIVE_SERVICE),
	CB_ENTRY(EXTERNAL_COMMAND),
	CB_ENTRY(AGGREGATED_STATUS),
	CB_ENTRY(RETENTION),
	CB_ENTRY(CONTACT_NOTIFICATION),
	CB_ENTRY(CONTACT_NOTIFICATION_METHOD),
	CB_ENTRY(ACKNOWLEDGEMENT),
	CB_ENTRY(STATE_CHANGE),
	CB_ENTRY(CONTACT_STATUS),
	CB_ENTRY(ADAPTIVE_CONTACT)
};

const char *callback_name(int id)
{
	static char *num_name = NULL;
	if (id < 0 || id > NEBCALLBACK_NUMITEMS - 1) {
		if (id == CTRL_PACKET)
			return "CTRL_PACKET";
		if (num_name)
			free(num_name);
		// so, this leaks memory. Let's hope it happens very rarely, shall we?
		if (!asprintf(&num_name, "(invalid/unknown %d)", id))
			return "(invalid/unknown)";
		return num_name;
	}

	return callback_list[id].name;
}

int callback_id(const char *orig_name)
{
	uint i, len;
	char name[100];

	if (!orig_name)
		return -1;

	len = strlen(orig_name);
	if (len > sizeof(name))
		return -1;

	for (i = 0; i < len; i++) {
		name[i] = toupper(orig_name[i]);
	}
	name[i] = '\0';

	for (i = 0; i < ARRAY_SIZE(callback_list); i++) {
		if (len != callback_list[i].name_len)
			continue;

		if (!strcmp(callback_list[i].name, name)) {
			return callback_list[i].id;
		}
	}

	/* not found */
	return -1;
}

#define CTRL_ENTRY(s) "CTRL_"#s
static const char *control_names[] = {
	CTRL_ENTRY(NOTHING),
	CTRL_ENTRY(PULSE),
	CTRL_ENTRY(INACTIVE),
	CTRL_ENTRY(ACTIVE),
	CTRL_ENTRY(PATHS),
	CTRL_ENTRY(STALL),
	CTRL_ENTRY(RESUME),
	CTRL_ENTRY(STOP),
};
const char *ctrl_name(uint code)
{
	if (code >= ARRAY_SIZE(control_names))
		return "(invalid/unknown)";
	if (code == CTRL_GENERIC)
		return "CTRL_GENERIC";
	return control_names[code];
}

const char *node_state_name(int state)
{
	switch (state) {
	case STATE_NONE: return "STATE_NONE";
	case STATE_PENDING: return "STATE_PENDING";
	case STATE_NEGOTIATING: return "STATE_NEGOTIATING";
	case STATE_CONNECTED: return "STATE_CONNECTED";
	}

	return "STATE_unknown_voodoo";
}

#if (defined(__GLIBC__) && (__GLIBC__ >= 2 && __GLIBC_MINOR__ >= 1))
#include <execinfo.h>
void bt_scan(const char *mark, int count)
{
#define TRACE_SIZE 100
	char **strings;
	void *trace[TRACE_SIZE];
	int i, bt_count, have_mark = 0;

	bt_count = backtrace(trace, TRACE_SIZE);
	if (!bt_count)
		return;
	strings = backtrace_symbols(trace, bt_count);
	if (!strings)
		return;

	for (i = 0; i < bt_count; i++) {
		char *paren;

		if (mark && !have_mark) {
			if (strstr(strings[i], mark))
				have_mark = i;
			continue;
		}
		if (mark && count && i >= have_mark + count)
			break;
		paren = strchr(strings[i], '(');
		paren = paren ? paren : strings[i];
		ldebug("%2d: %s", i, paren);
	}
	free(strings);
}
#else
void bt_scan(const char *mark, int count) {}
#endif

static const char *config_key_expires(const char *var)
{
	if (!strcmp(var, "ipc_debug_write"))
		return "2011-05";
	if (!strcmp(var, "ipc_debug_read"))
		return "2011-05";

	return NULL;
}

/* converts huge values to a more humanfriendly byte-representation */
const char *human_bytes(unsigned long long n)
{
	const char *suffix = "KMGTP";
	static char tbuf[8][16];
	static int t = 0;
	unsigned int shift = 1;

	t++;
	t &= 0x7;
	if (n < 1024) {
		sprintf(tbuf[t], "%llu bytes", n);
		return tbuf[t];
	}

	while (n >> (shift * 10) > 1024 && shift < strlen(suffix) - 1)
		shift++;

	sprintf(tbuf[t], "%0.2f %ciB",
			(float)n / (float)(1L << (shift * 10)), suffix[shift - 1]);

	return tbuf[t];
}

const char *tv_delta(const struct timeval *start, const struct timeval *stop)
{
	static char buf[50];
	unsigned long weeks, days, hours, mins, secs, usecs;
	time_t stop_usec;

	secs = stop->tv_sec - start->tv_sec;
	stop_usec = stop->tv_usec;
	if (stop_usec < start->tv_usec) {
		secs--;
		stop_usec += 1000000;
	}
	usecs = stop_usec - start->tv_usec;

	/* we only want 2 decimals */
	while (usecs > 1000)
		usecs /= 1000;

	weeks = secs / 604800;
	secs -= weeks * 604800;
	days = secs / 86400;
	secs -= days * 86400;
	hours = secs / 3600;
	secs -= hours * 3600;
	mins = secs / 60;
	secs -= mins * 60;

	if (!mins && !hours && !days) {
		sprintf(buf, "%lu.%03lus", secs, usecs);
	} else if (!hours && !days) {
		sprintf(buf, "%lum %lu.%03lus", mins, secs, usecs);
	} else if (!days) {
		sprintf(buf, "%luh %lum %lu.%03lus", hours, mins, secs, usecs);
	} else if (!weeks){
		sprintf(buf, "%lud %luh %lum %lu.%03lus", days, hours, mins, secs, usecs);
	} else {
		sprintf(buf, "%luw %lud %luh %lum %lu.%03lus",
				weeks, days, hours, mins, secs, usecs);
	}

	return buf;
}

/*
 * Parse config sync options.
 *
 * This is used for each node and also in the daemon compound
 */
int grok_confsync_compound(struct cfg_comp *comp, merlin_confsync *csync)
{
	unsigned i;

	if (!comp || !csync) {
		return -1;
	}

	/*
	 * first we reset it. An empty compound in the configuration
	 * means "reset the defaults and don't bother syncing this
	 * server automagically"
	 */
	memset(csync, 0, sizeof(*csync));
	for (i = 0; i < comp->vars; i++) {
		struct cfg_var *v = comp->vlist[i];
		if (!strcmp(v->key, "push")) {
			csync->push.cmd = strdup(v->value);
			continue;
		}
		if (!strcmp(v->key, "fetch") || !strcmp(v->key, "pull")) {
			csync->fetch.cmd = strdup(v->value);
			continue;
		}
		/*
		 * we ignore additional variables here, since the
		 * config sync script may want to add additional
		 * stuff to handle
		 */
	}

	return 0;
}

static int grok_binlog_var(const char *key, const char *value)
{
	if (!strcmp(key, "binlog_dir")) {
		if (binlog_dir != NULL)
			free(binlog_dir);
		binlog_dir = strdup(value);
		return 1;
	}

	return 0;
}

int grok_common_var(struct cfg_comp *config, struct cfg_var *v)
{
	const char *expires;

	if (!strcmp(v->key, "pulse_interval")) {
		pulse_interval = (unsigned)strtoul(v->value, NULL, 10);
		if (!pulse_interval) {
			cfg_warn(config, v, "Illegal pulse_interval. Using default.");
			pulse_interval = 10;
		}
		return 1;
	}

	expires = config_key_expires(v->key);
	if (expires) {
		cfg_warn(config, v, "'%s' is a deprecated variable, scheduled for "
			 "removal at the first release after %s", v->key, expires);
		/* it's understood, in a way */
		return 1;
	}

	if (!prefixcmp(v->key, "ipc_")) {
		if (!ipc_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok IPC option");

		return 1;
	}

	if (!prefixcmp(v->key, "log_") || !strcmp(v->key, "use_syslog")) {
		if (!log_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok logging option");

		return 1;
	}

	if (!prefixcmp(v->key, "binlog_")) {
		if (!grok_binlog_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok binlog option");

		return 1;
	}
	return 0;
}

/*
 * Set some common socket options
 */
int merlin_set_socket_options(int sd, int bufsize)
{
	/*
	 * make sure random output from import programs and whatnot
	 * doesn't carry over into the net_sock
	 */
	fcntl(sd, F_SETFD, FD_CLOEXEC);

	/* make socket non-blocking */
	fcntl(sd, F_SETFL, O_NONBLOCK);

	if (bufsize) {
		if (setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(int)) < 0) {
			ldebug("Failed to set sendbuffer for %d to %d bytes: %s",
				   sd, bufsize, strerror(errno));
		}
		if (setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int)) < 0) {
			ldebug("Failed to set recvbuffer for %d to %d bytes: %s",
				   sd, bufsize, strerror(errno));
		}
	}

	return 0;
}
