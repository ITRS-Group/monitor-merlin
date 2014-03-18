#include "shared.h"
#include "hookinfo.h"
#include "nebcallback.pb-c.h"
#include "codec.h"
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

static void
Timeval_assign(Timeval *timeval, struct timeval tv)
{
	timeval__init(timeval);
	timeval->sec = tv.tv_sec;
	timeval->usec = tv.tv_usec;
}

ContactNotificationDataMessage *
contact_notification_data_create(nebstruct_contact_notification_data *ds, uint32_t selection) {
	ContactNotificationData *ev = calloc(1, sizeof(ContactNotificationData));
	if (!ev) {
		lerr("Memory allocation error");
		return NULL;
	}

	contact_notification_data__init(ev);
	merlin_header__init(ev->header);
	ev->header->selection = selection;

	neb_callback_header__init(ev->neb_header);
	ev->neb_header->type = ds->type;
	ev->neb_header->flags = ds->flags;
	ev->neb_header->attr = ds->attr;
	Timeval_assign(ev->neb_header->timestamp, ds->timestamp);

	ev->notification_type = ds->notification_type;
	Timeval_assign(ev->start_time, ds->start_time);
	Timeval_assign(ev->end_time, ds->end_time);
	ev->host_name = ds->host_name;
	ev->service_description = ds->service_description;
	ev->contact_name = ds->contact_name;
	ev->reason_type = ds->reason_type;
	ev->state = ds->state;
	ev->output = ds->output;
	ev->ack_author = ds->ack_author;
	ev->ack_data = ds->ack_data;
	ev->escalated = ds->escalated;
	return ev;
}

void
contact_notification_data_destroy(ContactNotificationDataMessage *ev)
{
	free(ev);
}
