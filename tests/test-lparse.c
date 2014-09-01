#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include "logutils.h"
#include "lparse.h"
#include "test_utils.h"

static struct lparse_test *lpt;

static struct lparse_test {
	char *path;
	uint lines, empty;
	int max_runtime;
	int byte_delta;
} testfiles[] = {
	{ "logs/no-lf-terminator.log", 1, 0, 1, -1 },
	{ "logs/single-read.log", 1, 0, 1, 0 },
	{ "logs/beta.log", 6114, 0, 2, 0 },
	{ "logs/nuls.log", 20002, 8, 2, 0 },
	{ NULL, 0, 0, 0, 0 },
};

void sighandler(int signum)
{
	if (signum == SIGALRM) {
		fprintf(stderr, "Failed to complete parsing %s in under %d seconds\n",
				lpt->path, lpt->max_runtime);
	}
	exit(1);
}

static uint lines, empty;
static long long bytes;
static int check_line(char *str, uint len)
{
	if (!len)
		empty++;
	else
		lines++;
	bytes += len + 1;
	return 0;
}

#define print_expected(a, b, what) \
	printf("#    expected %llu " what ", got %llu. delta %d\n", \
			   (unsigned long long)a, (unsigned long long)b, (int)(a - b));


static int use_alarm = 1;

static void test_one(int rev, struct lparse_test *t, struct stat *st)
{
	lines = bytes = empty = 0;

	if (use_alarm && t->max_runtime)
		alarm(t->max_runtime);
	lparse_path_real(rev, t->path, st->st_size, check_line);
	if (use_alarm && t->max_runtime)
		alarm(0);

	if (lines == t->lines && t->empty == empty) {
		if (bytes == st->st_size || st->st_size - bytes == t->byte_delta) {
			t_pass("%s", t->path);
			return;
		}
	}

	if (lines == t->lines)
		printf("%spass%s %s\n", yellow, reset, t->path);
	else {
		failed++;
		printf("%sFAIL%s %s\n", red, reset, t->path);
	}
	if (lines != t->lines)
		print_expected(t->lines, lines, "lines");
	if (empty != t->empty)
		print_expected(t->empty, empty, "empty lines");
	if (st->st_size != bytes && st->st_size - bytes != t->byte_delta)
		print_expected(st->st_size, bytes, "bytes");
}

static void test_all(int reverse, const char *msg)
{
	int i;

	t_start("%s", msg);
	for (i = 0; testfiles[i].path; i++) {
		struct stat st;

		lpt = &testfiles[i];

		if (stat(lpt->path, &st) < 0) {
			fprintf(stderr, "Failed to stat '%s': %s\n", lpt->path, strerror(errno));
			exit(1);
		}

		test_one(reverse, lpt, &st);
	}
	t_end();
}

static struct path_cmp_test {
	char *path;
	uint correct;
} testpaths[] = {
	{ "nagios-12-01-2002-00.log", 2002120100 },
	{ "nagios-11-30-2006-01.log", 2006113001 },
	{ "nagios-08-01-2009-00.log", 2009080100 },
	{ "nagios-12-30-2147-99.log", 2147123099 },
	/* nagios.log is a special case, as is all logfiles without a dash */
	{ "nagios.log", 1 << ((8 * (sizeof(int))) - 1) },
	{ "foo.log", 0 },
	{ NULL, 0 },
};
static void test_path_cmp(void)
{
	int i;

	t_start("testing path comparison");
	for (i = 0; testpaths[i].path; i++) {
		struct path_cmp_test *t;
		uint cmp;

		t = &testpaths[i];
		cmp = path_cmp_number(t->path);
		if (cmp == t->correct) {
			t_pass("%s parses to %u", t->path, cmp);
		} else {
			t_fail("%s should be %u, got %u", t->path, t->correct, cmp);
		}
	}
	t_end();
}

int main(int argc, char **argv)
{
	int i;

	t_set_colors(0);
	t_verbose = 1;

	signal(SIGALRM, sighandler);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--no-alarm"))
			use_alarm = 0;
	}

	t_start("testing logfile parsing and sorting");
	test_all(0, "testing forward parsing");
	test_all(1, "testing reverse parsing");
	test_path_cmp();
	return t_end();
}
