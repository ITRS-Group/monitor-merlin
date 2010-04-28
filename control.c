/*
 * Functions for controlling nagios' behaviour. Add more as needed.
 */

#include "module.h"

static linked_item **mrm_host_list = NULL;
static linked_item **mrm_service_list = NULL;

static time_t stall_start;

static inline int add_linked_item(linked_item **list, int slot, void *item)
{
	struct linked_item *entry = malloc(sizeof(linked_item));

	if (!entry)
		return 0;

	entry->item = item;
	entry->next_item = list[slot];
	list[slot] = entry;

	return 1;
}

extern host *host_list;
static void create_host_lists(void)
{
	host *hst;

	linfo("Creating host object tree");

	mrm_host_list = calloc(get_num_selections(), sizeof(linked_item *));

	for (hst = host_list; hst; hst = hst->next) {
		int *sel = hash_find_val(hst->name);

		if (!sel) /* not a host we care about */
			continue;

		add_linked_item(mrm_host_list, *sel, hst);
	}
}

extern service *service_list;
static void create_service_lists(void)
{
	service *srv;

	linfo("Creating service object tree");

	mrm_service_list = calloc(get_num_selections(), sizeof(linked_item *));
	for (srv = service_list; srv; srv = srv->next) {
		int *sel = hash_find_val(srv->host_name);

		if (!sel) /* not a service on a host we care about */
			continue;

		add_linked_item(mrm_service_list, *sel, srv);
	}
}


void create_object_lists(void)
{
	/* if we're a poller we won't have any selections */
	if (!get_num_selections())
		return;

	create_host_lists();
	create_service_lists();
}


/* 
 * enables or disables active checks of all hosts and services
 * controlled by the node relating to selection id "selection"
 */
void enable_disable_checks(int selection, int enable)
{
	linked_item *list;
	int nsel;
	char *sel_name = NULL;

	nsel = get_num_selections();
	if (!nsel)
		return;

	sel_name = get_sel_name(selection);
	if (!sel_name)
		return;

	if (selection < 0 || selection >= get_num_selections()) {
		lerr("Illegal selection passed to alter_checking(): %d; max is %d",
			 selection, get_num_selections());

		return;
	}

	linfo("%sabling active checks for hosts in hostgroup '%s'",
	      enable ? "En" : "Dis", sel_name);
	for (list = mrm_host_list[selection]; list; list = list->next_item) {
		host *hst = (host *)list->item;
		hst->checks_enabled = enable;
		hst->should_be_scheduled = enable;
	}

	linfo("%sabling active checks for services of hosts in hostgroup '%s'",
	      enable ? "En" : "Dis", sel_name);
	for (list = mrm_service_list[selection]; list; list = list->next_item) {
		service *srv = (service *)list->item;
		srv->checks_enabled = enable;
		srv->should_be_scheduled = enable;
	}
}


/* return number of stalling seconds remaining */
#define STALL_TIMER 20
int is_stalling(void)
{
	/* stall_start is set to 0 when we stop stalling */
	if (stall_start && stall_start + STALL_TIMER < time(NULL))
		return time(NULL) - (stall_start + STALL_TIMER);

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
 * Handles merlin control events inside the module. Control events
 * that relate to cross-host communication only never reaches this.
 */
void handle_control(merlin_event *pkt)
{
	char *sel_name;

	if (!pkt) {
		lerr("handle_control() called with NULL packet");
		return;
	}

	sel_name = get_sel_name(pkt->hdr.selection);
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

	switch (pkt->hdr.code) {
	case CTRL_INACTIVE:
	case CTRL_ACTIVE:
		enable_disable_checks(pkt->hdr.selection, pkt->hdr.code == CTRL_INACTIVE);
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
