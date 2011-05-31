import time, os, sys
try:
	import hashlib
except ImportError:
	import sha as hashlib
if not hasattr(hashlib, 'sha'):
	import sha as hashlib
import random
import posix
import signal

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.insert(0, modpath)
from merlin_apps_utils import *
import merlin_conf as mconf
import merlin_db

import compound_config as cconf
config = {}
verbose = False
send_host_checks = True

def module_init(args):
	rem_args = []

	for arg in args:
		if arg.startswith('--merlin-cfg=') or arg.startswith('--merlin-conf='):
			mconf.config_file = arg.split('=', 1)[1]
		else:
			rem_args.append(arg)
			continue

	mconf.parse()
	return rem_args


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

def test_cmd(cmd_fd, cmd, msg=False):
	if msg != False:
		text = msg
	else:
		text = cmd

	full_cmd = "[%d] %s\n" % (time.time(), cmd)
	return test(os.write(cmd_fd, full_cmd), len(full_cmd), text)


class pasv_test:
	name = False
	hash = ''
	submit_time = 0
	table = ''
	where = ''

class pasv_test_host(pasv_test):
	table = 'host'
	cmd = "PROCESS_HOST_CHECK_RESULT"
	services = []

	def __init__(self, hostname):
		self.name = hostname

	def query(self):
		return ("%s WHERE host_name = '%s'" %
			(self.table, self.name))

class pasv_test_service(pasv_test):
	table = 'service'
	cmd = "PROCESS_SERVICE_CHECK_RESULT"
	host_name = False
	service_description = False

	def __init__(self, hostname, sdesc):
		self.host_name = hostname
		self.service_description = sdesc
		self.name = "%s;%s" % (hostname, sdesc)

	def query(self):
		return ("%s WHERE host_name = '%s' AND service_description = '%s'" %
			(self.table, self.host_name, self.service_description))


def _generate_counters(num_counters):
	unit = ['%', 's', '']
	i = 0
	cnt_list = []
	while i < num_counters:
		i += 1
		r = random.randint(0, 100)
		str = "COUNT%d=%d%s" % (i, r, unit[(i + r) % 3])
		cnt_list.append(str)

	return cnt_list


def _pasv_build_cmd(test, status):
	"""
	Builds the string for the passive check result
	"""

	return ("[%d] %s;%s;%d;" % (time.time(), test.cmd, test.name, status))


def _pasv_cmd_pipe_sighandler(one, two):
	print("No process is listening on the command pipe. Exiting")
	sys.exit(1)


def _pasv_open_cmdpipe(cmd_pipe):
	if not os.access(cmd_pipe, os.W_OK):
		print("%s doesn't exist or isn't writable. Exiting" % cmd_pipe)
		sys.exit(1)

	# pipes that aren't being read stall while we connect
	# to them, so we must add a stupid signal handler to
	# avoid hanging indefinitely in case Monitor isn't running.
	# Two seconds should be ample time for opening the pipe and
	# resetting the alarm before it goes off.
	signal.signal(signal.SIGALRM, _pasv_cmd_pipe_sighandler)
	signal.alarm(2)
	cmd_fd = os.open(cmd_pipe, posix.O_WRONLY)
	signal.alarm(0)
	return cmd_fd

def _pasv_help():
	print("""Available options for 'mon test pasv'

  --nagios-cfg=<file>   default /opt/monitor/var/etc/nagios.cfg
  --counters=<int>      number of counters per object (default 30)
  --hosts=<int>         number of hosts (default 1)
  --services=<int>      number of services (default 5)
  --loops=<int>         number of loops (default 1)
  --interval=<int>      interval in seconds between loops (def 1800)
  --delay=<int>         delay between submitting and checking (def 25)
	""")
	sys.exit(0)


