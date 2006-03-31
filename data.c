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
int blockify(void *ds, int cb_type, char *buf, int buflen)
{
	int i;
	int slen[5] = { 0, 0, 0, 0, 0 }; /* max number of strings is 5 */
	size_t strsize = 0;
	int len, strings;
	char *sp;
	off_t off, offset, *ptrs;

	if (!ds || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	offset = off = hook_info[cb_type].offset;
	strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

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


/* Undo the pointer mangling done above (well, not exactly, but the string
 * pointers will point to the location of the string in the block anyways) */
int deblockify(void *ds, off_t len, int cb_type)
{
	off_t *ptrs;
	int strings, i;

	if (!ds || !len || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

	for (i = 0; i < strings; i++) {
		char *ptr;

		if (!ptrs[i]) {
			lwarn("!ptrs[%d]; strings == %d. Fix the hook_info struct", i, strings);
			continue;
		}

		/* get the relative offset */
		memcpy(&ptr, (char *)ds + ptrs[i], sizeof(ptr));

		if (!ptr) /* ignore null pointers from original struct */
			continue;

		/* make sure we don't overshoot the buffer */
		if ((off_t)ptr > len) {
			ldebug("Nulling OOB pointer %u for type %d", i, *(int *)ds);
			ldebug("offset: %p; len: %lu; overshot with %lu bytes",
			       ptr, len, (off_t)ptr - len);

			ptr = NULL;
		}
		else
			ptr += (off_t)ds;

		/* now write it back to the proper location */
		memcpy((char *)ds + ptrs[i], &ptr, sizeof(ptr));
	}

	return 1;
}
