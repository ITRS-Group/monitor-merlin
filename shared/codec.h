#include "logging.h"
#include "shared.h"

#ifndef INCLUDE_codec_h__
#define INCLUDE_codec_h__

int merlin_encode(void *data, int cb_type, char *buf, int buflen);
int merlin_decode(void *ds, off_t len, int cb_type);
static inline int merlin_encode_event(merlin_event *pkt, void *data)
{
	return merlin_encode(data, pkt->hdr.type, pkt->body, sizeof(pkt->body));
}
static inline int merlin_decode_event(merlin_node *node, merlin_event *pkt)
{
	int ret = merlin_decode(pkt->body, pkt->hdr.len, pkt->hdr.type);

	if (ret) {
		lerr("CODEC: Failed to decode packet from '%s'. type: %u (%s); code: %u; len: %u",
			 node ? node->name : "(unknown)", pkt->hdr.type, callback_name(pkt->hdr.type), pkt->hdr.code, pkt->hdr.len);
	}
	return ret;
}

#endif
