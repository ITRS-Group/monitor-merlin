#include <stddef.h>

/**
 * copies the first n bytest of buffer src to destination dest, while
 * unescaping any escaped newlines. The resulting buffer in dest is
 * null-terminated.
 */
char * unescape_newlines(char *dest, const char *src, size_t n);
