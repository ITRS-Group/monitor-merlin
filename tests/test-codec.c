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
	size_t bufsz;
	unsigned char *buf = NULL;
	nebstruct_contact_notification_data ds = {0,};
	nebstruct_contact_notification_data *ds2 = NULL;
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
	MerlinMessage *message = merlin_message_from_nebstruct(MM_ContactNotificationData, &ds);
	merlin_message_set_selection(message, DEST_BROADCAST);

	ck_assert(!merlin_message_is_ctrl_packet(message));
	ck_assert(!merlin_message_is_nonet(message));
	ck_assert_int_eq(DEST_BROADCAST, merlin_message_get_selection(message));
	bufsz = merlin_encode_message(message, &buf);
	ck_assert_int_gt(bufsz, 0);
	ck_assert_msg(NULL != buf, "Encoded buffer is unexpectedly NULL");
	merlin_message_destroy(message);
	message = NULL;
	/* decode */
	message = merlin_decode_message(bufsz, buf);
	ds2 = merlin_message_to_nebstruct(message);
	ck_assert_msg(NULL != ds2, "Converted nebstruct is unexpectedly NULL");
	ck_assert_int_eq(ds.type, ds2->type);
	ck_assert_int_eq(ds.timestamp.tv_sec, ds2->timestamp.tv_sec);
	ck_assert_int_eq(ds.notification_type, ds2->notification_type);
	ck_assert_int_eq(ds.start_time.tv_sec, ds2->start_time.tv_sec);
	ck_assert_int_eq(ds.end_time.tv_sec, ds2->end_time.tv_sec);
	ck_assert_str_eq(ds.host_name, ds2->host_name);
	ck_assert_str_eq(ds.service_description, ds2->service_description);
	ck_assert_str_eq(ds.contact_name, ds2->contact_name);
	ck_assert_int_eq(ds.reason_type, ds2->reason_type);
	ck_assert_int_eq(ds.state, ds2->state);
	ck_assert_str_eq(ds.output, ds2->output);
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
