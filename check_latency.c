#define _GNU_SOURCE 1 /* for vasprintf */
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cfgfile.h"

#ifndef __GNUC__
# define __attribute__(x) /* nothing */
#endif

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

typedef struct {
	uint objects;
	float min, avg, max;
} latency_values;

typedef struct {
	int parsed;
	time_t when;
	latency_values host, service;
} retention_entry;
static retention_entry ret_ent[50], current;

typedef struct {
	uint host_increase, service_increase;
	float host_maxavg, service_maxavg;
} threshold;

static threshold warning = { 5, 5, 30.0, 30.0 };
static threshold critical = { 8, 8, 60.0, 60.0 };

#define hmin current.host.min
#define hmax current.host.max
#define smin current.service.min
#define smax current.service.max
#define current_state_loaded (current.when > ret_ent[0].when)

static void fprint_latency_values(FILE *fp, latency_values *lv)
{
	fprintf(fp, "\t\tobjects = %u\n", lv->objects);
	fprintf(fp, "\t\tmin = %f\n\t\tavg = %f\n\t\tmax = %f\n\t}\n",
			lv->min, lv->avg, lv->max);
}

static void fprint_retention_entry(FILE *fp, retention_entry *re)
{
	fprintf(fp, "%lu {\n", re->when);
	fprintf(fp, "\thost {\n");
	fprint_latency_values(fp, &re->host);
	fprintf(fp, "\tservice {\n");
	fprint_latency_values(fp, &re->service);
	fprintf(fp, "}\n\n");
}

static void write_state_file(const char *path)
{
	FILE *fp;
	uint i;

	fp = fopen(path, "w");
	if (!fp)
		return;

	fprint_retention_entry(fp, &current);
	for (i = 0; i < ARRAY_SIZE(ret_ent); i++) {
		if (!ret_ent[i].parsed)
			break;
		fprint_retention_entry(fp, &ret_ent[i]);
	}
	fclose(fp);
}

static void grok_latency_values(struct cfg_comp *c, latency_values *lv)
{
	int i;

	for (i = 0; i < c->vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!strcmp(v->key, "objects"))
			lv->objects = strtoul(v->value, NULL, 0);
		else if (!strcmp(v->key, "min"))
			lv->min = strtod(v->value, NULL);
		else if (!strcmp(v->key, "avg"))
			lv->avg = strtod(v->value, NULL);
		else if (!strcmp(v->key, "max"))
			lv->max = strtod(v->value, NULL);
	}
}

static void grok_retention_entry(struct cfg_comp *c, retention_entry *re)
{
	int i;

	re->when = strtoul(c->name, NULL, 0);
	for (i = 0; i < c->nested; i++) {
		if (!strcmp(c->nest[i]->name, "host"))
			grok_latency_values(c->nest[i], &re->host);
		else if (!strcmp(c->nest[i]->name, "service"))
			grok_latency_values(c->nest[i], &re->service);
	}
	re->parsed = 1;
}

static void load_state_file(const char *path)
{
	struct cfg_comp *rfile;
	int i;

	/* it may not exist yet */
	if (access(path, R_OK) < 0)
		return;

	rfile = cfg_parse_file(path);
	for (i = 0; i < rfile->nested && i < ARRAY_SIZE(ret_ent); i++) {
		grok_retention_entry(rfile->nest[i], &ret_ent[i]);
	}
}

static int parse_latency(struct cfg_comp *c, float *latency)
{
	int i, vars = 3;

	for (i = 0; i < c->vars && vars; i++) {
		struct cfg_var *v = c->vlist[i];

		if (!strcmp(v->key, "should_be_scheduled") || !strcmp(v->key, "active_checks_enabled"))
		{
			vars--;
			if (*v->value == '0')
				return 1;
		} else if (!strcmp(v->key, "check_latency")) {
			vars--;
			*latency = (float)strtod(v->value, NULL);
		}
	}

	return 0;
}

