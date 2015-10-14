/*
 * merlinreader.c
 *
 * This file reimplements the merlin packet encoder/decoder. This version should
 * be treated as a reference implementation for testing purposes, so changes in
 * merlin which introduces possible bugs shouldn't affect the test code.
 *
 * So don't change this file unless possible SIGSEGV or similar in the tests.
 */

#include <glib.h>
#include <shared/node.h>
#include "merlinreader.h"

struct MerlinReader_ {
	union {
		char raw[MAX_PKT_SIZE];
		merlin_event evt;
	} buffer;
	gsize bufsize;
};

MerlinReader *merlinreader_new(void) {
	MerlinReader *mr = g_malloc(sizeof(MerlinReader));
	mr->bufsize = 0;
	return mr;
}
void merlinreader_destroy(MerlinReader *mr) {
	g_free(mr);
}

gsize merlinreader_add_data(MerlinReader *mr, gpointer data, gsize size) {
	if (size > MAX_PKT_SIZE - mr->bufsize) {
		size = MAX_PKT_SIZE - mr->bufsize;
	}
	memcpy(&mr->buffer.raw[mr->bufsize], data, size);
	mr->bufsize += size;
	return size;
}
merlin_event *merlinreader_get_event(MerlinReader *mr) {
	gsize event_size;
	merlin_event *event_storage;
	/* We need at least a header to be able to read anything at all */
	if (mr->bufsize < sizeof(merlin_header))
		return NULL;

	if (mr->buffer.evt.hdr.sig.id != MERLIN_SIGNATURE) {
		/* TODO: Error handling... look for a correct header to resync? */
		printf("INVALID MERLIN HEADER\n");
	}
	event_size = mr->buffer.evt.hdr.len + sizeof(merlin_header);

	/* Is the event to short for the buffer? */
	if (mr->bufsize < event_size)
		return NULL;

	/* Tear out the event from the buffer */
	event_storage = g_memdup(mr->buffer.raw, event_size);

	mr->bufsize -= event_size;
	memmove(mr->buffer.raw, mr->buffer.raw + event_size, mr->bufsize);

	return event_storage;
}
