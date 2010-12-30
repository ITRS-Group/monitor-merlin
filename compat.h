#ifndef INCLUDE_compat_h__
#define INCLUDE_compat_h__

/*
 * CHAR_BIT really should be 8 on all systems we care about
 */
#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif

#ifdef __WORDSIZE
# define COMPAT_WORDSIZE __WORDSIZE
#else
# define COMPAT_WORDSIZE (sizeof(void *) * CHAR_BIT)
#endif

/* runtime detection of endianness with GNU style macros */
#define COMPAT_LITTLE_ENDIAN 1234
#define COMPAT_BIG_ENDIAN    4321

static inline unsigned int endianness(void)
{
	int i = 1;

#ifdef __BYTE_ORDER
	if (__BYTE_ORDER == COMPAT_LITTLE_ENDIAN || __BYTE_ORDER == COMPAT_BIG_ENDIAN)
		return __BYTE_ORDER;
#elif defined(_BYTE_ORDER)
	if (_BYTE_ORDER == COMPAT_LITTLE_ENDIAN || _BYTE_ORDER == COMPAT_BIG_ENDIAN)
		return _BYTE_ORDER;
#endif /* __BYTE_ORDER */

	if (((char *)&i)[0] == 1)
		return COMPAT_LITTLE_ENDIAN;

	return COMPAT_BIG_ENDIAN;
}

#ifdef NEEDS_MEMRCHR
extern void *memrchr(const void *s, int c, size_t n);
#endif

#ifdef NEEDS_ASPRINTF
#include <stdarg.h>
extern int asprintf(char **strp, const char *fmt, ...);
extern int vasprintf(char **strp, const char *fmt, va_list ap);
#endif

#endif /* INCLUDE_compat_h__ */
