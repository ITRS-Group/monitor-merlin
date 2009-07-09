#include "shared.h"

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
