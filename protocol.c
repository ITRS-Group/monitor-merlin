#include "io.h"
#include "protocol.h"
#include "logging.h"
#include <string.h>
#include <errno.h>

#if 0
int proto_read_event(int sock, struct proto_hdr *hdr, void **buf)
{
	int len;

	*buf = NULL;

	len = io_recv_all(sock, hdr, HDR_SIZE);
	if (len != HDR_SIZE) {
		if (len < 0) {
			lerr("recv(%d, ..., %d, MSG_DONTWAIT | MSG_NOSIGNAL) failed: %s",
				 sock, HDR_SIZE, strerror(errno));
		}
		if (len) {
			lerr("In read_event: Incomplete header read(). Expected %d, got %d",
				 HDR_SIZE, len);
			return 0;
		}

		return len;
	}

	*buf = malloc(hdr->len);
	if (*buf)
		return io_recv_all(sock, *buf, HDR_SIZE + hdr->len);

	return 0;
}

int proto_send_event(int sock, struct proto_hdr *hdr, void *buf)
{
	char sb[MAX_PKT_SIZE + HDR_SIZE];

	if (hdr->type == CTRL_PACKET)
		return io_send_all(sock, hdr, HDR_SIZE);

	if (hdr->len < 0 || hdr->len > MAX_PKT_SIZE || !buf)
		return 0;

	memcpy(sb, hdr, HDR_SIZE);
	memcpy(&sb[HDR_SIZE], buf, hdr->len);

	return io_send_all(sock, sb, HDR_SIZE + hdr->len);
}
#endif

int proto_ctrl(int sock, int control_type, int selection)
{
	struct proto_hdr hdr;

	memset(&hdr, 0, HDR_SIZE);

	hdr.type = CTRL_PACKET;
	hdr.len = control_type;
	hdr.selection = selection;

	return io_send_all(sock, &hdr, HDR_SIZE);
}
