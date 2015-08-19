#include "codec.h"
#include "logging.c"
#include <check.h>

void general_setup(void)
{
	merlin_log_file = "stdout";
	log_levels = -1;
	log_init();
}

void general_teardown(void)
{
}

START_TEST(test_encode_serviceevent)
{
	int ret;
	merlin_event pkt;
	merlin_service_status ds, *out;

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
	pkt.hdr.selection = DEST_BROADCAST;
	pkt.hdr.code = MAGIC_NONET;
	memset(&ds, 0, sizeof(ds));
	/* set some relevant values at the beginning and end of the struct */
	ds.state.initial_state = 123;
	ds.state.notified_on = 456;
	ds.host_name = "foo";
	ds.state.perf_data = "bar";
	ret = merlin_encode_event(&pkt, (void *)&ds);
	ck_assert(ret > 0);
	pkt.hdr.len = ret;
	ret = merlin_decode_event(NULL, &pkt);
	ck_assert_int_eq(ret, 0);
	out = (merlin_service_status *) pkt.body;
	ck_assert_int_eq(123, out->state.initial_state);
	ck_assert_int_eq(456, out->state.notified_on);
	ck_assert_str_eq("foo", out->host_name);
	ck_assert_str_eq("bar", out->state.perf_data);
}
END_TEST

START_TEST(test_encode_too_long)
{
	int ret;
	merlin_event pkt;
	merlin_service_status ds, *out;
	char *input = calloc((1 << 20) + 1, 1);

	memset(&pkt, 0, sizeof(pkt));
	pkt.hdr.type = NEBCALLBACK_SERVICE_CHECK_DATA;
	pkt.hdr.selection = DEST_BROADCAST;
	pkt.hdr.code = MAGIC_NONET;
	memset(&ds, 0, sizeof(ds));
	memset(input, 'a', 1 << 20);
	input[1<<20] = 0;
	ds.host_name = strdup(input);
	ds.service_description = "This should be truncated away";
	ds.state.perf_data = "This should be truncated away";
	ds.state.plugin_output = "This should be truncated away";
	ds.state.long_plugin_output = "This should be truncated away";
	ret = merlin_encode_event(&pkt, (void *)&ds);
	ck_assert(ret > 0);
	pkt.hdr.len = ret;
	ret = merlin_decode_event(NULL, &pkt);
	ck_assert_int_eq(ret, 0);
	out = (merlin_service_status *) pkt.body;
	ck_assert(pkt.body[(128 << 10) - 1] == 0);
	ck_assert(strcmp(input, out->host_name));
	ck_assert(!strncmp(input, out->host_name, 120<<10));
	ck_assert(NULL == out->service_description);
}
END_TEST

Suite *
check_codec_suite(void)
{
	Suite *s = suite_create("codec");

	TCase *tc = tcase_create("codec");
	tcase_add_checked_fixture (tc, general_setup, general_teardown);
	tcase_add_test(tc, test_encode_serviceevent);
	tcase_add_test(tc, test_encode_too_long);
	suite_add_tcase(s, tc);

	return s;
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[]) {
		int number_failed;
	Suite *s = check_codec_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
