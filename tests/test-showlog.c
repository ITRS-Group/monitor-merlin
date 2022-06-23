#include <check.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "auth.c"

#define pipe2(a, b) ck_assert(0 == pipe2(a, b))
#define assert_write(a, b, c) ck_assert((int)c == write(a, b, c))
#define pipe(a) ck_assert(0 == pipe(a))

START_TEST (hosts_services)
{
	int pipes[2];
	char *message = "host1;host2;host3\nhost1;service1;host4;service2";

	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	if (_i == 0)
		assert_write(pipes[1], "\n", 1);
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(auth_host_ok("host3"), 1);
	ck_assert_int_eq(auth_host_ok("host4"), 0);
	ck_assert_int_eq(auth_host_ok("service1"), 0);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 3);
	ck_assert_int_eq(auth_service_ok("host1", "service1"), 1);
	ck_assert_int_eq(auth_service_ok("host4", "service2"), 1);
	ck_assert_int_eq(auth_service_ok("host5", "service1"), 0);
	/* via host: */
	ck_assert_int_eq(auth_service_ok("host3", "service1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 2);
}
END_TEST

START_TEST (hosts_only)
{
	int pipes[2];
	char *message = "host1;host2;host3";

	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	if (_i == 0)
		assert_write(pipes[1], "\n", 1);
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(auth_host_ok("host3"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 3);
	ck_assert_int_eq(auth_service_ok("host1", "service1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 0);
}
END_TEST

START_TEST (odd_services)
{
	int pipes[2];
	char *message = "host1\nhost2";
	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	if (_i == 0)
		assert_write(pipes[1], "\n", 1);
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 1);
	ck_assert_int_eq(auth_service_ok("host1", "service1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 0);
}
END_TEST

START_TEST (multiblock)
{
	int pipes[2];
	char *message = "host1;host2\nhost3;service1;host4;service2";

	blocksize = 10; // bytes to read at a time
	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(auth_host_ok("host2"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 2);
	ck_assert_int_eq(auth_service_ok("host3", "service1"), 1);
	ck_assert_int_eq(auth_service_ok("host4", "service2"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 2);
}
END_TEST

START_TEST (too_many_lines)
{
	int pipes[2];
	char *message = "host1\nhost2;service1\nlots;of;other;data";

	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 1);
	ck_assert_int_eq(auth_service_ok("host2", "service1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 1);
}
END_TEST

START_TEST (empty_params)
{
	int pipes[2];
	char *message = "host1;;;;host2\nhost3;service1;;;;;;;host4;service2";

	pipe2(pipes, O_NONBLOCK);
	assert_write(pipes[1], message, strlen(message));
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(auth_host_ok("host1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 3);
	ck_assert_int_eq(auth_service_ok("host2", "service1"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 3);
}
END_TEST

START_TEST (long_parameter)
{
	int pipes[2];
	pid_t pid;

	pipe2(pipes, 0);
	pid = fork();
	if (pid < 0) {
		ck_abort_msg("Failed to fork");
	}
	else if (pid == 0) {
		char *message = malloc(6 * 10000 + 11 * 1000000);
		char *ptr = message;
		int i;

		close(pipes[0]);
		// hosts are h[0-9]{4}, meaning 6 bytes (incl ;) each meaning 10000 combinations
		// services are i[0-9]{4};s[0-9]{2}, meaning 10 bytes each meaning 1 000 000 combinations
		// in total, many megabytes of data, which should overflow any in-kernel buffers nicely.
		for (i = 0; i < 10000; i++, ptr += 6) {
			sprintf(ptr, "h%04d;", i);
		}
		ptr[-1] = '\n';
		for (i = 0; i < 10000; i++) {
			int j;
			for (j = 0; j < 100; j++, ptr += 10) {
				sprintf(ptr, "i%04d;s%02d;", i, j);
			}
		}
		ptr[-1] = '\0';
		ptr = message;
		while (strlen(ptr)) {
			ssize_t written = write(pipes[1], ptr, strlen(ptr));
			ptr += written;
		}
		free(message);
		close(pipes[1]);
		exit(0);
	}
	close(pipes[1]);
	auth_read_input(fdopen(pipes[0], "r"));
	ck_assert_int_eq(g_hash_table_size(auth_hosts), 10000);
	ck_assert_int_eq(auth_host_ok("h0000"), 1);
	ck_assert_int_eq(auth_host_ok("h9999"), 1);
	ck_assert_int_eq(auth_service_ok("i0000", "s00"), 1);
	ck_assert_int_eq(auth_service_ok("i9999", "s99"), 1);
	ck_assert_int_eq(g_hash_table_size(auth_services), 1000000);
}
END_TEST

static char *
run_with(char *args[], char *input_str)
{
	int output[2];
	int input[2];
	pid_t pid;
	char *output_str = calloc(1, 6000);
	size_t len;
	int status;

	pipe(output);
	pipe(input);
	pid = fork();
	if (pid < 0) {
		ck_abort_msg("Couldn't fork to run command");
	}
	else if (pid == 0) {
		char *the_environ[] = { NULL };

		close(output[0]);
		close(input[1]);
		dup2(output[1], STDOUT_FILENO);
		dup2(output[1], STDERR_FILENO);
		dup2(input[0], STDIN_FILENO);
		args[0] = "./showlog";
		execve("./showlog", args, the_environ);
		ck_abort_msg("Execve failed. This is bad.");
	}

	close(output[1]);
	close(input[0]);
	if (input_str != NULL)
		assert_write(input[1], input_str, strlen(input_str));
	close(input[1]);
	waitpid(pid, &status, 0);
	len = read(output[0], output_str, 6000);
	output_str[len] = '\0';
	return output_str;
}

START_TEST (return_input)
{
	int fd = open("tests/showlog_test.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);
	int diff;
	char *actual;
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		NULL
	};

	expected[len] = 0;
	close(fd);
	actual = run_with(args, NULL);
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

START_TEST (monitor_only)
{
	int fd = open("tests/showlog_test_monitoronly.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);
	int diff;
	char *actual;
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		"--restrict-objects",
		"--hide-process",
		"--hide-command",
		NULL
	};

	expected[len] = 0;
	close(fd);
	actual = run_with(args, "monitor");
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

START_TEST (monitor_and_not_monitor1_svc)
{
	int fd = open("tests/showlog_test_monitor_and_not_monitor1_svc.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		"--restrict-objects",
		"--hide-process",
		"--hide-command",
		NULL
	};
	char *actual;
	int diff;

	expected[len] = 0;
	close(fd);
	actual = run_with(args, "monitor\nnot-monitor1;Cron process");
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

START_TEST (not_monitor1_svc_only)
{
	int fd = open("tests/showlog_test_not_monitor1_svc_only.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		"--restrict-objects",
		"--hide-process",
		"--hide-command",
		NULL
	};
	char *actual;
	int diff;

	actual = run_with(args, "\nnot-monitor1;Cron process");
	expected[len] = 0;
	close(fd);
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

START_TEST (none)
{
	char expected[] = "";
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--restrict-objects",
		"--hide-process",
		"--hide-command",
		NULL
	};
	char *actual = run_with(args, "");
	ck_assert_str_eq(expected, actual);
	free(actual);
}
END_TEST

START_TEST (select_single_host)
{
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		"--host=monitor",
		"--hide-process",
		"--hide-command",
		NULL
	};
	char *actual;
	int diff;
	int fd = open("tests/showlog_test_monitoronly.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);

	expected[len] = 0;
	actual = run_with(args, NULL);
	close(fd);
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

START_TEST (select_single_service)
{
	char *args[] = {
		NULL,
		"tests/showlog_test.log",
		"--show-all",
		"--first=1386686225",
		"--last=1412770876",
		"--ascii",
		"--time-format=raw",
		"--service=not-monitor1;Cron process",
		"--hide-process",
		"--hide-command",
		NULL
	};
	char *actual;
	int diff;
	int fd = open("tests/showlog_test_not_monitor1_svc_only.log", O_RDONLY);
	char expected[6000];
	ssize_t len = read(fd, expected, 6000);

	actual = run_with(args, NULL);
	expected[len] = 0;
	close(fd);
	if ((diff = strcmp(expected, actual))) {
		ck_abort_msg("showlog didn't return the input. It looked like %s", actual);
	}
	free(actual);
}
END_TEST

Suite *
showlog_suite(void)
{
  Suite *s = suite_create("Showlog");
  TCase *log_selection;

  TCase *auth = tcase_create("Auth");
  tcase_add_loop_test(auth, hosts_services, 0, 2);
  tcase_add_loop_test(auth, hosts_only, 0, 2);
  tcase_add_loop_test(auth, odd_services, 0, 2);
  tcase_add_test(auth, multiblock);
  tcase_add_test(auth, too_many_lines);
  tcase_add_test(auth, empty_params);
  tcase_add_test(auth, long_parameter);
  tcase_set_timeout(auth, 30);
  suite_add_tcase(s, auth);

  log_selection = tcase_create("Log selection");
  tcase_add_test(log_selection, return_input);
  tcase_add_test(log_selection, monitor_only);
  tcase_add_test(log_selection, monitor_and_not_monitor1_svc);
  tcase_add_test(log_selection, not_monitor1_svc_only);
  tcase_add_test(log_selection, none);
  tcase_add_test(log_selection, select_single_host);
  tcase_add_test(log_selection, select_single_service);
  suite_add_tcase(s, log_selection);

  return s;
}

int
main (void)
{
  int number_failed;
  Suite *s = showlog_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
