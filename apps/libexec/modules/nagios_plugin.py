import sys

STATE_OK = 0
OK = STATE_OK
STATE_WARNING = 1
WARNING = STATE_WARNING
STATE_CRITICAL = 2
CRITICAL = STATE_CRITICAL
STATE_UNKNOWN = 3
UNKNOWN = STATE_UNKNOWN

def worst_state(a, b):
	if a == CRITICAL or b == CRITICAL:
		return CRITICAL
	if a == WARNING or b == WARNING:
		return WARNING
	return max(a, b)


def best_state(a, b):
	val = worst_state(a, b)
	if val == a:
		return b
	return a


def state_name(state):
	if state == OK:
		return "OK"
	if state == CRITICAL:
		return "CRITICAL"
	if state == WARNING:
		return "WARNING"
	if state == UNKNOWN:
		return "UNKNOWN"
	return "Unknown status %d" % state


def state_code(state_name):
	str = state_name.tolower()
	if str.startswith('c'):
		return CRITICAL
	if str.startswith('w'):
		return WARNING
	if str.startswith('o'):
		return OK

	return UNKNOWN


def unknown(msg):
	die(UNKNOWN, msg)


def ok(msg):
	die(OK, msg)


def die(state, msg):
	print("%s: %s" % (state_name(state), msg))
	sys.exit(state)