static int get_latency(const char *path)
{
	float lat;
	struct cfg_comp *statuslog;
	struct stat st;
	uint i;
	float htot = 0.0, stot = 0.0;
	uint hosts = 0, services = 0;

	statuslog = cfg_parse_file(path);
	if (!statuslog)
		return -1;

	stat(path, &st);
	current.when = st.st_mtime;
	if (current.when <= ret_ent[0].when) {
		memcpy(&current, &ret_ent[0], sizeof(current));
		current.when = st.st_mtime;
		return 0;
	}

	for (i = 0; i < statuslog->nested; i++) {
		struct cfg_comp *c = statuslog->nest[i];

		if (!strcmp(c->name, "servicestatus")) {
			if (parse_latency(c, &lat))
				continue;

			services++;

			stot += lat;
			if (lat < smin)
				smin = lat;
			if (lat > smax)
				smax = lat;
			continue;
		}
		if (!strcmp(c->name, "hoststatus")) {
			if (parse_latency(c, &lat))
				continue;

			hosts++;

			htot += lat;
			if (lat < hmin)
				hmin = lat;
			if (lat > hmax)
				hmax = lat;
			continue;
		}
	}

	current.host.objects = hosts;
	current.host.avg = htot / hosts;
	current.service.objects = services;
	current.service.avg = stot / services;

	return 0;
}

__attribute__((__format__(__printf__, 1, 2)))
static void usage(const char *fmt, ...)
{
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(stderr, fmt, ap);
		va_end(ap);
		fputc('\n', stderr);
	}

	printf("usage: check_latency [options]\n");
	printf("  --statefile=</path/to/latency.state>  where we read and stash our state\n");
	printf("  --status-log=</path/to/status.log>    status.log file to parse\n");
	printf("  -w,--warning=<threshold string>       set warning thresholds\n");
	printf("  -c,--critical=<threshold string>      set critical thresholds\n");
	printf("\n");
	printf("A threshold string has the format\n");
	printf("      A,B,C.C,D.D\n");
	printf("  A is the times host latency is allowed to increase by 1 second or more\n");
	printf("  B does what A does, but for services\n");
	printf("  C.C the maximum allowed host latency average\n");
	printf("  D.D is what C.C does, but for services\n");
	printf("  A and B should be given as integers.\n");
	printf("  C.C and D.D should be given as floating point values\n");
	exit(EXIT_FAILURE);
}

static void grok_threshold(char *opt, threshold *th)
{
	int i = 0;

	do {
		switch (i) {
		case 0: th->host_increase = strtoul(opt, &opt, 0); break;
		case 1: th->service_increase = strtoul(opt, &opt, 0); break;
		case 2: th->host_maxavg = strtod(opt, &opt); break;
		case 3: th->service_maxavg = strtod(opt, &opt); break;
		default:
			usage("Pay attention to the threshold format, please\n");
		}
		if (opt)
			opt++;
	} while (++i < 4 && opt && *opt);
}

static char *errors[8];
static int error_idx;
__attribute__((__format__(__printf__, 1, 2)))
static void push_error(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vasprintf(&errors[error_idx++], fmt, ap);
	va_end(ap);
}

static char *warnings[8];
static int warning_idx;
__attribute__((__format__(__printf__, 1, 2)))
static void push_warning(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vasprintf(&warnings[warning_idx++], fmt, ap);
	va_end(ap);
}

