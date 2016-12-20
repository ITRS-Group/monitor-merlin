#include "codec.h"
#include "hooks.c"
#include "node.h"
#include <check.h>

#include <naemon/naemon.h>

struct object_count num_objects = {0,};
comment *comment_list = NULL;
hostgroup *hostgroup_list = NULL;
servicegroup *servicegroup_list = NULL;
struct timeperiod **timeperiod_ary;
struct host **host_ary;
char *config_file_dir = NULL;
char *config_file = NULL;
char *temp_path = NULL;
iobroker_set *nagios_iobs = NULL;
int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;
unsigned long   event_broker_options = BROKER_NOTHING;
volatile sig_atomic_t sigshutdown = FALSE;
int interval_length = 60;
time_t event_start = 0L;
int service_check_timeout = 0;
int host_check_timeout = 0;
command *ocsp_command_ptr = NULL;
command *ochp_command_ptr = NULL;
command *global_host_event_handler_ptr = NULL;
command *global_service_event_handler_ptr = NULL;
char    *host_perfdata_command = NULL;
char    *service_perfdata_command = NULL;
char    *host_perfdata_file_processing_command = NULL;
char    *service_perfdata_file_processing_command = NULL;

static const char *queryhandler_socket_path = "/tmp/nagios.qh";

int process_check_result(__attribute__((unused)) check_result *cr){ return 0; }
struct comment *get_first_comment_by_host(__attribute__((unused)) char *host) { return NULL; }
int delete_comment(__attribute__((unused)) int type, __attribute__((unused)) unsigned long comment_id) { return 0; }
int delete_downtime_by_hostname_service_description_start_time_comment(__attribute__((unused)) char *hostname, __attribute__((unused)) char *service_description, __attribute__((unused)) time_t start_time, __attribute__((unused)) char *cmnt) { return 0; }
int init_check_result(__attribute__((unused)) check_result *cr) { return 0; }
int process_external_command2(__attribute__((unused)) int cmd, __attribute__((unused)) time_t entry_time, __attribute__((unused)) char *args) { return 0; }
int add_new_comment(__attribute__((unused)) int type, __attribute__((unused)) int entry_type, __attribute__((unused)) char *host_name, __attribute__((unused)) char *svc_description, __attribute__((unused)) time_t entry_time, __attribute__((unused)) char *author_name, __attribute__((unused)) char *comment_data, __attribute__((unused)) int persistent, __attribute__((unused)) int source, __attribute__((unused)) int expires, __attribute__((unused)) time_t expire_time, __attribute__((unused)) unsigned long *comment_id) { return 0; }

void *last_event;
void *last_user_data;


int ipc_init(void) { return 0; };
timed_event *
schedule_event(__attribute__((unused)) time_t delay, __attribute__((unused)) event_callback callback, void *user_data)
{
	void *res = calloc(1, 1);
	last_user_data = user_data;
	last_event = res;
	return res;
}
void
destroy_event(timed_event *event)
{
	ck_assert(last_event == event);
}

merlin_node ipc = { .name = "ipc" };

int ipc_is_connected(__attribute__((unused)) int msec) { return 0; }
void ipc_init_struct(void) {}
void ipc_deinit(void) {}
int dump_nodeinfo(__attribute__((unused)) merlin_node *n, __attribute__((unused)) int sd, __attribute__((unused)) int instance_id) {return 0;}


static merlin_event last_decoded_event;
int ipc_send_event(merlin_event *pkt) {
	merlin_decode_event(merlin_sender, pkt);
	memcpy(&last_decoded_event, pkt, sizeof(merlin_event));
	return 0;
}
int ipc_grok_var(__attribute__((unused)) char *var, __attribute__((unused)) char *val) {return 1;}

#include "module.c"
#include "pgroup.c"


