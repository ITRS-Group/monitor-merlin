from merlin_apps_utils import *

failed = 0
passed = 0
def test(a, b, msg):
	global passed, failed

	if a == b:
		if verbose:
			print("  %sok%s   %s" % (color.green, color.reset, msg))
		passed += 1
	else:
		print("  %sfail%s %s" % (color.red, color.reset, msg))
		print(a, b)
		failed += 1