def cmd_pasv(args):
	"""
	Submits passive checkresults to the nagios.cmd pipe and
	verifies that the data gets written to database correctly
	and in a timely manner.
	"""
	global verbose
	nagios_cfg = False
	num_hosts = 1
	num_services = 5
	num_loops = 1
	num_counters = 30
	interval = 1800
	delay = 25
	cmd_pipe = False
	global send_host_checks

	for arg in args:
		if arg.startswith('--nagios-cfg='):
			nagios_cfg = arg.split('=')[1]
		elif arg.startswith('--counters='):
			num_counters = int(arg.split('=')[1])
		elif arg.startswith('--hosts='):
			num_hosts = int(arg.split('=')[1])
		elif arg.startswith('--services='):
			num_services = int(arg.split('=')[1])
		elif arg.startswith('--loops='):
			num_loops = int(arg.split('=')[1])
		elif arg.startswith('--interval='):
			interval = int(arg.split('=')[1])
		elif arg.startswith('--delay='):
			delay = int(arg.split('=')[1])
		elif arg == '--help' or arg == 'help' or arg == '-h':
			_pasv_help()
		elif arg == '--verbose' or arg == '-v':
			verbose = True
		elif arg == '--nohostchecks':
			send_host_checks = False
		else:
			print("Unknown argument: %s" % arg)
			sys.exit(1)

	if nagios_cfg:
		comp = cconf.parse_conf(nagios_cfg)
		for v in comp.params:
			if v[0] == 'command_file':
				cmd_pipe = v[1]
				break

	db = merlin_db.connect(mconf)
	dbc = db.cursor()

	if not cmd_pipe:
		cmd_pipe = "/opt/monitor/var/rw/nagios.cmd"

	cmd_fd = _pasv_open_cmdpipe(cmd_pipe)
	# disable active checks while we test the passive ones, or
	# active checkresults might overwrite the passive ones and
	# contaminate the testresults
	test_cmd(cmd_fd, "STOP_EXECUTING_HOST_CHECKS")
	test_cmd(cmd_fd, "STOP_EXECUTING_SVC_CHECKS")
	test_cmd(cmd_fd, "START_ACCEPTING_PASSIVE_HOST_CHECKS")
	test_cmd(cmd_fd, "START_ACCEPTING_PASSIVE_SVC_CHECKS")
	os.close(cmd_fd)

	# now we update the database with impossible values so we
	# know we don't get the right test-data by mistake in case
	# the test-case is run multiple times directly following
	# each other
	dbc.execute("UPDATE host SET last_check = 5, current_state = 5")
	dbc.execute("UPDATE service SET last_check = 5, current_state = 5")

	host_list = []
	test_objs = []
	query = "SELECT host_name FROM host ORDER BY host_name ASC"
	dbc.execute(query)
	hi = 0
	# arbitrary (very) large value
	min_services = 100123098
	min_services_host = ''
	host_names = []
	for row in dbc.fetchall():
		if hi < num_hosts:
			obj = pasv_test_host(row[0])
			host_list.append(obj)
			if send_host_checks:
				test_objs.append(obj)
		hi += 1

	for host in host_list:
		query = ("SELECT service_description FROM service "
			"WHERE host_name = '%s' ORDER BY service_description ASC"
			% host.name)

		dbc.execute(query)
		services = 0
		si = 0
		for row in dbc.fetchall():
			if si < num_services:
				services += 1
				obj = pasv_test_service(host.name, row[0])
				host.services.append(obj)
				test_objs.append(obj)
			si += 1

		if services < min_services:
			min_services_host = host.name
			min_services = services

	if num_hosts > host_list:
		print("Can't run tests for %d hosts when only %d are configured" % (num_hosts, host_list.count()))

	if num_services > min_services:
		print("Can't run tests for %d services / host when %s has only %d configured"
			% (num_services, min_services_host, min_services))

	# primary testing loop
	loops = 0
	while loops < num_loops:
		loops += 1

		# generate the counters we'll be using.
		# We get fresh ones for each iteration
		counters = _generate_counters(num_counters)
		cnt_string = "%s" % " ".join(counters)
		cnt_hash = hashlib.sha(cnt_string).hexdigest()

		# why we have to disconnect from db and re-open the
		# command pipe is beyond me, but that's the way it is, it
		# seems. It also matches real-world use a lot better,
		# since the reader imitates ninja and the writer imitates
		# nsca.
		cmd_fd = _pasv_open_cmdpipe(cmd_pipe)
		merlin_db.disconnect()

		# new status every time so we can differ between the values
		# and also test the worst-case scenario where the daemon has
		# to run two queries for each passive checkresult
		status = loops % 3

		loop_start = time.time()
		print("Submitting passive check results (%s) @ %s" % (cnt_hash, time.time()))
		for t in test_objs:
			cmd = _pasv_build_cmd(t, status)
			cmd += "%s|%s\n" % (cnt_hash, cnt_string)
			t.cmd_hash = hashlib.sha(cmd).hexdigest()
			t.submit_time = time.time()
			result = os.write(cmd_fd, cmd)
			test(result, len(cmd), "%d of %d bytes written for %s" % (result, len(cmd), t.name))

		os.close(cmd_fd)
		db = merlin_db.connect(mconf)
		dbc = db.cursor()

		print("Sleeping %d seconds before reaping results" % delay)
		time.sleep(delay)
		for t in test_objs:
			query = ("SELECT "
				"last_check, current_state, output, perf_data "
				"FROM %s" % t.query())
			dbc.execute(query)
			row = dbc.fetchone()
			test(row[0] + delay > t.submit_time, True, "reasonable delay for %s" % t.name)
			test(row[1], status, "status updated for %s" % t.name)
			test(str(row[2]), cnt_hash, "output update for %s" % t.name)
			test(str(row[3]), cnt_string, "counter truncation check for %s" % t.name)

		if loops < num_loops:
			interval_sleep = (loop_start + interval) - time.time()
			if interval_sleep > 0:
				print("Sleeping %d seconds until next test-set" % interval_sleep)
				time.sleep(interval_sleep)

	total_tests = failed + passed
	print("failed: %d/%.3f%%" % (failed, float(failed * 100) / total_tests))
	print("passed: %d/%.3f%%" % (passed, float(passed * 100) / total_tests))
	print("total tests: %d" % total_tests)
