#include "shared.h"
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
int blockify(void *data, int cb_type, char *buf, int buflen)
{
	int i, len, strings;
	off_t off, *ptrs;

	if (!data || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;

	off = hook_info[cb_type].offset;
	strings = hook_info[cb_type].strings;
	ptrs = hook_info[cb_type].ptrs;

	memcpy(buf, data, off);

	for(i = 0; i < strings; i++) {
		char *sp;

		memcpy(&sp, (char *)buf + ptrs[i], sizeof(sp));
		if (!sp) {	/* NULL pointers remain NULL pointers */
			continue;
		}

		/* check this after !sp. No need to log a warning if only
		 * NULL-strings remain */
		if (buflen <= off) {
			lwarn("No space remaining in buffer. Skipping remaining %d strings",
				  strings - i);
			break;
		}
		len = strlen(sp);

		if (len > buflen - off) {
			linfo("String is too long (%d bytes, %lu remaining). Truncating",
				  len, buflen - off);
			len = buflen - off;
		}

		/* set the destination pointer */
		if (len)
			memcpy(buf + off, sp, len);

		/* nul-terminate the string. This way we can determine
		 * the difference between NULL pointers and nul-strings */
		buf[off + len] = '\0';

		/* write the correct location back to the block */
		memcpy(buf + ptrs[i], &off, sizeof(off));

		/* increment offset pointers and decrement remaining space */
		off += len + 1;
	}

	return off;
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
			lerr("Nulling OOB ptr %u. type: %d; offset: %p; len: %lu; overshot with %lu bytes",
				 i, *(int *)ds, ptr, len, (off_t)ptr - len);

			ptr = NULL;
		}
		else
			ptr += (off_t)ds;

		/* now write it back to the proper location */
		memcpy((char *)ds + ptrs[i], &ptr, sizeof(ptr));
	}

	return 1;
}
