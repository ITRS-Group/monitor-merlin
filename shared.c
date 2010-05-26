#include "shared.h"

/** global variables present in both daemon and module **/
int debug = 0;  /* doesn't actually do anything right now */
int is_module = 1; /* the daemon sets this to 0 immediately */
int pulse_interval = 15;
int use_database = 0;
uint num_nocs = 0, num_peers = 0, num_pollers = 0;

#ifndef ISSPACE
# define ISSPACE(c) (c == ' ' || c == '\t')
#endif

char *next_word(char *str)
{
	while (!ISSPACE(*str))
		str++;

	while (ISSPACE(*str) || *str == ',')
		str++;

	if (*str)
		return str;

	return NULL;
}

#define CB_ENTRY(s) #s
static const char *callback_names[NEBCALLBACK_NUMITEMS] = {
	CB_ENTRY(RESERVED0),
	CB_ENTRY(RESERVED1),
	CB_ENTRY(RESERVED2),
	CB_ENTRY(RESERVED3),
	CB_ENTRY(RESERVED4),
	CB_ENTRY(RAW_DATA),
	CB_ENTRY(NEB_DATA),
	CB_ENTRY(PROCESS_DATA),
	CB_ENTRY(TIMED_EVENT_DATA),
	CB_ENTRY(LOG_DATA),
	CB_ENTRY(SYSTEM_COMMAND_DATA),
	CB_ENTRY(EVENT_HANDLER_DATA),
	CB_ENTRY(NOTIFICATION_DATA),
	CB_ENTRY(SERVICE_CHECK_DATA),
	CB_ENTRY(HOST_CHECK_DATA),
	CB_ENTRY(COMMENT_DATA),
	CB_ENTRY(DOWNTIME_DATA),
	CB_ENTRY(FLAPPING_DATA),
	CB_ENTRY(PROGRAM_STATUS_DATA),
	CB_ENTRY(HOST_STATUS_DATA),
	CB_ENTRY(SERVICE_STATUS_DATA),
	CB_ENTRY(ADAPTIVE_PROGRAM_DATA),
	CB_ENTRY(ADAPTIVE_HOST_DATA),
	CB_ENTRY(ADAPTIVE_SERVICE_DATA),
	CB_ENTRY(EXTERNAL_COMMAND_DATA),
	CB_ENTRY(AGGREGATED_STATUS_DATA),
	CB_ENTRY(RETENTION_DATA),
	CB_ENTRY(CONTACT_NOTIFICATION_DATA),
	CB_ENTRY(CONTACT_NOTIFICATION_METHOD_DATA),
	CB_ENTRY(ACKNOWLEDGEMENT_DATA),
	CB_ENTRY(STATE_CHANGE_DATA),
	CB_ENTRY(CONTACT_STATUS_DATA),
	CB_ENTRY(ADAPTIVE_CONTACT_DATA)
};

const char *callback_name(int id)
{
	if (id < 0 || id > NEBCALLBACK_NUMITEMS - 1)
		return "(invalid/unknown)";

	return callback_names[id];
}

static const char *config_key_expires(const char *var)
{
	if (!strcmp(var, "mode"))
		return "2009-10";
	if (!strcmp(var, "ipc_debug_write"))
		return "2011-05";
	if (!strcmp(var, "ipc_debug_read"))
		return "2011-05";

	return NULL;
}

/* converts huge values to a more humanfriendly byte-representation */
const char *human_bytes(uint64_t n)
{
	const char *suffix = "KMGTP";
	static char tbuf[2][30];
	static int t = 0;
	int shift = 1;

	t ^= 1;
	if (n < 1024) {
		sprintf(tbuf[t], "%llu bytes", n);
		return tbuf[t];
	}

	while (n >> (shift * 10) > 1024 && shift < sizeof(suffix) - 1)
		shift++;

	sprintf(tbuf[t], "%0.2f %ciB",
			(float)n / (float)(1 << (shift * 10)), suffix[shift - 1]);

	return tbuf[t];
}

linked_item *add_linked_item(linked_item *list, void *item)
{
	struct linked_item *entry = malloc(sizeof(linked_item));

	if (!entry) {
		lerr("Failed to malloc(%zu): %s", sizeof(linked_item), strerror(errno));
		return NULL;
	}

	entry->item = item;
	entry->next_item = list;
	return entry;
}

const char *tv_delta(struct timeval *start, struct timeval *stop)
{
	static char buf[30];
	double secs;
	unsigned int days, hours, mins;

	secs = stop->tv_sec - start->tv_sec;
	days = secs / 86400;
	secs -= days * 86400;
	hours = secs / 3600;
	secs -= hours * 3600;
	mins = secs / 60;
	secs -= mins * 60;

	/* add the micro-seconds */
	secs = ((secs * 1000000) + (stop->tv_usec - start->tv_usec)) / 1000000;

	if (!mins && !hours && !days) {
		sprintf(buf, "%.3lfs", secs);
	} else if (!hours && !days) {
		sprintf(buf, "%um %.3lfs", mins, secs);
	} else if (!days) {
		sprintf(buf, "%uh %um %.3lfs", hours, mins, secs);
	} else {
		sprintf(buf, "%ud %uh %um %.3lfs", days, hours, mins, secs);
	}

	return buf;
}

int grok_common_var(struct cfg_comp *config, struct cfg_var *v)
{
	const char *expires;

	if (!strcmp(v->key, "pulse_interval")) {
		pulse_interval = (unsigned)strtoul(v->value, NULL, 10);
		if (!pulse_interval) {
			cfg_warn(config, v, "Illegal pulse_interval. Using default.");
			pulse_interval = 15;
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

	if (!prefixcmp(v->key, "log_")) {
		if (!log_grok_var(v->key, v->value))
			cfg_error(config, v, "Failed to grok logging option");

		return 1;
	}

	return 0;
}

/*
 * Beef up the send and receive buffers of the sockets we work on
 */
int set_socket_buffers(int sd)
{
	int optval = 128 << 10; /* 128KB */

	setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &optval, sizeof(int));
	setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &optval, sizeof(int));

	/*
	 * we also set it to non-blocking mode, although that's not
	 * strictly speaking necessary
	 */
	if (fcntl(sd, F_SETFL, O_NONBLOCK) < 0)
		lwarn("ipc: fcntl(sock, F_SEFTL, O_NONBLOCKING) failed");

	return 0;
}
