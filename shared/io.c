/*
 * I/O utilities
 */

#include "shared.h"

int io_poll(int fd, int events, int msec)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = events;

	return poll(&pfd, 1, msec);
}

int io_send_all(int fd, const void *buf, size_t len)
{
	int poll_ret, sent, loops = 0;
	size_t total = 0;

	if (!buf || !len)
		return 0;

	poll_ret = io_poll(fd, POLLOUT, 0);
	if (poll_ret < 0)
		lerr("io_poll(%d, POLLOUT, 0) returned %d: %s", fd, poll_ret, strerror(errno));

	do {
		loops++;
		sent = send(fd, buf + total, len - total, MSG_DONTWAIT);
		if (poll_ret > 0 && sent + total == 0) {
			/* disconnected peer? */
			return 0;
		}
		if (sent < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				sent = io_write_ok(fd, 100);
				continue;
			}

			lerr("send(%d, (buf + total), %zu, MSG_DONTWAIT) returned %d (%s)",
				 fd, len - total, sent, strerror(errno));
			continue;
		}

		total += sent;
	} while (total < len && sent > 0 && loops < 15);

	if (sent < 0)
		return sent;

	return total;
}
