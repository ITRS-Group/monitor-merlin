#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "io.h"
#include "protocol.h"
#include "logging.h"

int proto_read_event(int sock, struct merlin_event *pkt)
{
	int len, result;

	len = io_recv_all(sock, &pkt->hdr, HDR_SIZE);
	if (len != HDR_SIZE) {
		lerr("In read_event: Incomplete header read(). Expected %d, got %d",
			 HDR_SIZE, len);
		return -1;
	}

	if (pkt->hdr.protocol != MERLIN_PROTOCOL_VERSION) {
		lerr("Bad protocol version (%d, expected %d)\n",
			 pkt->hdr.protocol, MERLIN_PROTOCOL_VERSION);
		return -1;
	}

	if (pkt->hdr.type == CTRL_PACKET)
		return len;

	if (!pkt->hdr.len) {
		lerr("Non-control packet of type %d with zero size length (this should never happen)", pkt->hdr.type);
		return len;
	}

	result = io_recv_all(sock, pkt->body, pkt->hdr.len);
	if (result != pkt->hdr.len) {
		lwarn("Bogus read in proto_read_event(). got %d, expected %d",
			  result, pkt->hdr.len);
	}
	else {
		ldebug("Successfully read 1 event (%d bytes; %d bytes body) from socket %d\n",
			   HDR_SIZE + result, pkt->hdr.len, sock);
	}

	return result;
}

int proto_send_event(int sock, struct merlin_event *pkt)
{
	pkt->hdr.protocol = MERLIN_PROTOCOL_VERSION;

	if (pkt->hdr.len < 0 || packet_size(pkt) > TOTAL_PKT_SIZE) {
		ldebug("header is invalid, or packet is too large. aborting\n");
		return -1;
	}

	/*
	 * if this is a control packet, we mustn't use hdr->len
	 * to calculate the total size of the payload, or we'll
	 * send random bits of data across the link
	 */
	if (pkt->hdr.type == CTRL_PACKET)
		return io_send_all(sock, &pkt->hdr, HDR_SIZE);

	return io_send_all(sock, pkt, packet_size(pkt));
}

int proto_ctrl(int sock, int control_type, int selection)
{
	struct merlin_header hdr;

	memset(&hdr, 0, HDR_SIZE);

	hdr.type = CTRL_PACKET;
	hdr.len = control_type;
	hdr.selection = selection;

	return io_send_all(sock, &hdr, HDR_SIZE);
}
