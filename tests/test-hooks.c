#include "../hooks.c"
#include "../node.h"
#include <check.h>

#include <nagios/objects.h>
#include <nagios/comments.h>

squeue_t *nagios_squeue;
struct object_count num_objects = {0,};
comment *comment_list = NULL;
hostgroup *hostgroup_list = NULL;
struct timeperiod **timeperiod_ary;
struct host **host_ary;
char *config_file_dir = NULL;
char *config_file = NULL;
char *temp_path = NULL;
iobroker_set *nagios_iobs = NULL;
int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;
unsigned long   event_broker_options = BROKER_NOTHING;
int      sigshutdown = FALSE;
int interval_length = 60;
time_t event_start = 0L;
int service_check_timeout = 0;
int host_check_timeout = 0;

int process_check_result(check_result *cr){ return 0; }
struct host *find_host(const char *name) { return NULL; }
struct hostgroup *find_hostgroup(const char *name) { return NULL; }
struct service *find_service(const char *host_name, const char *service_description) { return NULL; }
struct comment *get_first_comment_by_host(char *host) { return NULL; }
int delete_comment(int type, unsigned long comment_id) { return 0; }
int delete_downtime_by_hostname_service_description_start_time_comment(char *hostname, char *service_description, time_t start_time, char *cmnt) { return 0; }
int init_check_result(check_result *cr) { return 0; }
int process_external_command2(int cmd, time_t entry_time, char *args) { return 0; }
int add_new_comment(int type, int entry_type, char *host_name, char *svc_description, time_t entry_time, char *author_name, char *comment_data, int persistent, int source, int expires, time_t expire_time, unsigned long *comment_id) { return 0; }
timed_event *schedule_new_event(int event_type, int high_priority, time_t run_time, int recurring, unsigned long event_interval, void *timing_func, int compensate_for_time_change, void *event_data, void *event_args, int event_options) {
	timed_event *evt = calloc(1, sizeof(timed_event));
	evt->event_args = event_args;
	return evt;
}
nagios_macros *get_global_macros() { return NULL; }
void fcache_timeperiod(FILE *fp, struct timeperiod *temp_timeperiod) {}
int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *)) { return 0; }
int qh_register_handler(const char *name, const char *description, unsigned int options, qh_handler handler) { return 0; }
int neb_register_callback(int callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *)) { return 0; }
const char *notification_reason_name(unsigned int reason_type) { return NULL; }
void logit(int data_type, int display, const char *fmt, ...) {}
void remove_event(squeue_t *sq, timed_event *event) {}

merlin_node ipc;
struct merlin_notify_stats merlin_notify_stats[9][2][2];
struct host *merlin_recv_host = NULL;
struct service *merlin_recv_service = NULL;

int ipc_is_connected(int msec) { return 0; }
void ipc_init_struct(void) {}
void ipc_deinit(void) {}
int dump_nodeinfo(merlin_node *n, int sd, int instance_id) {return 0;}

static merlin_event *last_decoded_event;
int ipc_send_event(merlin_event *pkt) {
	merlin_decode_event(merlin_sender, pkt);
	last_decoded_event = pkt;
	return 0;
}
int ipc_grok_var(char *var, char *val) {return 1;}

#include "../module.c"
#include "../pgroup.c"

void general_setup()
{
	num_peer_groups = 0;
	peer_group = NULL;
	nebmodule_init(0, "tests/singlenode.conf", NULL);
	merlin_should_send_paths = 0;
	ipc.name = "Local";
}

void general_teardown()
{
	nebmodule_deinit(0, 0);
}

START_TEST(test_callback_host_check)
{
	time_t expected_last_check = time(NULL);
	time_t not_expected_last_check = 2147123099;

	host_ary = calloc(1, sizeof(host*));
	host hst;
	memset(&hst, 0, sizeof(host));
	host_ary[0] = &hst;
	num_objects.hosts = 1;

	int event_type = NEBTYPE_PROCESS_EVENTLOOPSTART;
	post_config_init(0, &event_type);

	hst.id = 0;
	hst.name = "test-host";
	hst.last_check = not_expected_last_check;

	nebstruct_host_check_data ev_data = {0,};
	merlin_host_status *event_body;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	ev_data.type = NEBTYPE_HOSTCHECK_PROCESSED;
	ev_data.flags = 0;
	ev_data.attr = NEBATTR_CHECK_ALERT;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &hst;
	ev_data.end_time.tv_sec =  expected_last_check;
	merlin_mod_hook(NEBCALLBACK_HOST_CHECK_DATA, &ev_data);
	ck_assert_int_eq(last_decoded_event->hdr.type, NEBCALLBACK_HOST_CHECK_DATA);
	event_body = (merlin_host_status *)last_decoded_event->body;
	ck_assert_int_eq(event_body->nebattr, NEBATTR_CHECK_ALERT);
	ck_assert_str_eq(event_body->name, hst.name);
	ck_assert_int_eq(expected_last_check, event_body->state.last_check);
}
END_TEST

START_TEST(test_callback_service_check)
{
	time_t expected_last_check = time(NULL);
	time_t not_expected_last_check = 2147123099;

	num_objects.services = 1;
	int event_type = NEBTYPE_PROCESS_EVENTLOOPSTART;
	post_config_init(0, &event_type);

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
	ev_data.attr = NEBATTR_CHECK_ALERT;
	ev_data.timestamp = tv;
	ev_data.object_ptr = &svc;
	ev_data.end_time.tv_sec =  expected_last_check;
	merlin_mod_hook(NEBCALLBACK_SERVICE_CHECK_DATA, &ev_data);
	ck_assert_int_eq(last_decoded_event->hdr.type, NEBCALLBACK_SERVICE_CHECK_DATA);
	event_body = (merlin_service_status *)last_decoded_event->body;
	ck_assert_int_eq(event_body->nebattr, NEBATTR_CHECK_ALERT);
	ck_assert_str_eq(event_body->host_name, svc.host_name);
	ck_assert_int_eq(expected_last_check, event_body->state.last_check);
}
END_TEST

Suite *
check_hooks_suite(void)
{
	Suite *s = suite_create("hooks");

	TCase *tc = tcase_create("callback");
	tcase_add_checked_fixture (tc, general_setup, general_teardown);
	tcase_add_test(tc, test_callback_host_check);
	tcase_add_test(tc, test_callback_service_check);
	suite_add_tcase(s, tc);

	return s;
}

int main(int argc, char *argv[]) {
		int number_failed;
	Suite *s = check_hooks_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
