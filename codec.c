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

static nebstruct_contact_notification_data *
nebstruct_contact_notification_data_from_message(ContactNotificationData *message);


#define assert_return(Assert, Return)  do { if (!Assert) return Return;} while (0);
#define MESSAGE_TYPE(T) MERLIN_MESSAGE__TYPE__ ## T
#define PB_SET(Target, Source, What) Target->What = Source->What; Target->has_ ## What = 1;
static const size_t message_size(const MerlinMessage *message)
{
	return merlin_message__get_packed_size(message);
}

static MerlinMessage__Type merlin_message_type(const MerlinMessage *message)
{
	return message->type;
}

void merlin_message_set_selection(const MerlinMessage *message, int32_t selection)
{
	message->header->selection = selection;
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
	return message->type == MESSAGE_TYPE(MERLIN_CTRL_PACKET);
}

size_t
merlin_encode_message(const MerlinMessage *msg, unsigned char **buffer)
{
	size_t bufsz = message_size(msg);
	*buffer = malloc(bufsz);
	if (!buffer) {
		lerr("Memory allocation error");
		return -1;
	}
	merlin_message__pack(msg, *buffer);
	return bufsz;
}

void
merlin_encoded_message_destroy(unsigned char *message)
{
	free(message);
}

MerlinMessage *
merlin_decode_message(size_t len, const unsigned char *data)
{
	MerlinMessage *message = merlin_message__unpack(NULL, len, data);
	return message;
}

void *
merlin_message_to_nebstruct(const MerlinMessage *message)
{
	MerlinMessage__Type type;
	void *nebstruct_data = NULL;
	assert_return(message, NULL);
	type = merlin_message_type(message);
	switch (type) {
		case MESSAGE_TYPE(CONTACT_NOTIFICATION_DATA):
			nebstruct_data = nebstruct_contact_notification_data_from_message(message->contact_notification_data);
			break;
		default:
			lwarn("Can not convert unknown/unsupported message type %d to nebstruct", (int) type);
			break;
	}

	return nebstruct_data;
}

static Timeval *
Timeval_create( struct timeval tv)
{
	Timeval *timeval = calloc(1, sizeof(Timeval));
	if (!timeval) {
		lerr("Memory allocation error");
		return NULL;
	}
	timeval__init(timeval);
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

static nebstruct_contact_notification_data *
nebstruct_contact_notification_data_from_message(ContactNotificationData *message)
{
	nebstruct_contact_notification_data *ds = NULL;
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
	message->neb_header->timestamp = Timeval_create(ds->timestamp);

	PB_SET(message, ds, notification_type);

	message->start_time = Timeval_create(ds->start_time);

	message->end_time = Timeval_create(ds->end_time);

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


static void
Timeval_destroy(Timeval *tv)
{
	free(tv);
}

static void
merlin_header_destroy(MerlinHeader *header)
{
	Timeval_destroy(header->sent);
	free(header);
}

static void
neb_callback_header_destroy(NebCallbackHeader *header)
{
	Timeval_destroy(header->timestamp);
	free(header);
}

static void
contact_notification_data_destroy(ContactNotificationData *message)
{
	neb_callback_header_destroy(message->neb_header);
	Timeval_destroy(message->start_time);
	Timeval_destroy(message->end_time);
	free(message);
}

#define MERLIN_MESSAGE_DESTROY_DATA(MESSAGE, MTYPE) MTYPE ## _destroy((MESSAGE)->MTYPE);
void merlin_message_destroy(MerlinMessage *message)
{
	merlin_header_destroy(message->header);
	switch (merlin_message_type(message)) {
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
merlin_message_from_nebstruct(MerlinMessage__Type type, void *data)
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
