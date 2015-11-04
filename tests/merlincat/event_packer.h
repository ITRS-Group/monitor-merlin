#ifndef TOOLS_MERLINCAT_EVENT_PACKER_H_
#define TOOLS_MERLINCAT_EVENT_PACKER_H_

#include <glib.h>
#include <shared/node.h>
#include <naemon/naemon.h>

/**
 * Takes a merlin event struct, and resturns a newly allocated string containing
 * the event in a text-friendly parseable format.
 *
 * The returned string should be freed with free
 */
char *event_packer_pack(const merlin_event *evt);

/**
 * Takes a merlin event struct, and resturns a newly allocated kvvec containing
 * all fields in the merlin_event struct, as a kvvec
 *
 * If name is not null, *name will point to a constant internal string with the
 * name of the event. It should not be freed
 *
 * The kvvec should be freed with kvvec_destroy(kvv, KVVEC_FREE_ALL);
 */
struct kvvec *event_packer_pack_kvv(const merlin_event *evt, const char **name);

/**
 * Takes a line representing the an merlin_event as a line as outputted from
 * event_packer_pack, and resturns a newly allocated merlin_event
 *
 * The returned merlin_event should be freed with free
 */
merlin_event *event_packer_unpack(const char *line);

/**
 * Takes a name of an event and return the corresponding packet type.
 *
 * Useful for searching filtering packets by type
 */
uint16_t event_packer_str_to_type(const char *typestr);

/**
 * Takes an event type, as string, and a kvvec of parameters, and returns a
 * newly allocated merlin_event, that should be freed with free()
 *
 * If any failure, return NULL
 */
merlin_event *event_packer_unpack_kvv(const char *cmd, struct kvvec *kvv);

#endif /* TOOLS_MERLINCAT_EVENT_PACKER_H_ */
