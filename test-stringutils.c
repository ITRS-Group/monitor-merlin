#include "test_utils.h"
#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#define T_ASSERT(pred, msg) do {\
	if ((pred)) { t_pass("%s: %s", __FUNCTION__, msg); } else { t_fail("%s: %s", __FUNCTION__, msg); } \
	} while (0)

void test_unescape_newlines() {
	const char *src = "foo\nbar\\n\\baz\\n";
	char *expected = "foo\nbar\n\\baz\n";
	char *dest = (char *)malloc(strlen(src));
	unescape_newlines(dest, src, strlen(src));
	T_ASSERT(0 == strcmp(expected, dest), "newlines unescaped as expected");
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
