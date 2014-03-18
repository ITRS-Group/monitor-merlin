#ifndef MERLIN_CODEC_H_INCLUDED
#define MERLIN_CODEC_H_INCLUDED
#include "nebcallback.pb-c.h"

typedef ContactNotificationData ContactNotificationDataMessage;
typedef ProtobufCMessage GenericMessage;

ContactNotificationDataMessage *
contact_notification_data_create(nebstruct_contact_notification_data *, uint32_t);

void
contact_notification_data_destroy(ContactNotificationDataMessage *);
#endif
