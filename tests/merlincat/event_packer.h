#ifndef TOOLS_MERLINCAT_EVENT_PACKER_H_
#define TOOLS_MERLINCAT_EVENT_PACKER_H_

#include <glib.h>
#include <shared/node.h>

/**
 * Takes a merlin event struct, and resturns a newly allocated string containing
 * the event in a text-friendly parseable format.
 *
 * The returned string should be freed with free
 */
char *event_packer_pack(const merlin_event *evt);

/**
 * Takes a line representing the an merlin_event as a line as outputted from
 * event_packer_pack, and resturns a newly allocated merlin_event
 *
 * The returned merlin_event should be freed with free
 */
merlin_event *event_packer_unpack(const char *line);

/**
 * Takes an event type, as string, and a kvvec of parameters, and returns a
 * newly allocated merlin_event, that should be freed with free()
 *
 * If any failure, return NULL
 */
merlin_event *event_packer_unpack_kvv(const char *cmd, struct kvvec *kvv);

#endif /* TOOLS_MERLINCAT_EVENT_PACKER_H_ */
