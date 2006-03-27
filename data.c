/*
 * Author: Andreas Ericsson <ae@op5.se>
 *
 * Copyright(C) 2005 OP5 AB
 * All rights reserved.
 *
 */

#include "module.h"
#include "hookinfo.h"

/*
 * Both of these conversions involve a fair deal of Black Magic.
 * If you don't understand what's happening, please don't fiddle.
 */

/*
 * Fixed-length variables are simply memcpy()'d.
 * Each string allocated is stuck in a memory area at the end of the object.
 * The pointer offset is then set to reflect the start of the string relative
 * to the beginning of the memory chunk.
 */
static void *
__blockify(void *ds, int *len, off_t offset, int strings, off_t *ptrs)
{
	int i;
	char *buf;
	int slen[5] = { 0, 0, 0, 0, 0 }; /* max number of strings is 5 */
	size_t strsize = 0;
	char *sp; /* source pointer */
	off_t off = offset; /* offset relative to start of chunk */

	if(!ds)
		return NULL;

	for(i = 0; i < strings; i++) {
		if (!ptrs[i])
			continue;

		if (ptrs[i] > offset)
			ldebug("__blockify(): About to crash on OOB copy");

		memcpy(&sp, (char *)ds + ptrs[i], sizeof(sp));

		if (!sp) {
			slen[i] = 0;
			continue;
		}

		ldebug("sp [%p] = '%s'", sp, sp);
		slen[i] = strlen(sp);
		strsize += slen[i];
	}

	/* calculate the buffer-size we need */
	*len = offset + strsize + strings + 1;

	buf = calloc(1, *len);
	if (!buf)
		return NULL;

	memcpy(buf, ds, offset);

	/* iterate again to do the copying and to set pointers to
	 * relative offsets */
	off = offset;
	for(i = 0; i < strings; i++) {
		/* get the source-pointer */
		memcpy(&sp, (char *)ds + ptrs[i], sizeof(sp));

		if (!slen[i]) {
			/* NULL pointers remain NULL pointers */
			memset(buf + off, 0, sizeof(char *));
			continue;
		}

		if (!sp)
			continue;

		/* copy the string to the new offset */
		memcpy(buf + off, sp, slen[i]);

		/* set the pointer to its relative offset */
		memcpy(buf + ptrs[i], &off, sizeof(char *));

		/* up the offset for the next string */
		off += slen[i] + 1;
	}

	return buf;
}


void *blockify(void *data, int *len, int cb_type)
{
	if (cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS) {
		*len = 0;
		return NULL;
	}

	return __blockify(data, len, hook_info[cb_type].offset,
					  hook_info[cb_type].strings, hook_info[cb_type].ptrs);
}

/*
 * Each string is strduped out of the original chunk, and the pointer is
 * set blindly to the beginning of the new string. The strings must be
 * strdup()'ed since Nagios will free() them otherwise.
 */
int deblockify(void *ds, off_t len, int cb_type)
{
	off_t *ptrs;
	int strings, i;

	if (!ds || !len || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

	/* beware the black magic */
	for (i = 0; i < strings; i++) {
		off_t offset;

		if (!ptrs[i]) {
			lwarn("!ptrs[%d]; strings == %d. Fix the hook_info struct", i, strings);
			continue;
		}

		/* get the relative offset */
		memcpy(&offset, (char *)ds + ptrs[i], sizeof(offset));

		if (!offset) /* ignore null pointers from original struct */
			continue;

		/* make sure we don't overshoot the buffer */
		if (offset > len) {
			ldebug("Nulling OOB pointer %u for type %d", i, *(int *)ds);
			ldebug("offset: %lu; len: %lu; overshot with %lu bytes",
			       offset, len, offset - len);

			offset = 0;
		}
		else
			offset += (off_t)ds;

		/* now write it back to the proper location */
		memcpy((char *)ds + ptrs[i], &offset, sizeof(offset));
	}

	return 1;
}
