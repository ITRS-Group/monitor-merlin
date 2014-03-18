#ifndef MERLIN_CODEC_H_INCLUDED
#define MERLIN_CODEC_H_INCLUDED
#include "nebcallback.pb-c.h"

/*
 * Type definitions
 */
typedef ContactNotificationData ContactNotificationDataMessage;
typedef ProtobufCMessage GenericMessage;
typedef MerlinHeader__Code MerlinHeaderCode;
/*
 * Generic declarations
 */
int
message_is_ctrl_packet(const GenericMessage *);

MerlinHeaderCode
message_get_code(const GenericMessage *);

/*
 * Message specific declarations
 */
ContactNotificationDataMessage *
contact_notification_data_create(nebstruct_contact_notification_data *, uint32_t);

void
contact_notification_data_destroy(ContactNotificationDataMessage *);
#endif
