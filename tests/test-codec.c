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
	MerlinMessage *message = merlin_message_from_payload(MM_ContactNotificationData, &ds);
	merlin_message_set_selection(message, DEST_BROADCAST);

	ck_assert(!merlin_message_is_ctrl_packet(message));
	ck_assert(!merlin_message_is_nonet(message));
	ck_assert_int_eq(DEST_BROADCAST, merlin_message_get_selection(message));
	bufsz = merlin_encode_message(message, &buf);
	ck_assert(bufsz > 0);
	ck_assert_msg(NULL != buf, "Encoded buffer is unexpectedly NULL");
	merlin_message_destroy(message);
	message = NULL;
	/* decode */
	message = merlin_decode_message(bufsz, buf);
	ds2 = merlin_message_to_payload(message);
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
	free(ds2);
}
END_TEST

START_TEST(test_merlin_ctrl_packet)
{
	struct timeval tv;
	merlin_nodeinfo info = {0,};
	merlin_nodeinfo *info2 = NULL;
	size_t bufsz;
	unsigned char *buf = NULL;
	gettimeofday(&tv, NULL);
	info.start = tv;
	info.last_cfg_change = 12345678L;
	strncpy((char *)info.config_hash, "6b945c39dcedda030c6a2416c866815b7d988815", 20);
	info.peer_id = 1;
	info.active_peers = 0;
	info.configured_peers = 0;
	info.active_pollers = 1;
	info.configured_pollers = 1;
	info.active_masters = 0;
	info.configured_masters = 0;
	info.host_checks_handled = 5;
	info.service_checks_handled = 10;
	MerlinMessage *message = merlin_message_from_payload(MM_MerlinCtrlPacket, &info);
	merlin_message_set_selection(message, DEST_PEERS_POLLERS);
	ck_assert(merlin_message_is_ctrl_packet(message));
	ck_assert(!merlin_message_is_nonet(message));
	ck_assert_int_eq(DEST_PEERS_POLLERS, merlin_message_get_selection(message));
	bufsz = merlin_encode_message(message, &buf);
	ck_assert(bufsz > 0);
	ck_assert_msg(NULL != buf, "Encoded buffer is unexpectedly NULL");
	merlin_message_destroy(message);
	message = NULL;
	/* decode */
	message = merlin_decode_message(bufsz, buf);
	info2 = merlin_message_to_payload(message);
	ck_assert_msg(NULL != info2, "Converted nodeinfo is unexpectedly NULL");
	ck_assert_int_eq(info.start.tv_sec, info2->start.tv_sec);
	ck_assert_int_eq(info.start.tv_usec, info2->start.tv_usec);
	ck_assert_int_eq(info.last_cfg_change, info2->last_cfg_change);
	ck_assert_str_eq(info.config_hash, info2->config_hash);
	ck_assert_int_eq(info.peer_id, info2->peer_id);
	ck_assert_int_eq(info.active_peers, info2->active_peers);
	ck_assert_int_eq(info.configured_peers, info2->configured_peers);
	ck_assert_int_eq(info.active_pollers, info2->active_pollers);
	ck_assert_int_eq(info.configured_pollers, info2->configured_pollers);
	ck_assert_int_eq(info.active_masters, info2->active_masters);
	ck_assert_int_eq(info.configured_masters, info2->configured_masters);
	ck_assert_int_eq(info.host_checks_handled, info2->host_checks_handled);
	ck_assert_int_eq(info.service_checks_handled, info2->service_checks_handled);
}
END_TEST

Suite *
check_codec_suite(void)
{
	Suite *s = suite_create("codec");

	TCase *tc = tcase_create("messages");
	tcase_add_test(tc, test_contact_notification_data);
	tcase_add_test(tc, test_merlin_ctrl_packet);
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
