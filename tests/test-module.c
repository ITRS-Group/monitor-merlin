#define _GNU_SOURCE
#include <unistd.h>
#include <check.h>
#include <time.h>
#include "../module.c"

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
timed_event *schedule_new_event(int event_type, int high_priority, time_t run_time, int recurring, unsigned long event_interval, void *timing_func, int compensate_for_time_change, void *event_data, void *event_args, int event_options) { return NULL; }
nagios_macros *get_global_macros() { return NULL; }
void fcache_timeperiod(FILE *fp, struct timeperiod *temp_timeperiod) {}
int neb_deregister_callback(int callback_type, int (*callback_func)(int, void *)) { return 0; }
int qh_register_handler(const char *name, const char *description, unsigned int options, qh_handler handler) { return 0; }
int neb_register_callback(int callback_type, void *mod_handle, int priority, int (*callback_func)(int, void *)) { return 0; }
const char *notification_reason_name(unsigned int reason_type) { return NULL; }
void logit(int data_type, int display, const char *fmt, ...) {}

void node_migration_setup() {
	config_file_dir = get_current_dir_name();
	config_file = "nagios.cfg";
	nebmodule_init(0, "tests/module.conf", NULL);
	num_objects.hosts = 1;
	host_ary = calloc(1, sizeof(host*));
	host_ary[0] = calloc(1, sizeof(host));
	host_ary[0]->id = 0;
	host_ary[0]->services = calloc(1, sizeof(servicesmember));
	host_ary[0]->services->service_ptr = calloc(1, sizeof(service));
	host_ary[0]->services->service_ptr->id = 0;
//	host_ary[1] = calloc(1, sizeof(host));
//	host_ary[1]->id = 1;
//	host_ary[1]->services = calloc(1, sizeof(servicesmember));
//	host_ary[1]->services->service_ptr = calloc(1, sizeof(service));
//	host_ary[1]->services->service_ptr->id = 1;
	num_objects.services = 1;
	struct timeval ts = {0,0};
	nebstruct_process_data ds;
	ds.type = NEBTYPE_PROCESS_EVENTLOOPSTART;
	ds.flags = 0;
	ds.attr = 0;
	ds.timestamp = ts;
	post_config_init(0, (void *)&ds);
	ipc.action = NULL;
	node_set_state(&ipc, STATE_CONNECTED, "Fake connected");
	node_set_state(node_table[0], STATE_CONNECTED, "Fake connected");
	node_set_state(node_table[1], STATE_CONNECTED, "Fake connected");
	pgroup_assign_peer_ids(node_table[0]->pgroup);
}

void node_migration_teardown() {
}


START_TEST (check_host_node)
{
	int from = _i % 6, to = _i / 6;
	int from_passive = from % 2, to_passive = to % 2;
	host *hst = host_ary[0];
	int i;
	merlin_node *nodes[] = {
		&ipc,
		node_table[0],
		node_table[1],
	};

	from = from / 2;
	to = to / 2;
	// remember: num_nodes = number of nodes - 1, because ipc is magic
	merlin_node *node = nodes[from];
	set_host_check_node(node, hst, from_passive);
	ck_assert_int_eq(from_passive, bitmap_isset(passive_host_checks, hst->id));
	ck_assert(pgroup_host_node(hst->id) == nodes[0]);
	ck_assert(host_check_node[hst->id] == nodes[from]);
	if (from == 0)
		ck_assert(pgroup_host_node(hst->id) == host_check_node[hst->id]);
	// < 3, because https://www.youtube.com/watch?v=uQu71l1WQ3g
	for (i = 0; i < 3; i++) {
		ck_assert_int_eq((nodes[i] == node), nodes[i]->host_checks);
		ck_assert_int_eq((i == 0), nodes[i]->assigned.current.hosts);
		ck_assert_int_eq(0, nodes[i]->assigned.extra.hosts);
		if (from_passive) {
			if (i == 0 && nodes[i] == node) // responsible, ran check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.hosts);
			else if (nodes[i] == node) // not responsible, ran check
				ck_assert_int_eq(1, nodes[i]->assigned.passive.hosts);
			else if (i == 0) // responsible, didn't run check
				ck_assert_int_eq(-1, nodes[i]->assigned.passive.hosts);
			else // not responsible, didn't run check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.hosts);
		}
	}
	node = nodes[to];
	set_host_check_node(node, hst, to_passive);
	ck_assert_int_eq(to_passive, bitmap_isset(passive_host_checks, hst->id));
	for (i = 0; i < 3; i++) {
		ck_assert_int_eq((nodes[i] == node), nodes[i]->host_checks);
		ck_assert_int_eq((i == 0), nodes[i]->assigned.current.hosts);
		ck_assert_int_eq(0, nodes[i]->assigned.extra.hosts);
		if (to_passive) {
			if (i == 0 && nodes[i] == node) // responsible, ran check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.hosts);
			else if (nodes[i] == node) // not responsible, ran check
				ck_assert_int_eq(1, nodes[i]->assigned.passive.hosts);
			else if (i == 0) // responsible, didn't run check
				ck_assert_int_eq(-1, nodes[i]->assigned.passive.hosts);
			else // not responsible, didn't run check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.hosts);
		} else {
			ck_assert_int_eq(0, nodes[i]->assigned.passive.hosts);
		}
	}
}
END_TEST

