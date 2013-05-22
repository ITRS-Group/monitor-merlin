#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <google/cmockery.h>
#include "module.h"

/* extern STUBS */
merlin_node *merlin_sender = NULL;
struct merlin_notify_stats merlin_notify_stats[9][2][2];
time_t merlin_should_send_paths = 1;
void *neb_handle = NULL;

void set_host_check_node(merlin_node *node, host *h) {}
node_selection *node_selection_by_hostname(const char *name){ return NULL; }
void set_service_check_node(merlin_node *node, service *s) {}
int send_paths(void) { return 0; }

int neb_register_callback(int callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *)) {
	return 0;
}

int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *)) {
	return 0;
}
/* extern STUBS */

void callback_host_status_test(void **state) {
	host *hst =NULL;
	nebstruct_host_check_data ev_data;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_HOSTSTATUS_UPDATE;
	ev_data.flags = 0;
	ev_data.attr = 0;
	ev_data.timestamp = tv;
	ev_data.object_ptr = (void *)hst;
	merlin_mod_hook(NEBCALLBACK_HOST_STATUS_DATA, &ev_data);
}

int main(int argc, char *argv[]) {
	const UnitTest tests[] = {
		unit_test(callback_host_status_test),
	};

	return run_tests(tests);

}
