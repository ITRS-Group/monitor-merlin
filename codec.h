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
 * General nebstruct message data flow:
 *   nebstruct_* ->
 *   merlin_message_from_basedata() -> MerlinMessage *->
 *   merlin_message_encode() -> buffer ->
 *   (wire transmission) ->
 *   buffer -> merlin_message_decode() ->
 *   MerlinMessage * -> merlin_message_to_nebstruct() ->
 *   nebstruct_*
 */

/*
 * Message types
 */
#define MM_ContactNotificationData MERLIN_MESSAGE__TYPE__CONTACT_NOTIFICATION_DATA
#define MM_MerlinCtrlPacket MERLIN_MESSAGE__TYPE__MERLIN_CTRL_PACKET

/**
 * Converts the supplied data to a MerlinMessage of the supplied type.
 * The caller is responsible for deallocating the resulting message by
 * passing it to merlin_message_destroy()
 * NOTE: Any pointers in the resulting message are simply copied from
 * the source data (no new memory is allocated), meaning that the source
 * data must not be deallocated while the message is in use.
 * @param type The type of message to create
 * @param data The source data to use (some type of nebstruct_*)
 * @return The message, or NULL on error
 */
MerlinMessage *
merlin_message_from_basedata(MerlinMessage__Type type, void *data);

/**
 * Converts the supplied MerlinMessage to its nebstruct representation.
 * The caller is responsible for deallocating the resulting nebstruct
 * by passing it to free()
 * NOTE: Any pointers in the resulting nebstruct are simply copied from
 * the source message (no new memory is allocated), meaning that the message
 * must not be deallocated while the nebstruct is still in use.
 * @param message The message
 * @return The resulting nebstruct, or NULL on error
 */
void *
merlin_message_to_nebstruct(const MerlinMessage *message);

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
merlin_encode_message(const MerlinMessage *, unsigned char **buffer);


void merlin_message_ctrl_packet_set_code(const MerlinMessage *message, int code);
void merlin_message_set_sent(const MerlinMessage *message, struct timeval *when);
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
int32_t
merlin_message_get_selection(const MerlinMessage *);


/* CtrlPacket functions*/
/**
 * Returns the non-zero if this message is of the MERLIN_CTRL_PACKET type and
 * zero if it is not.
 * @param message The message
 * @return "bool" indicating whether the message is a MERLIN_CTRL_PACKET
 */
int
merlin_message_is_ctrl_packet(const MerlinMessage *);

int
merlin_message_ctrl_packet_code(const MerlinMessage *);

NodeInfo *
merlin_message_ctrl_packet_nodeinfo(const MerlinMessage *);

/* NodeInfo functions */
MerlinTimeval *
merlin_message_nodeinfo_start(const NodeInfo *);

unsigned char *
merlin_message_nodeinfo_config_hash(const NodeInfo *);

int64_t
merlin_message_nodeinfo_last_cfg_change(const NodeInfo *);

/* MerlinTimeval functions*/
int64_t
merlin_message_timeval_sec(const MerlinTimeval *);

int64_t
merlin_message_timeval_usec(const MerlinTimeval *);

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