START_TEST (check_service_node)
{
	int from = _i % 6, to = _i / 6;
	int from_passive = from % 2, to_passive = to % 2;
	service *svc = host_ary[0]->services->service_ptr;
	int i;
	merlin_node *nodes[] = {
		&ipc,
		node_table[0],
		node_table[1],
	};

	from = from / 2;
	to = to / 2;
	// remember: num_nodes = number of nodes - 1, because ipc is magic
	merlin_node *node = nodes[from];
	set_service_check_node(node, svc, from_passive);
	ck_assert_int_eq(from_passive, bitmap_isset(passive_service_checks, svc->id));
	ck_assert(pgroup_service_node(svc->id) == nodes[0]);
	ck_assert(service_check_node[svc->id] == nodes[from]);
	if (from == 0)
		ck_assert(pgroup_service_node(svc->id) == service_check_node[svc->id]);
	for (i = 0; i < 3; i++) {
		ck_assert_int_eq((nodes[i] == node), nodes[i]->service_checks);
		ck_assert_int_eq((i == 0), nodes[i]->assigned.current.services);
		ck_assert_int_eq(0, nodes[i]->assigned.extra.services);
		if (from_passive) {
			if (i == 0 && nodes[i] == node) // responsible, ran check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.services);
			else if (nodes[i] == node) // not responsible, ran check
				ck_assert_int_eq(1, nodes[i]->assigned.passive.services);
			else if (i == 0) // responsible, didn't run check
				ck_assert_int_eq(-1, nodes[i]->assigned.passive.services);
			else // not responsible, didn't run check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.services);
		}
	}
	node = nodes[to];
	set_service_check_node(node, svc, to_passive);
	ck_assert_int_eq(to_passive, bitmap_isset(passive_service_checks, svc->id));
	for (i = 0; i < 3; i++) {
		ck_assert_int_eq((nodes[i] == node), nodes[i]->service_checks);
		ck_assert_int_eq((i == 0), nodes[i]->assigned.current.services);
		ck_assert_int_eq(0, nodes[i]->assigned.extra.services);
		if (to_passive) {
			if (i == 0 && nodes[i] == node) // responsible, ran check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.services);
			else if (nodes[i] == node) // not responsible, ran check
				ck_assert_int_eq(1, nodes[i]->assigned.passive.services);
			else if (i == 0) // responsible, didn't run check
				ck_assert_int_eq(-1, nodes[i]->assigned.passive.services);
			else // not responsible, didn't run check
				ck_assert_int_eq(0, nodes[i]->assigned.passive.services);
		} else {
			ck_assert_int_eq(0, nodes[i]->assigned.passive.services);
		}
	}
}
END_TEST

Suite *
check_node_suite(void)
{
	Suite *s = suite_create("module");

	TCase *migration = tcase_create("node migration");
	tcase_add_checked_fixture (migration, node_migration_setup, node_migration_teardown);
	tcase_add_loop_test(migration, check_host_node, 0, 6 * 6);
	tcase_add_loop_test(migration, check_service_node, 0, 6 * 6);
	suite_add_tcase(s, migration);

	return s;
}

int
main (void)
{
	int number_failed;
	Suite *s = check_node_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
