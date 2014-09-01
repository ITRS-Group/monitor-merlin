#ifndef INCLUDE_test_utils_h__
#define INCLUDE_test_utils_h__
#define TEST_PASS 1
#define TEST_FAIL 0

/*
 * You might suspect test utils is test specific code.
 * You'd be wrong.
 *
 * As our tools are mostly invoked by daemons, printing their output to stdout
 * might be considered unhelpful, but I guess that's a future fix.
 */

extern const char *red, *green, *yellow, *cyan, *reset;
extern unsigned int passed, failed, t_verbose;

extern void t_set_colors(int force);
extern void t_start(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void t_pass(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void t_fail(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void t_diag(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern int ok_int(int a, int b, const char *name);
extern int ok_uint(unsigned int a, unsigned int b, const char *name);
extern int ok_str(const char *a, const char *b, const char *name);
extern int t_end(void);
extern void crash(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2), __noreturn__));
#endif
