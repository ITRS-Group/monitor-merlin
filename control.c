/*
 * Functions for controlling nagios' behaviour. Add more as needed.
 */

#define NSCORE

#include "module.h"
#include "shared.h"
#include "hash.h"
#include "protocol.h"

static linked_item **mrm_host_list = NULL;
static linked_item **mrm_service_list = NULL;

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

	mrm_host_list = calloc(get_num_selections(), sizeof(linked_item));

	for (hst = host_list; hst; hst = hst->next) {
		int sel = hash_find_val(hst->name);

		if (sel < 0) /* not a host we care about */
			continue;

		add_linked_item(mrm_host_list, sel, hst);
	}
}

extern service *service_list;
static void create_service_lists(void)
{
	service *srv;

	linfo("Creating service object tree");

	mrm_service_list = calloc(get_num_selections(), sizeof(linked_item *));
	for (srv = service_list; srv; srv = srv->next) {
		int sel = hash_find_val(srv->host_name);

		if (sel < 0) /* not a service on a host we care about */
			continue;

		add_linked_item(mrm_service_list, sel, srv);
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


void enable_disable_checks(int selection, int enable)
{
	linked_item *list;
	int nsel;

	nsel = get_num_selections();
	if (!nsel)
		return;

	if (selection < 0 || selection >= get_num_selections()) {
		lerr("Illegal selection passed to alter_checking(): %d; max is %d",
			 selection, get_num_selections());

		return;
	}

	linfo("%sabling active checks for hosts in hostgroup '%s'",
	      enable ? "En" : "Dis", get_sel_name(selection));
	for (list = mrm_host_list[selection]; list; list = list->next_item) {
		host *hst = (host *)list->item;
		hst->checks_enabled = enable;
		hst->should_be_scheduled = enable;
	}

	linfo("%sabling active checks for services of hosts in hostgroup '%s'",
	      enable ? "En" : "Dis", get_sel_name(selection));
	for (list = mrm_service_list[selection]; list; list = list->next_item) {
		service *srv = (service *)list->item;
		srv->checks_enabled = enable;
		srv->should_be_scheduled = enable;
	}
}


void disable_all_distributed_checks(void)
{
	int i;

	for (i = 0; i <= get_num_selections(); i++)
		enable_disable_checks(i, 0);
}

void handle_control(int code, int selection)
{
	if (selection == -1)
		linfo("Received general control packet, code %d", code);
	else
		linfo("Received control packet code %d for selection '%s'",
			  code, get_sel_name(selection));

	switch (code) {
	case CTRL_INACTIVE:
	case CTRL_ACTIVE:
		enable_disable_checks(selection, code == CTRL_INACTIVE);
		break;
	default:
		lwarn("Unknown control code: %d", code);
	}
}
