#ifndef MERLIN_CODEC_H_INCLUDED
#define MERLIN_CODEC_H_INCLUDED
#include "nebcallback.pb-c.h"

/*
 * Type definitions
 */
typedef ContactNotificationData ContactNotificationDataMessage;
typedef ProtobufCMessage GenericMessage;
typedef MerlinCtrlPacket__Code MerlinCtrlPacketCode;
/*
 * Generic declarations
 */
int
message_is_ctrl_packet(const GenericMessage *);

int
message_is_nonet(const MerlinHeader *);

/*
 * Message specific declarations
 */
MerlinCtrlPacketCode
message_get_ctrl_packet_code(const MerlinCtrlPacket *);

ContactNotificationDataMessage *
contact_notification_data_create(nebstruct_contact_notification_data *, uint32_t);

void
contact_notification_data_destroy(ContactNotificationDataMessage *);
#endif
