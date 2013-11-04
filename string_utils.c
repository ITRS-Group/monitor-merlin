#include <stddef.h>
char * unescape_newlines(char *dest, const char *src, size_t n) {
	int i = 0, j = 0;
	while (i < n) {
		if (src[i] == '\\' && src[i+1] == 'n' ) {
			++i;
			dest[j] = '\n';
		}
		else {
			dest[j] = src[i];
		}
		++j;
		++i;
	}
	dest[j] = '\0';
	return dest;
}
