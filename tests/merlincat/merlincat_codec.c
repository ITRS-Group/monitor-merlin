#include <shared/shared.h>
#include <shared/node.h>
#include "merlincat_codec.h"
#include <string.h>

// This is unfourtunatly needed for the naemon structs
#include <naemon/naemon.h>

/**
 * Conversion table to encode and decode data-structs sent from
 * Nagios to event broker modules.
 *
 * To add more checks here, first go check in nagios/include/nebcallbacks.h
 * to figure out the thing you want to add stuff to. Then find the
 * corresponding data struct in nagios/include/nebstructs.h and add the
 * necessary info.
 *
 * 'strings' is the number of dynamic strings (char *)
 * 'offset' is 'sizeof(nebstruct_something_data)'
 * 'ptrs[]' are the offsets of each char *. use the offsetof() macro
 */

static struct merlincat_hook_info_struct {
	int cb_type;
	int strings;
	off_t offset, ptrs[7];
} hook_info[NEBCALLBACK_NUMITEMS] = {
	{ NEBCALLBACK_PROCESS_DATA, 0, sizeof(nebstruct_process_data),
		{ 0, 0, 0, 0, 0 },
	},
	{ NEBCALLBACK_TIMED_EVENT_DATA, 0, sizeof(nebstruct_timed_event_data),
		{ 0, 0, 0, 0, 0 },
	},
	{ NEBCALLBACK_LOG_DATA, 1, sizeof(nebstruct_log_data),
		{
			offsetof(nebstruct_log_data, data),
			0, 0, 0, 0
		},
	},
	{ NEBCALLBACK_SYSTEM_COMMAND_DATA, 2, sizeof(nebstruct_system_command_data),
		{
			offsetof(nebstruct_system_command_data, command_line),
			offsetof(nebstruct_system_command_data, output),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_EVENT_HANDLER_DATA, 4, sizeof(nebstruct_event_handler_data),
		{
			offsetof(nebstruct_event_handler_data, host_name),
			offsetof(nebstruct_event_handler_data, service_description),
			offsetof(nebstruct_event_handler_data, command_line),
			offsetof(nebstruct_event_handler_data, output),
			0
		},
	},
	{ NEBCALLBACK_NOTIFICATION_DATA, 5, sizeof(nebstruct_notification_data),
		{
			offsetof(nebstruct_notification_data, host_name),
			offsetof(nebstruct_notification_data, service_description),
			offsetof(nebstruct_notification_data, output),
			offsetof(nebstruct_notification_data, ack_author),
			offsetof(nebstruct_notification_data, ack_data),
		},
	},
	{ NEBCALLBACK_SERVICE_CHECK_DATA, 5, sizeof(merlin_service_status),
		{
			offsetof(merlin_service_status, host_name),
			offsetof(merlin_service_status, service_description),
			offsetof(merlin_service_status, state.plugin_output),
			offsetof(merlin_service_status, state.long_plugin_output),
			offsetof(merlin_service_status, state.perf_data),
		}
	},
	{ NEBCALLBACK_HOST_CHECK_DATA, 4, sizeof(merlin_host_status),
		{
			offsetof(merlin_host_status, name),
			offsetof(merlin_host_status, state.plugin_output),
			offsetof(merlin_host_status, state.long_plugin_output),
			offsetof(merlin_host_status, state.perf_data),
			0
		},
	},
	{ NEBCALLBACK_COMMENT_DATA, 4, sizeof(nebstruct_comment_data),
		{
			offsetof(nebstruct_comment_data, host_name),
			offsetof(nebstruct_comment_data, service_description),
			offsetof(nebstruct_comment_data, author_name),
			offsetof(nebstruct_comment_data, comment_data),
			0,
		},
	},
	{ NEBCALLBACK_DOWNTIME_DATA, 4, sizeof(nebstruct_downtime_data),
		{
			offsetof(nebstruct_downtime_data, host_name),
			offsetof(nebstruct_downtime_data, service_description),
			offsetof(nebstruct_downtime_data, author_name),
			offsetof(nebstruct_downtime_data, comment_data),
			0,
		},
	},
	{ NEBCALLBACK_FLAPPING_DATA, 2, sizeof(nebstruct_flapping_data),
		{
			offsetof(nebstruct_flapping_data, host_name),
			offsetof(nebstruct_flapping_data, service_description),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_PROGRAM_STATUS_DATA, 2, sizeof(nebstruct_program_status_data),
		{
			offsetof(nebstruct_program_status_data, global_host_event_handler),
			offsetof(nebstruct_program_status_data, global_service_event_handler),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_HOST_STATUS_DATA, 4, sizeof(merlin_host_status),
		{
			offsetof(merlin_host_status, name),
			offsetof(merlin_host_status, state.plugin_output),
			offsetof(merlin_host_status, state.long_plugin_output),
			offsetof(merlin_host_status, state.perf_data),
			0
		},
	},
	{ NEBCALLBACK_SERVICE_STATUS_DATA, 5, sizeof(merlin_service_status),
		{
			offsetof(merlin_service_status, host_name),
			offsetof(merlin_service_status, service_description),
			offsetof(merlin_service_status, state.plugin_output),
			offsetof(merlin_service_status, state.long_plugin_output),
			offsetof(merlin_service_status, state.perf_data),
		}
	},
	{ NEBCALLBACK_ADAPTIVE_PROGRAM_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_HOST_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_SERVICE_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_EXTERNAL_COMMAND_DATA, 2, sizeof(nebstruct_external_command_data),
		{
			offsetof(nebstruct_external_command_data, command_string),
			offsetof(nebstruct_external_command_data, command_args),
			0, 0, 0
		},
	},
	{ NEBCALLBACK_AGGREGATED_STATUS_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_RETENTION_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_CONTACT_NOTIFICATION_DATA, 6, sizeof(nebstruct_contact_notification_data),
		{
			offsetof(nebstruct_contact_notification_data, host_name),
			offsetof(nebstruct_contact_notification_data, service_description),
			offsetof(nebstruct_contact_notification_data, contact_name),
			offsetof(nebstruct_contact_notification_data, output),
			offsetof(nebstruct_contact_notification_data, ack_author),
			offsetof(nebstruct_contact_notification_data, ack_data),
		},
	},
	{ NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA, 7, sizeof(nebstruct_contact_notification_method_data),
		{
			offsetof(nebstruct_contact_notification_method_data, host_name),
			offsetof(nebstruct_contact_notification_method_data, service_description),
			offsetof(nebstruct_contact_notification_method_data, contact_name),
			offsetof(nebstruct_contact_notification_method_data, command_name),
			offsetof(nebstruct_contact_notification_method_data, output),
			offsetof(nebstruct_contact_notification_method_data, ack_author),
			offsetof(nebstruct_contact_notification_method_data, ack_data),
		},
	},
	{ NEBCALLBACK_ACKNOWLEDGEMENT_DATA, 4, sizeof(nebstruct_acknowledgement_data),
		{
			offsetof(nebstruct_acknowledgement_data, host_name),
			offsetof(nebstruct_acknowledgement_data, service_description),
			offsetof(nebstruct_acknowledgement_data, author_name),
			offsetof(nebstruct_acknowledgement_data, comment_data),
			0
		},
	},
	{ NEBCALLBACK_STATE_CHANGE_DATA, 3, sizeof(nebstruct_statechange_data),
		{
			offsetof(nebstruct_statechange_data, host_name),
			offsetof(nebstruct_statechange_data, service_description),
			offsetof(nebstruct_statechange_data, output),
			0, 0
		},
	},
	{ NEBCALLBACK_CONTACT_STATUS_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
	{ NEBCALLBACK_ADAPTIVE_CONTACT_DATA, 0, 0, { 0, 0, 0, 0, 0 }, },
};

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
int merlincat_encode(gpointer data, int cb_type, gpointer buf, gsize buflen)
{
	int i, len, num_strings;
	off_t offset, *ptrs;

	if (cb_type == RUNCMD_PACKET && data != NULL) {
		offset = sizeof(merlin_runcmd);
		num_strings = 1;

		/* calloc to ensure we 0 initialize */
		ptrs = calloc(7,sizeof(off_t));
		ptrs[0] = offsetof(merlin_runcmd, content);
	}
	else if (!data || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return 0;
	else {
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
	}
	/*
	 * copy the base struct first. We'll overwrite the string
	 * positions later on.
	 */
	memcpy(buf, data, offset);

	for(i = 0; i < num_strings; i++) {
		char *sp = NULL;

		/* get the pointer to the real string */
		memcpy(&sp, (char *)buf + ptrs[i], sizeof(sp));
		if (!sp) {	/* NULL pointers remain NULL pointers */
			continue;
		}

		/* check this after !sp. No need to log a warning if only
		 * NULL-strings remain */
		if (buflen <= offset) {
			/*
			lwarn("No space remaining in buffer. Skipping remaining %d strings",
				  num_strings - i);
				  */
			for (; i < num_strings; i++) {
				memset(buf + ptrs[i], 0, sizeof(char *));
			}
			break;
		}
		len = strlen(sp);

		if (len > buflen - offset - 1) {
			/*
			linfo("String is too long (%d bytes, %lu remaining). Truncating",
				  len, buflen - offset - 1);
				  */
			len = buflen - offset - 1;
		}

		/* set the destination pointer */
		if (len)
			memcpy(buf + offset, sp, len);

		/* nul-terminate the string. This way we can determine
		 * the difference between NULL pointers and nul-strings */
		((char*)buf)[offset + len] = '\0';

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

	if (cb_type == RUNCMD_PACKET) {
		free(ptrs);
	}

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
int merlincat_decode(gpointer ds, gsize len, int cb_type)
{
	off_t *ptrs;
	int num_strings, i, ret = 0;

	if (cb_type == RUNCMD_PACKET && ds && len) {
		num_strings = 1;

		/* calloc to ensure we 0 initialize */
		ptrs = calloc(7,sizeof(off_t));
		ptrs[0] = offsetof(merlin_runcmd, content);
	}
	else if (!ds || !len || cb_type < 0 || cb_type >= NEBCALLBACK_NUMITEMS)
		return -1;
	else {
		num_strings = hook_info[cb_type].strings;
		ptrs = hook_info[cb_type].ptrs;
	}

	for (i = 0; i < num_strings; i++) {
		char *ptr;

		if (!ptrs[i]) {
			/*
			lwarn("!ptrs[%d]; strings == %d. Fix the hook_info struct", i, num_strings);
			*/
			continue;
		}

		/* get the relative offset */
		memcpy(&ptr, (char *)ds + ptrs[i], sizeof(ptr));

		if (!ptr) /* ignore null pointers from original struct */
			continue;

		/* make sure we don't overshoot the buffer */
		if ((off_t)ptr > len) {
			/*
			lerr("Nulling OOB ptr %u. cb: %d; type: %d; offset: %p; len: %lu; overshot with %lu bytes",
				 i, cb_type, *(int *)ds, ptr, len, (off_t)ptr - len);
			*/
			ptr = NULL;
			ret |= (1 << i);
		}
		else
			ptr += (off_t)ds;

		/* now write it back to the proper location */
		memcpy((char *)ds + ptrs[i], &ptr, sizeof(ptr));
	}
	if (cb_type == RUNCMD_PACKET) {
		free(ptrs);
	}

	return ret;
}