static void expiration_setup(void)
{
	int event_type = NEBTYPE_PROCESS_EVENTLOOPSTART;
	host *hst;
	num_peer_groups = 0;
	peer_group = NULL;
	nagios_iobs = iobroker_create();
	qh_init(queryhandler_socket_path);
	nebmodule_init(0, "tests/twopeers.conf", NULL);
	init_objects_host(3);
	init_objects_service(3);
	hst = create_host("host0");
	register_host(hst);
	register_service(create_service(hst, "service0"));
	hst = create_host("host1");
	register_host(hst);
	register_service(create_service(hst, "service1"));
	hst = create_host("host2");
	register_host(hst);
	register_service(create_service(hst, "service2"));

	post_config_init(0, &event_type);

	ipc.action = NULL;
	ipc.name = "Local";
	node_set_state(node_table[0], STATE_CONNECTED, "Fake connected");
	node_set_state(node_table[1], STATE_CONNECTED, "Fake connected");
	pgroup_assign_peer_ids(node_table[0]->pgroup);
}

static void expiration_teardown(void)
{
	destroy_objects_host();
	destroy_objects_service();
	qh_deinit(queryhandler_socket_path);
	iobroker_destroy(nagios_iobs, IOBROKER_CLOSE_SOCKETS);
	nebmodule_deinit(0, 0);
}

#define to_timed_event(user_data) \
	((struct nm_event_execution_properties) {EVENT_EXEC_NORMAL, EVENT_TYPE_TIMED, user_data, {{last_event, 0}}})

START_TEST(set_clear_svc_expire)
{
	merlin_event pkt = {{{0,},0,0,0,0,0,{0,},{0}},{0}};
	nebstruct_service_check_data ds = {0,};
	ds.type = NEBTYPE_SERVICECHECK_ASYNC_PRECHECK;
	ds.object_ptr = host_ary[0]->services->service_ptr;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] != NULL, "Service sending a precheck should trigger expiration check");
	ck_assert_msg(expired_services[0] == NULL, "Service precheck should not expire service");
	expire_event(&to_timed_event(last_user_data));
	ck_assert_msg(expired_services[0] != NULL, "Service should become expired after expire_event runs");
	ck_assert_msg(service_expiry_map[0] == NULL, "Expiring a check should clear expiration check");
	ds.type = NEBTYPE_SERVICECHECK_PROCESSED;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] == NULL, "Service sending a check result should clear expiration check");
	ck_assert_msg(expired_services[0] == NULL, "Service should not be expired after check result comes in");
	ds.type = NEBTYPE_SERVICECHECK_ASYNC_PRECHECK;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] != NULL, "Service sending a precheck should trigger expiration check");
	ds.type = NEBTYPE_SERVICECHECK_PROCESSED;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] == NULL, "Service sending a check result should clear expiration check");
	ck_assert_msg(expired_services[0] == NULL, "Service should not be expired after check result comes in");
	ds.type = NEBTYPE_SERVICECHECK_PROCESSED;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] == NULL, "Resending a check result should keep expiration map cleared");
	ck_assert_msg(expired_services[0] == NULL, "Resending a check result should keep expired list cleared");
}
END_TEST

