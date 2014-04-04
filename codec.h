#ifndef MERLIN_CODEC_H_INCLUDED
#define MERLIN_CODEC_H_INCLUDED
#include "nebcallback.pb-c.h"
#include "merlin.pb-c.h"
#include <nagios/nebstructs.h>
/*
 * Type definitions
 */
typedef MerlinCtrlPacket__Code MerlinCtrlPacketCode;

/*
 * Message types
 */
#define MM_ContactNotificationData MERLIN_MESSAGE__TYPE__CONTACT_NOTIFICATION_DATA

/**
 * Creates a MerlinMessage of the supplied type, using the supplied data
 * as a source.
 * @param type The type of message to create
 * @param data The source data to use (often a nebstruct_*)
 * @return The message, or NULL on error
 */
MerlinMessage *
merlin_message_create(MerlinMessage__Type type, void *data);

/**
 * Deallocates the supplied message.
 * @param message The message to destroy
 * */
void
merlin_message_destroy(MerlinMessage *message);

/**
 * Encodes a MerlinMessage into the specified buffer. The caller is
 * responsible for deallocating the buffer by passing it to
 * merlin_encoded_message_destroy.
 *
 * This function allocates enough memory for the buffer to hold the
 * entire message. As such, the buffer should NOT be allocated prior
 * to calling this function.
 *
 * @param msg The message to decode
 * @param buffer The buffer to store the encoded message
 * @return The size of the allocated buffer
 */
size_t
merlin_encode_message(const MerlinMessage *, unsigned char *buffer);

/**
 * Sets the selection for this message. Selection should be a bitwise OR
 * of the DEST_* macros defined in node.h.
 * @param message The message
 * @param selection The selection
 */
void merlin_message_set_selection(const MerlinMessage *, int32_t selection);

/**
 * Returns the currently set selection for this message.
 * @param message The message
 * @return The currently set selection
 */
int32_t merlin_message_get_selection(const MerlinMessage *);

/**
 * Returns the non-zero if this message is of the MERLIN_CTRL_PACKET type and
 * zero if it is not.
 * @param message The message
 * @return "bool" indicating whether the message is a MERLIN_CTRL_PACKET
 */
int merlin_message_is_ctrl_packet(const MerlinMessage *);
/**
 * Returns non-zero if the NONET flag is set for this message, and zero
 * if it is not.
 * @param msg The message
 * @return "bool" indicating whether the NONET flag is set
 */
int
merlin_message_is_nonet(const MerlinMessage *);

/**
 * Decodes a data buffer containing a MerlinMessage encoded with
 * merlin_encode_message. The caller is responsible for deallocating
 * the returned message by passing it to
 * merlin_message_destroy.
 * @param len The size of the buffer
 * @param data The buffer to decode
 * @return The decoded message
 */
MerlinMessage *
merlin_decode_message(size_t len, const unsigned char *data);
#endif
