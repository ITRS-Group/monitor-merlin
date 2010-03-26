#include "shared.h"
#include "colors.h"
const char *red = "", *green = "", *yellow = "", *reset = "";
uint passed, failed;
static uint t_depth;
static const char *indent_str = "  ";

void t_set_colors(int force)
{
	if (force == 1 || (!force && isatty(fileno(stdout)))) {
		red = CLR_RED;
		yellow = CLR_YELLOW;
		green = CLR_GREEN;
		reset = CLR_RESET;
	}
}

void t_start(const char *fmt, ...)
{
	va_list ap;

	t_depth++;

	va_start(ap, fmt);
	printf("%s", yellow);
	vfprintf(stdout, fmt, ap);
	printf("%s", reset);
	va_end(ap);
}

void t_end(void)
{
	if (t_depth)
		t_depth--;
}

static void t_indent(int depth)
{
	uint i;
	for (i = 0; i < depth; i++) {
		printf("%s", indent_str);
	}
}

void t_pass(const char *fmt, ...)
{
	va_list ap;

	passed++;
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

void ok_int(int a, int b, const char *name)
{
	if (a == b)
		t_pass(name);
	else {
		t_fail(name);
		t_diag("%d != %d", a, b);
	}
}

void ok_uint(uint a, uint b, const char *name)
{
	if (a == b)
		t_pass(name);
	else {
		t_fail(name);
		t_diag("%u != %d", a, b);
	}
}

void ok_str(const char *a, const char *b, const char *name)
{
	if (a && b && !strcmp(a, b)) {
		t_pass(name);
	} else {
		t_fail(name);
		t_diag("'%s' != '%s'", a, b);
	}
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

void log_msg(int severity, const char *fmt, ...)
{
	va_list ap;
	int len;

	printf("[%lu] %d: ", time(NULL), severity);
	va_start(ap, fmt);
	len = vprintf(fmt, ap);
	va_end(ap);
	if (fmt[len] != '\n')
		putchar('\n');
}

int log_grok_var(char *var, char *val)
{
	return 0;
}

void log_event_count(const char *prefix, merlin_event_counter *cnt, float t)
{
	return;
}

void log_deinit(void)
{
	return;
}
