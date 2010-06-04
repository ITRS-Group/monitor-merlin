/*
 * Functions for controlling nagios' behaviour. Add more as needed.
 */

#include "module.h"

static time_t stall_start;

/* return number of stalling seconds remaining */
#define STALL_TIMER 20
int is_stalling(void)
{
	/* stall_start is set to 0 when we stop stalling */
	if (stall_start && stall_start + STALL_TIMER > time(NULL))
		return (stall_start + STALL_TIMER) - time(NULL);

	stall_start = 0;
	return 0;
}

/*
 * we use setter functions for these, so we can start stalling
 * after having sent the paths without having to implement the
 * way we mark and unmark stalling in multiple places
 */
void ctrl_stall_start(void)
{
	stall_start = time(NULL);
}

void ctrl_stall_stop(void)
{
	stall_start = 0;
}

/*
 * Marks a (poller) node as active or inactive
 */
static void node_set_state(int id, int state)
{
	/*
	 * we only do this if we have pollers and the id we got
	 * from the ipc socket is the id of a configured poller
	 */
	if (!num_pollers || id < 0 || id >= num_nodes)
		return;

	node_table[id]->status = state;
}

/*
 * Handles merlin control events inside the module. Control events
 * that relate to cross-host communication only never reaches this.
 */
void handle_control(merlin_event *pkt)
{
	char *sel_name, *node_name = NULL;

	if (!pkt) {
		lerr("handle_control() called with NULL packet");
		return;
	}

	sel_name = get_sel_name(pkt->hdr.selection);
	if (pkt->hdr.selection >= 0 && pkt->hdr.selection < num_nodes)
		node_name = node_table[pkt->hdr.selection]->name;
	if (node_name && (pkt->hdr.code == CTRL_INACTIVE || pkt->hdr.code == CTRL_ACTIVE)) {
		linfo("Received control packet code %d for %s node %s",
			  pkt->hdr.code,
			  node_type(node_table[pkt->hdr.selection]), node_name);
	} else {
		if (pkt->hdr.selection == CTRL_GENERIC)
			linfo("Received general control packet, code %d", pkt->hdr.code);
		else {
			if (!sel_name) {
				lwarn("Received control packet code %d for invalid selection", pkt->hdr.code);
			} else {
				linfo("Received control packet code %d for selection '%s'",
					  pkt->hdr.code, sel_name);
			}
		}
	}

	switch (pkt->hdr.code) {
	case CTRL_INACTIVE:
		node_set_state(pkt->hdr.selection, STATE_NONE);
		break;
	case CTRL_ACTIVE:
		node_set_state(pkt->hdr.selection, STATE_CONNECTED);
		break;
	case CTRL_STALL:
		ctrl_stall_start();
		break;
	case CTRL_RESUME:
		ctrl_stall_stop();
		break;
	case CTRL_STOP:
		linfo("Received (and ignoring) CTRL_STOP event. What voodoo is this?");
		break;
	default:
		lwarn("Unknown control code: %d", pkt->hdr.code);
	}
}
