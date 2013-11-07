#include "test_utils.h"
#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#define T_ASSERT(pred, msg) do {\
	if ((pred)) { t_pass("%s: %s", __FUNCTION__, msg); } else { t_fail("%s: %s", __FUNCTION__, msg); } \
	} while (0)

void test_unescape_newlines() {
	char *src = "foo\nbar\\n\\baz\\n";
	char *expected = "foo\nbar\n\\baz\n";
	char *dest = (char *)malloc(strlen(src));
	size_t len = 0, expected_size = 0;
	len = unescape_newlines(dest, src, strlen(src));
	T_ASSERT(0 == strcmp(expected, dest), "newlines unescaped as expected");
	expected_size = strlen(expected)+1;
	T_ASSERT(len == expected_size, "returned length is correct");
	expected_size = strlen(dest)+1;
	T_ASSERT(len == expected_size, "destination buffer is of correct length");
	free(dest);

	/*test binary safe-ness*/
	src = "foo\nbar\\nboll\0kaka";
	expected = "foo\nbar\nboll\0kaka";
	expected_size = 18;
	dest = (char *)malloc(expected_size);
	len = unescape_newlines(dest, src, expected_size);
	T_ASSERT(expected_size == len, "returned length is correct");
	T_ASSERT(0 == memcmp(expected, dest, len), "length argument is respected");
	free(dest);

	/* test quoted backslash */
	src = "foo\\\\nbar";
	expected = "foo\\nbar";
	expected_size = 9;
	dest = (char *)malloc(expected_size);
	len = unescape_newlines(dest, src, expected_size);
	T_ASSERT(expected_size == len, "returned length is correct");
	T_ASSERT(0 == memcmp(expected, dest, len), "length argument is respected");
	free(dest);
}

int main(int argc, char *argv[]) {
	t_set_colors(0);
	t_verbose = 1;

	t_start("testing string utilities");
	test_unescape_newlines();

	t_end();
	return 0;
}
