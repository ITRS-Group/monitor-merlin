#include "module.h"
#include "test_utils.h"

#define T_ASSERT(pred, msg) do {\
	if ((pred)) { t_pass("%s: %s", __FUNCTION__, msg); } else { t_fail("%s: %s", __FUNCTION__, msg); } \
	} while (0)
/* extern STUBS */
merlin_node *merlin_sender = NULL;
merlin_node ipc;
struct merlin_notify_stats merlin_notify_stats[9][2][2];
time_t merlin_should_send_paths = 1;
void *neb_handle = NULL;

void set_host_check_node(merlin_node *node, host *h) {}
node_selection *node_selection_by_name(const char *name){ return NULL; }
node_selection *node_selection_by_hostname(const char *name){ return NULL; }
void set_service_check_node(merlin_node *node, service *s) {}


static merlin_event *last_decoded_event;
int ipc_send_event(merlin_event *pkt) {
	merlin_decode_event(merlin_sender, pkt);
	last_decoded_event = pkt;
	return 0;
}
int ipc_grok_var(char *var, char *val) {return 0;}
int send_paths(void) { return 0; }

int node_ctrl(merlin_node *node, int code, uint selection, void *data, uint32_t len, int msec) {
	return 0;
}

int neb_register_callback(int callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *)) {
	return 0;
}

int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *)) {
	return 0;
}
/* extern STUBS */

void test_callback_host_status() {
	time_t expected_last_check = time(NULL);
	host hst;
	memset(&hst, 0, sizeof(host));
	hst.id = 1;
	hst.name = "test-host";
	hst.last_check = expected_last_check;
	nebstruct_host_status_data ev_data;
	merlin_host_status *event_body;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_HOSTSTATUS_UPDATE;
	ev_data.flags = 0;
	ev_data.attr = 0;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &hst;
	merlin_mod_hook(NEBCALLBACK_HOST_STATUS_DATA, &ev_data);
	T_ASSERT(last_decoded_event->hdr.type == NEBCALLBACK_HOST_STATUS_DATA, "event type is left untouched");
	event_body = (merlin_host_status *)last_decoded_event->body;
	T_ASSERT(0 == strcmp(event_body->name, hst.name), "name is left untouched");
	T_ASSERT(expected_last_check == event_body->state.last_check, "last_check field is left untouched");
}


void test_callback_host_check() {
	time_t expected_last_check = time(NULL);
	time_t not_expected_last_check = 2147123099;
	host hst;
	memset(&hst, 0, sizeof(host));
	hst.id = 1;
	hst.name = "test-host";
	hst.last_check = not_expected_last_check;
	nebstruct_host_check_data ev_data;
	merlin_host_status *event_body;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_HOSTCHECK_PROCESSED;
	ev_data.flags = 0;
	ev_data.attr = 0;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &hst;
	ev_data.end_time.tv_sec =  expected_last_check;
	merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, &ev_data);
	T_ASSERT(last_decoded_event->hdr.type == NEBCALLBACK_HOST_CHECK_DATA, "event type is left untouched");
	event_body = (merlin_host_status *)last_decoded_event->body;
	T_ASSERT(0 == strcmp(event_body->name, hst.name), "name is left untouched");
	T_ASSERT(expected_last_check == event_body->state.last_check, "last_check field is updated to reflect nagios.log entry");
}

void test_callback_service_status() {
	time_t expected_last_check = time(NULL);
	service svc;
	memset(&svc, 0, sizeof(service));
	svc.id = 1;
	svc.host_name = "test-host";
	svc.description = "test-service";
	svc.last_check = expected_last_check;
	nebstruct_service_status_data ev_data;
	merlin_service_status *event_body;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_SERVICESTATUS_UPDATE;
	ev_data.flags = 0;
	ev_data.attr = 0;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &svc;
	merlin_mod_hook(NEBCALLBACK_SERVICE_STATUS_DATA, &ev_data);
	T_ASSERT(last_decoded_event->hdr.type == NEBCALLBACK_SERVICE_STATUS_DATA, "event type is left untouched");
	event_body = (merlin_service_status *)last_decoded_event->body;
	T_ASSERT(0 == strcmp(event_body->host_name, svc.host_name), "host name is left untouched");
	T_ASSERT(expected_last_check == event_body->state.last_check, "last_check field is left untouched");
}
void test_callback_service_check() {
	time_t expected_last_check = time(NULL);
	time_t not_expected_last_check = 2147123099;
	service svc;
	memset(&svc, 0, sizeof(service));
	svc.id = 1;
	svc.host_name = "test-host";
	svc.description = "test-service";
	svc.last_check = not_expected_last_check;
	nebstruct_service_check_data ev_data;
	merlin_service_status *event_body;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_SERVICECHECK_PROCESSED;
	ev_data.flags = 0;
	ev_data.attr = 0;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &svc;
	ev_data.end_time.tv_sec =  expected_last_check;
	merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, &ev_data);
	T_ASSERT(last_decoded_event->hdr.type == NEBCALLBACK_SERVICE_CHECK_DATA, "event type is left untouched");
	event_body = (merlin_service_status *)last_decoded_event->body;
	T_ASSERT(0 == strcmp(event_body->host_name, svc.host_name), "host name is left untouched");
	T_ASSERT(expected_last_check == event_body->state.last_check, "last_check field is updated to reflect nagios.log entry");
}
int main(int argc, char *argv[]) {
	t_set_colors(0);
	t_verbose = 1;

	t_start("testing neb module to daemon interface");
	test_callback_host_status();
	test_callback_host_check();
	test_callback_service_status();
	test_callback_service_check();
	t_end();
	return 0;
}
