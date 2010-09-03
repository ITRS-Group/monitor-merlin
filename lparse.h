#ifndef lparse_h__
#define lparse_h__
#include <stdint.h>
extern int lparse_fd(int fd, uint64_t size, int (*parse)(char *, uint));
extern int lparse_rev_fd(int fd, uint64_t size, int (*parse)(char *, uint));
extern int lparse_path_real(int rev, const char *path, uint64_t size, int (*parse)(char *, uint));
#define lparse_path(path, size, parse) lparse_path_real(0, path, size, parse)
#define lparse_rev_path(path, size, parse) lparse_path_real(1, path, size, parse)
#endif
