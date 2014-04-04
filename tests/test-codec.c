#include "../codec.h"
#include "../node.h"
#include "../module.h"
#include <check.h>

#include <nagios/broker.h>
#include <sys/time.h>
char *config_file_dir = NULL;
char *config_file = NULL;
int ipc_grok_var(char *var, char *val) {return 1;}
const char *notification_reason_name(unsigned int reason_type) { return NULL; }
int dump_nodeinfo(merlin_node *n, int sd, int instance_id) {return 0;}
struct merlin_notify_stats merlin_notify_stats[9][2][2];

START_TEST(test_contact_notification_data)
{
	struct timeval tv;
	nebstruct_contact_notification_data ds = {0,};
	gettimeofday(&tv, NULL);
	ds.type = NEBTYPE_CONTACTNOTIFICATION_END;
	ds.timestamp = tv;
	ds.notification_type = SERVICE_NOTIFICATION;
	ds.start_time = tv;
	ds.start_time.tv_sec += 10;
	ds.end_time = tv;
	ds.end_time.tv_sec +=20;
	ds.host_name = "ultrahost.cx";
	ds.service_description = "mega-service-checkar";
	ds.contact_name = "Bob the Builder";
	ds.reason_type = NOTIFICATION_NORMAL;
	ds.state = 2;
	ds.output = "Oh no! The service was eaten by a grue!";
	MerlinMessage *message = merlin_message_create(MM_ContactNotificationData, &ds);
	merlin_message_set_selection(message, DEST_BROADCAST);

	ck_assert(!merlin_message_is_ctrl_packet(message));
	ck_assert(!merlin_message_is_nonet(message));
	ck_assert_int_eq(DEST_BROADCAST, merlin_message_get_selection(message));
	/// ck_assert(message_encode(message)) ... something something


}
END_TEST

Suite *
check_codec_suite(void)
{
	Suite *s = suite_create("codec");

	TCase *tc = tcase_create("messages");
	tcase_add_test(tc, test_contact_notification_data);
	suite_add_tcase(s, tc);

	return s;
}

int main(int argc, char *argv[]) {
	int number_failed;
	Suite *s = check_codec_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
