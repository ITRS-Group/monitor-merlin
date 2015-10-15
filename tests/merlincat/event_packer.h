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

#endif /* TOOLS_MERLINCAT_EVENT_PACKER_H_ */