START_TEST(set_clear_host_expire)
{
	merlin_event pkt = {{{0,},0,0,0,0,0,{0,},{0}},{0}};
	nebstruct_host_check_data ds = {0,};
	ds.type = NEBTYPE_HOSTCHECK_ASYNC_PRECHECK;
	ds.object_ptr = host_ary[0];
	hook_host_result(&pkt, &ds);
	ck_assert_msg(host_expiry_map[0] != NULL, "Host sending a precheck should trigger expiration check");
	ck_assert_msg(expired_hosts[0] == NULL, "Host precheck should not expire host");
	expire_event(&to_timed_event(last_user_data));
	ck_assert_msg(expired_hosts[0] != NULL, "Host should become expired after expire_event runs");
	ck_assert_msg(host_expiry_map[0] == NULL, "Expiring a check should clear expiration check");
	ds.type = NEBTYPE_HOSTCHECK_PROCESSED;
	hook_host_result(&pkt, &ds);
	ck_assert_msg(host_expiry_map[0] == NULL, "Host sending a check result should clear expiration check");
	ck_assert_msg(expired_hosts[0] == NULL, "Host should not be expired after check result comes in");
	ds.type = NEBTYPE_HOSTCHECK_ASYNC_PRECHECK;
	hook_host_result(&pkt, &ds);
	ck_assert_msg(host_expiry_map[0] != NULL, "Host sending a precheck should trigger expiration check");
	ds.type = NEBTYPE_HOSTCHECK_PROCESSED;
	hook_host_result(&pkt, &ds);
	ck_assert_msg(host_expiry_map[0] == NULL, "Host sending a check result should clear expiration check");
	ck_assert_msg(expired_hosts[0] == NULL, "Host should not be expired after check result comes in");
	ds.type = NEBTYPE_HOSTCHECK_PROCESSED;
	hook_service_result(&pkt, &ds);
	ck_assert_msg(service_expiry_map[0] == NULL, "Resending a check result should keep expiration map cleared");
	ck_assert_msg(expired_services[0] == NULL, "Resending a check result should keep expired list cleared");
}
END_TEST

START_TEST(multiple_svc_expire)
{
	void *first_user_data;
	merlin_event pkt = {{{0,},0,0,0,0,0,{0,},{0}},{0}};
	nebstruct_service_check_data ds0 = {0,}, ds1 = {0,};
	ds0.type = NEBTYPE_SERVICECHECK_ASYNC_PRECHECK;
	ds0.object_ptr = host_ary[0]->services->service_ptr;
	ds1.type = NEBTYPE_SERVICECHECK_ASYNC_PRECHECK;
	ds1.object_ptr = host_ary[1]->services->service_ptr;
	hook_service_result(&pkt, &ds0);
	ck_assert_msg(service_expiry_map[0] != NULL, "Service sending a precheck should trigger expiration check");
	ck_assert_msg(service_expiry_map[1] == NULL, "Service sending a precheck should not trigger other expiration checks");
	ck_assert_msg(expired_services[0] == NULL, "Service precheck should not expire service");
	first_user_data = last_user_data;
	hook_service_result(&pkt, &ds1);
	ck_assert_msg(service_expiry_map[0] != NULL, "Old expiration check should still be around");
	ck_assert_msg(service_expiry_map[1] != NULL, "New service sending a precheck should trigger expiration check, too");
	expire_event(&to_timed_event(first_user_data));
	ck_assert_msg(expired_services[0] != NULL, "Service should become expired after expire_event runs");
	ck_assert_msg(service_expiry_map[0] == NULL, "Expiring a check should clear expiration check");
	ck_assert_msg(service_expiry_map[1] != NULL, "Other services in expiration precheck should not be affected by an expiration");
	ck_assert_msg(expired_services[1] == NULL, "Other services in expiration precheck should not be affected by an expiration");
	ds0.type = NEBTYPE_SERVICECHECK_PROCESSED;
	hook_service_result(&pkt, &ds0);
	ck_assert_msg(service_expiry_map[0] == NULL, "Service sending a check result should clear expiration check");
	ck_assert_msg(expired_services[0] == NULL, "Service should not be expired after check result comes in");
	ck_assert_msg(service_expiry_map[1] != NULL, "Other services in expiration precheck should not be affected by an expiration");
	ck_assert_msg(expired_services[1] == NULL, "Other services in expiration precheck should not be affected by an expiration");
	first_user_data = last_user_data;
	ds0.type = NEBTYPE_SERVICECHECK_ASYNC_PRECHECK;
	hook_service_result(&pkt, &ds0);
	expire_event(&to_timed_event(first_user_data));
	expire_event(&to_timed_event(last_user_data));
	ck_assert_msg(expired_services[0] != NULL, "Service should become expired after expire_event runs");
	ck_assert_msg(expired_services[1] != NULL, "Service should become expired after expire_event runs");
	ds0.type = NEBTYPE_SERVICECHECK_PROCESSED;
	hook_service_result(&pkt, &ds0);
	ck_assert_msg(service_expiry_map[0] == NULL, "Service sending a check result should clear expiration check");
	ck_assert_msg(expired_services[1] != NULL, "One service sending a check result should not clear others' expired status");
}
END_TEST

