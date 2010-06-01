#include "shared.h"
#include "colors.h"
#include "test_utils.h"

const char *cyan = "", *red = "", *green = "", *yellow = "", *reset = "";
uint passed, failed, t_verbose = 0;
static uint t_depth;
static const char *indent_str = "  ";

void t_set_colors(int force)
{
	if (force == 1 || isatty(fileno(stdout))) {
		cyan = CLR_CYAN;
		red = CLR_RED;
		yellow = CLR_YELLOW;
		green = CLR_GREEN;
		reset = CLR_RESET;
	}
}

static void t_indent(uint depth)
{
	uint i;
	for (i = 0; i < depth; i++) {
		printf("%s", indent_str);
	}
}

void t_start(const char *fmt, ...)
{
	va_list ap;

	t_indent(t_depth++);
	va_start(ap, fmt);
	printf("%s### ", cyan);
	vfprintf(stdout, fmt, ap);
	printf("%s\n", reset);
	va_end(ap);
}

int t_end(void)
{
	if (t_depth)
		t_depth--;
	if (!t_depth || failed) {
		t_indent(t_depth);
		printf("Test results: %s%u passed%s, %s%u failed%s\n",
			   green, passed, reset, failed ? red : "", failed, failed ? reset : "");
	}

	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

void t_pass(const char *fmt, ...)
{
	va_list ap;

	passed++;
	if (!t_verbose)
		return;
	t_indent(t_depth);
	printf("%sPASS%s ", green, reset);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	putchar('\n');
}

void t_fail(const char *fmt, ...)
{
	va_list ap;

	failed++;
	t_indent(t_depth);
	printf("%sFAIL%s ", red, reset);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	putchar('\n');
}

void t_diag(const char *fmt, ...)
{
	if (fmt) {
		va_list ap;
		t_indent(t_depth + 1);
		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
		va_end(ap);
		putchar('\n');
	}
}

int ok_int(int a, int b, const char *name)
{
	if (a == b) {
		t_pass(name);
		return TEST_PASS;
	}

	t_fail(name);
	t_diag("%d != %d", a, b);
	return TEST_FAIL;
}

int ok_uint(uint a, uint b, const char *name)
{
	if (a == b) {
		return TEST_PASS;
		t_pass(name);
	}

	t_fail(name);
	t_diag("%u != %d", a, b);
	return TEST_FAIL;
}

int ok_str(const char *a, const char *b, const char *name)
{
	if ((!a && !b) || (a && b && !strcmp(a, b))) {
		t_pass(name);
		return TEST_PASS;
	}

	t_fail(name);
	t_diag("'%s' != '%s'", a, b);
	return TEST_FAIL;
}

void __attribute__((__noreturn__)) crash(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);

	exit(1);
}
