#include "shared.h"
#include "hookinfo.h"
#include "nebcallback.pb-c.h"
#include "merlin.pb-c.h"
#include "codec.h"
#include "mrln_logging.h"
/*
 * Both of these conversions involve a fair deal of Black Magic.
 * If you don't understand what's happening, please don't fiddle.
 *
 * For those seeking enlightenment, read on:
 * The packet must be a single continuous block of memory in order
 * to be efficiently sent over the network. In order to arrange
 * this, we must handle strings somewhat specially.
 *
 * Each string is copied to the "free" memory area beyond the rest
 * of the data contained in the object we're mangling, one* after
 * another and will a single nul char separating them. The pointers
 * to the strings are then modified so they contain the relative
 * offset from the beginning of the memory area instead of an
 * absolute memory address.  This means that in order to use those
 * strings once a packet has been encoded, it must be decoded again
 * so the string pointers are restored to an absolute address,
 * calculated based on the address of the base object and their
 * relative offset regarding that base object.
 *
 * One way to access a string inside an encoded object without
 * first running it through merlin_encode is to use:
 *
 *   str = buf->some_string + (unsigned long)buf);
 *
 * but that quickly gets unwieldy, is harder to test automagically
 * and means callers must be aware of implementation details they
 * shouldn't really have to care about, so we avoid that idiom.
 */

/*
 * Fixed-length variables are simply memcpy()'d.
 * Each string allocated is stuck in a memory area at the end of
 * the object. The pointer offset is then set to reflect the start
 * of the string relative to the beginning of the memory chunk.
 */
