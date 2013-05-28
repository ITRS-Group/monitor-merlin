#include "module.h"
#include "test_utils.h"

#define T_ASSERT(pred, msg) do {\
	if ((pred)) { t_pass(msg); } else { t_fail(msg); } \
	} while (0)
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

void test_callback_host_status() {
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
	T_ASSERT(0 == 0, "zero should equal itself!");
}

int main(int argc, char *argv[]) {
	t_set_colors(0);
	t_verbose = 1;

	t_start("testing neb module to daemon interface");
	test_callback_host_status();
}
