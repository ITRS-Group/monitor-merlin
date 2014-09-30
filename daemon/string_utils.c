#include <stddef.h>

int unescape_newlines(char *dest, const char *src, size_t n) {
	size_t i = 0, j = 0;
	while (i < n) {
		if (i == n - 1) {
			/* last byte is \, next invalid read contains n -> don't read the n */
			dest[j] = src[i];
		}
		else if (src[i] == '\\' && src[i+1] == 'n' ) {
			++i;
			dest[j] = '\n';
		}
		else if (src[i] == '\\' && src[i+1] == '\\') {
			dest[j] = src[++i];
		}
		else {
			dest[j] = src[i];
		}
		++j;
		++i;
	}
	return j;
}