START_TEST(multiple_host_expire)
{
	void *first_user_data;
	merlin_event pkt = {{{0,},0,0,0,0,0,{0,},{0}},{0}};
	nebstruct_host_check_data ds0 = {0,}, ds1 = {0,};
	ds0.type = NEBTYPE_HOSTCHECK_ASYNC_PRECHECK;
	ds0.object_ptr = host_ary[0];
	ds1.type = NEBTYPE_HOSTCHECK_ASYNC_PRECHECK;
	ds1.object_ptr = host_ary[1];
	hook_host_result(&pkt, &ds0);
	ck_assert_msg(host_expiry_map[0] != NULL, "Host sending a precheck should trigger expiration check");
	ck_assert_msg(host_expiry_map[1] == NULL, "Host sending a precheck should not trigger other expiration checks");
	ck_assert_msg(expired_hosts[0] == NULL, "Host precheck should not expire host");
	first_user_data = last_user_data;
	hook_host_result(&pkt, &ds1);
	ck_assert_msg(host_expiry_map[0] != NULL, "Old expiration check should still be around");
	ck_assert_msg(host_expiry_map[1] != NULL, "New host sending a precheck should trigger expiration check, too");
	expire_event(&to_timed_event(first_user_data));
	ck_assert_msg(expired_hosts[0] != NULL, "Host should become expired after expire_event runs");
	ck_assert_msg(host_expiry_map[0] == NULL, "Expiring a check should clear expiration check");
	ck_assert_msg(host_expiry_map[1] != NULL, "Other hosts in expiration precheck should not be affected by an expiration");
	ck_assert_msg(expired_hosts[1] == NULL, "Other hosts in expiration precheck should not be affected by an expiration");
	ds0.type = NEBTYPE_HOSTCHECK_PROCESSED;
	hook_host_result(&pkt, &ds0);
	ck_assert_msg(host_expiry_map[0] == NULL, "Host sending a check result should clear expiration check");
	ck_assert_msg(expired_hosts[0] == NULL, "Host should not be expired after check result comes in");
	ck_assert_msg(host_expiry_map[1] != NULL, "Other hosts in expiration precheck should not be affected by an expiration");
	ck_assert_msg(expired_hosts[1] == NULL, "Other hosts in expiration precheck should not be affected by an expiration");
	first_user_data = last_user_data;
	ds0.type = NEBTYPE_HOSTCHECK_ASYNC_PRECHECK;
	hook_host_result(&pkt, &ds0);
	expire_event(&to_timed_event(first_user_data));
	expire_event(&to_timed_event(last_user_data));
	ck_assert_msg(expired_hosts[0] != NULL, "Host should become expired after expire_event runs");
	ck_assert_msg(expired_hosts[1] != NULL, "Host should become expired after expire_event runs");
	ds0.type = NEBTYPE_HOSTCHECK_PROCESSED;
	hook_host_result(&pkt, &ds0);
	ck_assert_msg(host_expiry_map[0] == NULL, "Host sending a check result should clear expiration check");
	ck_assert_msg(expired_hosts[1] != NULL, "One host sending a check result should not clear others' expired status");
}
END_TEST

Suite *
check_hooks_suite(void)
{
	Suite *s = suite_create("hooks");
	TCase *tc;

	tc = tcase_create("expiration");
	tcase_add_checked_fixture (tc, expiration_setup, expiration_teardown);
	tcase_add_test(tc, set_clear_host_expire);
	tcase_add_test(tc, set_clear_svc_expire);
	tcase_add_test(tc, multiple_host_expire);
	tcase_add_test(tc, multiple_svc_expire);
	suite_add_tcase(s, tc);

	return s;
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[]) {
		int number_failed;
	Suite *s = check_hooks_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
