/*
 * I/O utilities
 */

#include "io.h"
#include "protocol.h"
#include "logging.h"
#include <errno.h>
#include <string.h>

int io_send_all(int fd, const void *buf, size_t len)
{
	int total = 0;

	if (!buf || !len)
		return 0;

	while (total < len) {
		int sent;

		ldebug("io_send_all: Trying send(%d, %p, %d, MSG_DONTWAIT | MSG_NOSIGNAL",
			   fd, buf + total, len - total);
		sent = send(fd, buf + total, len - total, MSG_DONTWAIT | MSG_NOSIGNAL);

		if (sent < 0) {
			ldebug("io_send_all(): send() returned %d: %s", sent, strerror(errno));
			if (errno != EAGAIN)
				return sent;

			continue;
		}

		total += sent;
	}

	return total;
}


int io_recv_all(int fd, void *buf, size_t len)
{
	int rd, total = 0;

	do {
		rd = recv(fd, buf + total, len - total, MSG_DONTWAIT | MSG_NOSIGNAL);
		total += rd;
	} while (total < len && rd > 0);

	if (rd < 0)
		return rd;

	return total;
}

int io_poll(int fd, int events, int msec)
{
	struct pollfd pfd;

	pfd.fd = fd;
	pfd.events = events;

	return poll(&pfd, 1, msec);
}
