#include <stddef.h>

/**
 * Copies at most n bytes from buffer src to destination dest, while
 * unescaping any escaped newlines. This is a binary method - if you want
 * the output to be NULL terminated, make sure the input is (and that n is
 * large enough to include it).
 *
 * Returns the length of dest, which can be shorter than src, but never longer.
 */
int unescape_newlines(char *dest, const char *src, size_t n);
