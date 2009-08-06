#ifndef INCLUDE_io_h__
#define INCLUDE_io_h__

#include <sys/types.h>
#include <sys/poll.h>
#include <sys/socket.h>

#define io_poll_read(fd, msec) io_poll(fd, POLLIN, msec)
#define io_poll_write(fd, msec) io_poll(fd, POLLOUT, msec)
int io_poll(int fd, int events, int msec);
int io_recv_all(int fd, void *buf, size_t len);
int io_send_all(int fd, const void *buf, size_t len);

#endif /* INCLUDE_io_h__ */
