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
static int
__blockify(void *ds, off_t offset, int strings, off_t *ptrs, char *buf, int buflen)
{
	int i;
	int slen[5] = { 0, 0, 0, 0, 0 }; /* max number of strings is 5 */
	size_t strsize = 0;
	int len;
	char *sp; /* source pointer */
	off_t off = offset; /* offset relative to start of chunk */

	if(!ds)
		return 0;

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

		ldebug("1: sp [%p] = '%s'", sp, sp);
		slen[i] = strlen(sp);
		strsize += slen[i];
	}

	/* calculate the buffer-size we need */
	len = offset + strsize + strings + 1;
	if (len > buflen)
		return -1;

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

		/* copy the string to its new home, including nul-terminator */
		ldebug("2: i: %d; Copying [%p] '%s' to %p (offset %lu)",
			   i, sp, sp, buf + off, off);
		memcpy(buf + off, sp, slen[i] + 1);
		ldebug("2: i: %d; off [%lu] buf + off [%p] = '%s'", i, off, buf + off, buf + off);

		/* set the pointer to its relative offset */
		memcpy(buf + ptrs[i], &off, sizeof(char *));

		/* inc the offset for the next string */
		off += slen[i] + 1;
	}

	/* debug-run */
	for (i = 0; i < strings; i++) {
		memcpy(&off, buf + ptrs[i], sizeof(&off));
		sp = buf + off;
		ldebug("3: i = %d", i);
		if (!off) {
			ldebug("3: null-string");
			continue;
		}
		if (off > buflen) {
			ldebug("3: offset (%lu) out of bounds ( > %d)", off, buflen);
			continue;
		}

		ldebug("3: off: [%lu] sp [%p] = '%s'", off, sp, off ? sp : "(null)");
	}

	return len;
}


int blockify(void *data, int cb_type, char *buf, int buflen)
{
	if (cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	return __blockify(data, hook_info[cb_type].offset,
					  hook_info[cb_type].strings, hook_info[cb_type].ptrs,
					  buf, buflen);
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
