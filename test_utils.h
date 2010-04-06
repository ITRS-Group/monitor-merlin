#ifndef INCLUDE_test_utils_h__
#define INCLUDE_test_utils_h__
typedef unsigned int uint;
extern const char *red, *green, *yellow, *reset;
extern uint passed, failed, t_verbose;
extern void t_set_colors(int force);
extern void t_pass(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void t_fail(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void t_diag(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2)));
extern void ok_int(int a, int b, const char *name);
extern void ok_uint(uint a, uint b, const char *name);
extern void ok_str(const char *a, const char *b, const char *name);
extern int t_end(void);
extern void crash(const char *fmt, ...)
	__attribute__((__format__(__printf__, 1, 2), __noreturn__));
#endif
