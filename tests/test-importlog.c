#include <check.h>
#include <stdlib.h>
#include <sys/wait.h>
#include "auth.c"

#define pipe(a) ck_assert(0 == pipe(a))
#define assert_write(a, b, c) ck_assert((int)c == write(a, b, c))

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
		args[0] = "./import";
		execve("./import", args, the_environ);
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

/*
 * This test ensures that the "mon log import" function does not crash when
 * the log contains a line containing [not-a-timestamp].
 */
START_TEST (invalid_timestamp)
{
	char *output;
	char *args[] = {
		NULL,
		"--no-sql",
		"tests/importlog_naemon.log",
		NULL
	};

	/* Check that output does not contain crash msg */
	output = run_with(args, NULL);
	if (strstr(output, "crash() called when parsing") != NULL) {
		ck_abort_msg("Import crashed when parsing a line. Output: \n%s", output);
	}

	free(output);
}
END_TEST

Suite *
importlog_suite(void)
{
	Suite *s = suite_create("Importlog");
	TCase *invalid_line;

	invalid_line = tcase_create("Invalid log line");
	tcase_add_test(invalid_line, invalid_timestamp);
	suite_add_tcase(s, invalid_line);

	return s;
}

int
main (void)
{
	int number_failed;
	Suite *s = importlog_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
