#include <string.h>
#include <stdlib.h>

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
