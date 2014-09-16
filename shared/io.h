#ifndef INCLUDE_io_h__
#define INCLUDE_io_h__

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>

#define io_read_ok(fd, msec) io_poll(fd, POLLIN, msec)
#define io_write_ok(fd, msec) io_poll(fd, POLLOUT, msec)
extern int io_poll(int fd, int events, int msec);
extern int io_send_all(int fd, const void *buf, size_t len);

#endif /* INCLUDE_io_h__ */
