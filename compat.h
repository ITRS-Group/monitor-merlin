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
#ifdef __BYTE_ORDER
	return __BYTE_ORDER;
#elif defined(_BYTE_ORDER)
	return _BYTE_ORDER;
#else
	int i = 1;

	if (((char *)&i)[0] == 1)
		return COMPAT_LITTLE_ENDIAN;

	return COMPAT_BIG_ENDIAN;
#endif /* __BYTE_ORDER */
}

#endif /* INCLUDE_compat_h__ */
