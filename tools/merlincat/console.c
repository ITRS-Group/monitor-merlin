#include "console.h"
#include <shared/node.h>
#include "event_packer.h"

void console_print_merlin_event(merlin_event *evt) {
	char *line = event_packer_pack(evt);
	printf("Line: %s\n", line);
	free(line);
}
