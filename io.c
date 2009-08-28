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
	int poll_ret, sent, total = 0;

	if (!buf || !len)
		return 0;

	poll_ret = io_poll(fd, POLLOUT, 0);
	if (poll_ret < 0)
		lerr("io_poll(%d, POLLOUT, 0) returned %d: %s", fd, poll_ret, strerror(errno));

	do {
		sent = send(fd, buf + total, len - total, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (poll_ret > 0 && sent + total == 0) {
			/* disconnected peer? */
			return 0;
		}
		if (sent < 0) {
			lerr("send(%d, (buf + total), %zu, MSG_DONTWAIT | MSG_NOSIGNAL) returned %d (%s)",
				 fd, len - total, sent, strerror(errno));
			if (errno != EAGAIN)
				return sent;

			continue;
		}

		total += sent;
	} while (total < len && sent > 0);

	if (sent < 0)
		return sent;

	return total;
}

int io_recv_all(int fd, void *buf, size_t len)
{
	int poll_ret, rd, total = 0;

	poll_ret = io_poll(fd, POLLIN, 0);
	if (poll_ret < 1)
		lerr("io_poll(%d, POLLIN, 0) returned %d: %s", fd, poll_ret, strerror(errno));

	do {
		rd = recv(fd, buf + total, len - total, MSG_DONTWAIT | MSG_NOSIGNAL);
		if (poll_ret > 0 && rd + total == 0) {
			/* disconnected peer? */
			return 0;
		}

		if (rd < 0) {
			lerr("recv(%d, (buf + total), %zu, MSG_DONTWAIT | MSG_NOSIGNAL) returned %d (%s)",
				 fd, len - total, rd, strerror(errno));
			if (errno != EAGAIN)
				return rd;

			continue;
		}
		total += rd;
	} while (total < len && rd > 0);

	if (rd < 0)
		return rd;

	return total;
}