static void check_thresholds(void)
{
	int i, h_check = 1, s_check = 1;
	uint h_inc = 0, s_inc = 0;
	retention_entry *h_low = NULL, *s_low = NULL;


	if (current.host.avg > ret_ent[0].host.avg)
		h_inc++;
	if (current.service.avg > ret_ent[0].service.avg)
		s_inc++;

	for (i = 0; i < ARRAY_SIZE(ret_ent) - 1; i++) {
		/*
		 * no point in parsing last entry, since we need to compare it
		 * with the one after it in the array
		 */
		if (!ret_ent[i + 1].parsed)
			break;

		if (h_check && ret_ent[i].host.avg > ret_ent[i + 1].host.avg) {
			h_inc++;
			h_low = &ret_ent[i + 1];
		} else {
			h_check = 0;
		}

		if (s_check && ret_ent[i].service.avg > ret_ent[i + 1].service.avg) {
			s_inc++;
			s_low = &ret_ent[i + 1];
		} else {
			s_check = 0;
		}

		if (s_inc == -1 && h_inc == -1)
			break;
	}

	if (h_inc && h_inc >= critical.host_increase &&
		h_low && h_low->host.avg + h_inc >= current.host.avg)
	{
		push_error("Host latency has increased the past %d times", h_inc);
	}
	if (s_inc && s_inc >= critical.service_increase &&
		s_low && s_low->service.avg + h_inc >= current.service.avg)
	{
		push_error("Service latency has increased the past %d times", s_inc);
	}
	if (current.host.avg >= critical.host_maxavg) {
		push_error("Current host latency is critically high. %f > %f",
				   current.host.avg, critical.host_maxavg);
	}
	if (current.service.avg >= critical.service_maxavg) {
		push_error("Current service latency is critically high. %f > %f",
				   current.service.avg, critical.service_maxavg);
	}

	if (h_inc && h_inc >= warning.host_increase) {
		push_warning("Host latency has increased the past %d times", h_inc);
	}
	if (s_inc && s_inc >= warning.service_increase) {
		push_warning("Service latency has increased the past %d times", s_inc);
	}
	if (current.host.avg >= warning.host_maxavg) {
		push_warning("Current host latency alarmingly high. %f > %f",
					 current.host.avg, warning.host_maxavg);
	}
	if (current.service.avg >= warning.service_maxavg) {
		push_warning("Current service latency alarmingly high. %f > %f",
					 current.service.avg, warning.service_maxavg);
	}
}

int main(int argc, char **argv)
{
	int i;
	char *state_file = NULL, *statuslog = NULL;
	int update = 1;

	for (i = 1; i < argc; i++) {
		char *arg, *opt;
		int eq_opt = 0;

		arg = argv[i];
		opt = strchr(arg, '=');
		if (opt) {
			*opt++ = '\0';
			eq_opt = 1;
		} else if (i + 1 < argc) {
			opt = argv[i + 1];			
		}

		if (!strcmp(arg, "--help") || !strcmp(arg, "-h"))
			usage(NULL);

		if (!strcmp(arg, "--no-update") || !strcmp(arg, "-n")) {
			update = 0;
			continue;
		}

		if (*arg == '-') {
			if (!opt)
				usage("Option '%s' requires an argument\n", arg);
			if (!eq_opt)
				i++;
		}

		if (!strcmp(arg, "-w") || !strcmp(arg, "--warning")) {
			grok_threshold(opt, &warning);
			continue;
		}
		if (!strcmp(arg, "-c") || !strcmp(arg, "--critical")) {
			grok_threshold(opt, &critical);
			continue;
		}
		if (!strcmp(arg, "--state-file")) {
			state_file = opt;
			continue;
		}
		if (!strcmp(arg, "--status-log")) {
			statuslog = arg;
			continue;
		}

		if (strstr(arg, "log"))
			statuslog = arg;
		else
			usage("Unknown argument: '%s'\n", arg);
	}

	if (state_file)
		load_state_file(state_file);

	if (!statuslog) {
		statuslog = "/opt/monitor/var/status.log";
	/* 	usage("No status.log file given\n"); */
	}

	smin = hmin = 100000.0;
	if (get_latency(statuslog) < 0) {
		usage("%s doesn't exist. Is op5 Monitor really running?\n", statuslog);
		return 0;
	}

	if (state_file && current_state_loaded)
		write_state_file(state_file);

	/* now we have all the data, so check against the thresholds */
	check_thresholds();
	if (error_idx) {
		printf("CRITICAL: ");
		for (i = 0; i < error_idx; i++)
			printf("%s. ", errors[i]);
	} else if (warning_idx) {
		printf("WARNING: ");
		for (i = 0; i < warning_idx; i++)
			printf("%s. ", warnings[i]);
	} else {
		printf("OK: Host latency: %.3fs. Service latency: %.3fs",
			   current.host.avg, current.service.avg);
	}

	/*
	 * we don't bother with printing the increase thing here, since
	 * performance data is used primarily to graph things anyway,
	 * so anything that does anything with it can calculate that
	 * for itself anyway
	 */
	printf("| host_avg=%f;%f;%f;0; service_avg=%f;%f;%f;0;\n",
		   current.host.avg, warning.host_maxavg, critical.host_maxavg,
		   current.service.avg, warning.service_maxavg, critical.service_maxavg);

	if (error_idx)
		return 2;
	if (warning_idx)
		return 1;

	return 0;
}
