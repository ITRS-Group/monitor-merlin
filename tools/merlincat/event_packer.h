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
char *event_packer_pack(merlin_event *evt);

#endif /* TOOLS_MERLINCAT_EVENT_PACKER_H_ */