int merlin_encode(void *data, int cb_type, char *buf, int buflen)
{
	int i, len, num_strings;
	off_t offset, *ptrs;

	if (!data || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	/*
	 * offset points to where we should write, based off of
	 * the base location of the block we're writing into.
	 * Here, we set it to write to the first byte in pkt->body
	 * not occupied with the binary data that makes up the
	 * struct itself.
	 */
	offset = hook_info[cb_type].offset;
	num_strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

	/*
	 * copy the base struct first. We'll overwrite the string
	 * positions later on.
	 */
	memcpy(buf, data, offset);

	for(i = 0; i < num_strings; i++) {
		char *sp;

		/* get the pointer to the real string */
		memcpy(&sp, (char *)buf + ptrs[i], sizeof(sp));
		if (!sp) {	/* NULL pointers remain NULL pointers */
			continue;
		}

		/* check this after !sp. No need to log a warning if only
		 * NULL-strings remain */
		if (buflen <= offset) {
			lwarn("No space remaining in buffer. Skipping remaining %d strings",
				  num_strings - i);
			break;
		}
		len = strlen(sp);

		if (len > buflen - offset) {
			linfo("String is too long (%d bytes, %lu remaining). Truncating",
				  len, buflen - offset);
			len = buflen - offset;
		}

		/* set the destination pointer */
		if (len)
			memcpy(buf + offset, sp, len);

		/* nul-terminate the string. This way we can determine
		 * the difference between NULL pointers and nul-strings */
		buf[offset + len] = '\0';

		/* write the correct location back to the block */
		memcpy(buf + ptrs[i], &offset, sizeof(offset));

		/* increment offset pointers and decrement remaining space */
		offset += len + 1;
	}

	/*
	 * offset now holds the total length of the packet, including
	 * the last nul-terminator, regardless of how many strings we
	 * actually stashed in there.
	 */

	/* offset must be multiple of 8 to avoid memory alignment issues on SPARC */
	if (offset % 8)
		offset += 8 - offset % 8;

	return offset;
}


/*
 * Undo the pointer mangling done above (well, not exactly, but the
 * string pointers will point to the location of the string in the
 * block anyways, and thus "work").
 * Note that strings still cannot be free()'d, since the memory
 * they reside in is a single continuous block making up the entire
 * event.
 * Returns 0 on success, -1 on general (input) errors and > 0 on
 * decoding errors.
 */
int merlin_decode(void *ds, off_t len, int cb_type)
{
	off_t *ptrs;
	int num_strings, i, ret = 0;

	if (!ds || !len || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return -1;

	num_strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

	for (i = 0; i < num_strings; i++) {
		char *ptr;

		if (!ptrs[i]) {
			lwarn("!ptrs[%d]; strings == %d. Fix the hook_info struct", i, num_strings);
			continue;
		}

		/* get the relative offset */
		memcpy(&ptr, (char *)ds + ptrs[i], sizeof(ptr));

		if (!ptr) /* ignore null pointers from original struct */
			continue;

		/* make sure we don't overshoot the buffer */
		if ((off_t)ptr > len) {
			lerr("Nulling OOB ptr %u. cb: %s; type: %d; offset: %p; len: %lu; overshot with %lu bytes",
				 i, callback_name(cb_type), *(int *)ds, ptr, len, (off_t)ptr - len);

			ptr = NULL;
			ret |= (1 << i);
		}
		else
			ptr += (off_t)ds;

		/* now write it back to the proper location */
		memcpy((char *)ds + ptrs[i], &ptr, sizeof(ptr));
	}

	return ret;
}

static merlin_nodeinfo *
merlin_nodeinfo_from_ctrl_packet(MerlinCtrlPacket *message);

static nebstruct_contact_notification_data *
nebstruct_contact_notification_data_from_message(ContactNotificationData *message);

static MerlinTimeval *
merlin_timeval_create( struct timeval tv);

static NodeInfo *
nodeinfo_create(void);

#define assert_return(Assert, Return)  do { if (!Assert) return Return;} while (0);
#define MESSAGE_TYPE(T) MERLIN_MESSAGE__TYPE__ ## T
#define PB_SET(Target, Source, What) Target->What = Source->What; Target->has_ ## What = 1;
const size_t merlin_message_size(const MerlinMessage *message)
{
	return merlin_message__get_packed_size(message);
}

static MerlinMessage__Type merlin_message_type(const MerlinMessage *message)
{
	return message->type;
}

void merlin_message_set_sent(const MerlinMessage *message, struct timeval *when)
{
	message->header->sent = merlin_timeval_create(*when);
}

void merlin_message_set_selection(const MerlinMessage *message, int32_t selection)
{
	message->header->selection = selection;
	message->header->has_selection = 1;
}

void merlin_message_ctrl_packet_set_code(const MerlinMessage *message, int code)
{
	if (!message || merlin_message_is_ctrl_packet(message)) {
		return;
	}
	message->merlin_ctrl_packet->code = code;
}
int32_t merlin_message_get_selection(const MerlinMessage *message)
{
	return message->header->selection;
}

int merlin_message_is_nonet(const MerlinMessage *message)
{
	return message->header->nonet;
}

int merlin_message_is_ctrl_packet(const MerlinMessage *message)
{
	return merlin_message_type(message) == MESSAGE_TYPE(MERLIN_CTRL_PACKET);
}

int merlin_message_ctrl_packet_code(const MerlinMessage *message)
{
	assert_return(message, -1);
	assert_return(merlin_message_is_ctrl_packet(message), -1);
	return message->merlin_ctrl_packet->code;
}

NodeInfo *
merlin_message_ctrl_packet_nodeinfo(const MerlinMessage *message)
{
	assert_return(message, NULL);
	assert_return(merlin_message_is_ctrl_packet(message), NULL);
	return message->merlin_ctrl_packet->nodeinfo;
}

MerlinTimeval *
merlin_message_nodeinfo_start(const NodeInfo *nodeinfo)
{
	assert_return(nodeinfo, NULL);
	return nodeinfo->start;
}

unsigned char *
merlin_message_nodeinfo_config_hash(const NodeInfo *nodeinfo)
{
	assert_return(nodeinfo, NULL);
	return (unsigned char *) nodeinfo->config_hash;
}

int64_t
merlin_message_nodeinfo_last_cfg_change(const NodeInfo *nodeinfo)
{
	assert_return(nodeinfo, 0);
	return nodeinfo->last_cfg_change;
}

int64_t
merlin_message_timeval_sec(const MerlinTimeval *timeval)
{
	assert_return(timeval, 0);
	return timeval->sec;
}

int64_t
merlin_message_timeval_usec(const MerlinTimeval *timeval)
{
	assert_return(timeval, 0);
	return timeval->usec;
}

size_t
merlin_encode_message(const MerlinMessage *msg, unsigned char *buffer)
{
	assert_return(msg, -1);
	assert_return(buffer, -1);
	return merlin_message__pack(msg, (uint8_t *)buffer);
}

MerlinMessage *
merlin_decode_message(size_t len, const unsigned char *data)
{
	MerlinMessage *message = merlin_message__unpack(NULL, len, data);
	return message;
}

void *
merlin_message_to_payload(const MerlinMessage *message)
{
	MerlinMessage__Type type;
	void *payload = NULL;
	assert_return(message, NULL);
	type = merlin_message_type(message);
	switch (type) {
		case MESSAGE_TYPE(MERLIN_CTRL_PACKET):
			payload = merlin_nodeinfo_from_ctrl_packet(message->merlin_ctrl_packet);
			break;
		case MESSAGE_TYPE(CONTACT_NOTIFICATION_DATA):
			payload = nebstruct_contact_notification_data_from_message(message->contact_notification_data);
			break;
		default:
			lwarn("Can not convert unknown/unsupported message type %d to nebstruct", (int) type);
			break;
	}

	return payload;
}

static MerlinTimeval *
merlin_timeval_create( struct timeval tv)
{
	MerlinTimeval *timeval = calloc(1, sizeof(MerlinTimeval));
	if (!timeval) {
		lerr("Memory allocation error");
		return NULL;
	}
	merlin_timeval__init(timeval);
	timeval->sec = tv.tv_sec;
	timeval->has_sec = 1;
	timeval->usec = tv.tv_usec;
	timeval->has_usec = 1;
	return timeval;
}

static MerlinHeader *
merlin_header_create(void)
{
	MerlinHeader *header = calloc(1, sizeof(MerlinHeader));
	if (!header) {
		lerr("Memory allocation error");
		return NULL;
	}
	merlin_header__init(header);
	header->sig = MERLIN_SIGNATURE;
	header->protocol = MERLIN_PROTOCOL_VERSION;
	header->has_protocol = 1;

	return header;
}

static NebCallbackHeader *
neb_callback_header_create(void)
{
	NebCallbackHeader *header = calloc(1, sizeof(NebCallbackHeader));
	if (!header) {
		lerr("Memory allocation error");
		return NULL;
	}
	neb_callback_header__init(header);
	return header;
}

static merlin_nodeinfo *
merlin_nodeinfo_from_ctrl_packet(MerlinCtrlPacket *message)
{
	merlin_nodeinfo *info = NULL;
	assert_return(message, NULL);
	info = calloc(1, sizeof(merlin_nodeinfo));
	if (!info) {
		lerr("Memory allocation error");
		return NULL;
	}

	info->start.tv_sec = message->nodeinfo->start->sec;
	info->start.tv_usec = message->nodeinfo->start->usec;
	info->last_cfg_change = message->nodeinfo->last_cfg_change;
	strncpy((char *)info->config_hash, message->nodeinfo->config_hash, sizeof(info->config_hash));
	info->peer_id = message->nodeinfo->peer_id;
	info->active_peers = message->nodeinfo->active_peers;
	info->configured_peers = message->nodeinfo->configured_peers;
	info->active_pollers = message->nodeinfo->active_pollers;
	info->configured_pollers = message->nodeinfo->configured_pollers;
	info->active_masters = message->nodeinfo->active_masters;
	info->configured_masters = message->nodeinfo->configured_masters;
	info->host_checks_handled = message->nodeinfo->host_checks_handled;
	info->service_checks_handled = message->nodeinfo->service_checks_handled;
	return info;
}
static nebstruct_contact_notification_data *
nebstruct_contact_notification_data_from_message(ContactNotificationData *message)
{
	nebstruct_contact_notification_data *ds = NULL;
	assert_return(message, NULL);
	ds = calloc(1, sizeof(nebstruct_contact_notification_data));
	if (!ds) {
		lerr("Memory allocation error");
		return NULL;
	}
	ds->type = message->neb_header->type;
	ds->flags = message->neb_header->flags;
	ds->attr = message->neb_header->attr;
	ds->timestamp.tv_sec = message->neb_header->timestamp->sec;
	ds->timestamp.tv_usec = message->neb_header->timestamp->usec;
	ds->notification_type = message->notification_type;
	ds->start_time.tv_sec = message->start_time->sec;
	ds->start_time.tv_usec = message->start_time->usec;
	ds->end_time.tv_sec = message->end_time->sec;
	ds->end_time.tv_usec = message->end_time->usec;
	ds->host_name = message->host_name;
	ds->service_description = message->service_description;
	ds->contact_name = message->contact_name;
	ds->reason_type = message->reason_type;
	ds->state = message->state;
	ds->output = message->output;
	ds->ack_author = message->ack_author;
	ds->ack_data = message->ack_data;
	ds->escalated = message->escalated;
	return ds;
}

static ContactNotificationData *
contact_notification_data_from_nebstruct(nebstruct_contact_notification_data *ds)
{
	ContactNotificationData *message = NULL;
	assert_return(ds, NULL);
	message = calloc(1, sizeof(ContactNotificationData));
	if (!message) {
		lerr("Memory allocation error");
		return NULL;
	}

	contact_notification_data__init(message);
	message->neb_header = neb_callback_header_create();
	PB_SET(message->neb_header, ds, type);
	PB_SET(message->neb_header, ds, flags);
	PB_SET(message->neb_header, ds, attr);
	message->neb_header->timestamp = merlin_timeval_create(ds->timestamp);

	PB_SET(message, ds, notification_type);

	message->start_time = merlin_timeval_create(ds->start_time);

	message->end_time = merlin_timeval_create(ds->end_time);

	message->host_name = ds->host_name;

	message->service_description = ds->service_description;

	message->contact_name = ds->contact_name;

	PB_SET(message, ds, reason_type);
	PB_SET(message, ds, state);

	message->output = ds->output;
	message->ack_author = ds->ack_author;
	message->ack_data = ds->ack_data;
	PB_SET(message, ds, escalated);

	return message;
}

static NodeInfo *
nodeinfo_create(void)
{
	NodeInfo *message = calloc(1, sizeof(NodeInfo));
	if (!message) {
		lerr("Memory allocation error");
		return NULL;
	}

	node_info__init(message);
	return message;
}

static void
nodeinfo_destroy(NodeInfo *info)
{
	free(info->start);
	free(info);
}

static MerlinCtrlPacket *
merlin_message_ctrl_packet_from_nodeinfo(merlin_nodeinfo *info)
{

	MerlinCtrlPacket *message = NULL;
	assert_return(info, NULL);
	message = calloc(1, sizeof(MerlinCtrlPacket));
	if (!message) {
		lerr("Memory allocation error");
		return NULL;
	}

	merlin_ctrl_packet__init(message);
	message->nodeinfo = nodeinfo_create();
	message->nodeinfo->start = merlin_timeval_create(info->start);
	PB_SET(message->nodeinfo, info, last_cfg_change);
	message->nodeinfo->config_hash = (char *) info->config_hash;
	PB_SET(message->nodeinfo, info, peer_id);
	PB_SET(message->nodeinfo, info, active_peers);
	PB_SET(message->nodeinfo, info, configured_peers);
	PB_SET(message->nodeinfo, info, active_pollers);
	PB_SET(message->nodeinfo, info, configured_pollers);
	PB_SET(message->nodeinfo, info, active_masters);
	PB_SET(message->nodeinfo, info, configured_masters);
	PB_SET(message->nodeinfo, info, host_checks_handled);
	PB_SET(message->nodeinfo, info, service_checks_handled);
	return message;
}


static void
merlin_timeval_destroy(MerlinTimeval *tv)
{
	free(tv);
}

static void
merlin_header_destroy(MerlinHeader *header)
{
	merlin_timeval_destroy(header->sent);
	free(header);
}

static void
neb_callback_header_destroy(NebCallbackHeader *header)
{
	merlin_timeval_destroy(header->timestamp);
	free(header);
}

static void
merlin_ctrl_packet_destroy(MerlinCtrlPacket *message)
{
	nodeinfo_destroy(message->nodeinfo);
	free(message);
}

static void
contact_notification_data_destroy(ContactNotificationData *message)
{
	neb_callback_header_destroy(message->neb_header);
	merlin_timeval_destroy(message->start_time);
	merlin_timeval_destroy(message->end_time);
	free(message);
}

#define MERLIN_MESSAGE_DESTROY_DATA(MESSAGE, MTYPE) MTYPE ## _destroy((MESSAGE)->MTYPE);
void merlin_message_destroy(MerlinMessage *message)
{
	merlin_header_destroy(message->header);
	switch (merlin_message_type(message)) {
		case MESSAGE_TYPE(MERLIN_CTRL_PACKET):
			MERLIN_MESSAGE_DESTROY_DATA(message, merlin_ctrl_packet);
			break;
		case MESSAGE_TYPE(CONTACT_NOTIFICATION_DATA):
			MERLIN_MESSAGE_DESTROY_DATA(message, contact_notification_data);
			break;
		default:
			lwarn("Can not destroy unknown/unsupported message type %d", (int) merlin_message_type(message));
			break;
	}
	free(message);
}

#define MERLIN_MESSAGE_FILL_DATA(MESSAGE, MTYPE, DATA) (MESSAGE)->MTYPE = MTYPE ## _from_nebstruct((nebstruct_ ## MTYPE *) DATA);
MerlinMessage *
merlin_message_from_payload(MerlinMessage__Type type, void *data)
{
	MerlinMessage *message = NULL;
	if (!data) {
		return NULL;
	}
	message = calloc(1, sizeof(MerlinMessage));
	if (!message) {
		lerr("Memory allocation error");
		return NULL;
	}
	merlin_message__init(message);
	message->header = merlin_header_create();
	switch ( type ) {
		case MESSAGE_TYPE(MERLIN_CTRL_PACKET):
			message->type = MESSAGE_TYPE(MERLIN_CTRL_PACKET);
			message->merlin_ctrl_packet = merlin_message_ctrl_packet_from_nodeinfo((merlin_nodeinfo *) data);
			break;
		case MESSAGE_TYPE(CONTACT_NOTIFICATION_DATA):
			message->type = MESSAGE_TYPE(CONTACT_NOTIFICATION_DATA);
			MERLIN_MESSAGE_FILL_DATA(message, contact_notification_data, data);
			break;
		default:
			lwarn("Can not create unknown/unsupported message type %d", (int)type);
			return NULL;
	}
	return message;
}
