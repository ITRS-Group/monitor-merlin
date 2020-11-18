from builtins import range
from pytap import *

# import pytapunit


def run(tap):
    tap.print_header("pytap sub example")
    tap.ok(True, "Passing test")
    tap.ok(False, "Failing test")
    tap.ok(False, "Failing test with string type diag output")
    tap.diag("This is a diagnostics string")
    tap.ok_eq(1, False, "Passing test with bool type diag output")
    tap.diag({"multi": {"dimensional": {"python array": "with value"}}})
    tap.diag(False)
    tap.ok(False, "Known breakage", tap.TODO)
    tap.ok(False, "A single (failed) skipped test", tap.SKIP)
    tap.ok(True, "'fixed' test", tap.TODO)
    tap.ok(9, "Buggy test")
    tap.ok(9 != False, "No longer buggy test")
    tap.ok_type({}, {"nisse": True}, "An array is an array")
    tap.ok(1 == 1, "1 and 1 are equal")
    tap.ok(1 == "nisse", "A failed test")
    tap.ok_eq(1, "nisse", "knowingly failed test", tap.TODO)
    tap.skip_start()
    tap.ok(True, "Skipping OK test")
    tap.ok_eq(1, "nisse", "Skipping failed test")
    tap.ok_eq(1, "nisse", "Skipping todo test", tap.TODO)
    tap.skip_end()
    tap.skip_start("Skipping 1 test with a reason", 1)
    tap.ok_eq(1, "nisse", "This output should be hidden")
    return tap.done()


tap = pytap("Basic tests")
tap.verbose = 1
sub = tap.sub_init()
run(sub)
tap.ok_eq(1, 1, "1 and 1 are equal")
tap.ok_eq(1, 0, "1 and 0 aren't equal")
# tap.show_colors()
sub = tap.sub_init("fully ok subsuite")
for i in range(1, 15):
    sub.ok(i == i, "%d == %d" % (i, i))
sub.done()
sub = tap.sub_init("mostly fixed subsuite")
for i in range(1, 5):
    sub.ok(i == i & 3, "i = %d; Handling up to 3 with a bitmask of 3" % i, tap.TODO)
sub.done()
tap.verbose = 2
tap.ok_eq(1, 0, "Skipped failed test, (tap.verbose = 2)", tap.SKIP)
tap.done()
sys.exit(0)
