import time, os, sys
import random
import posix
import signal
import re
import copy
import subprocess

try:
	import hashlib
except ImportError:
	import sha as hashlib

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.insert(0, modpath)
from merlin_apps_utils import *
from merlin_test_utils import *
import merlin_db

import compound_config as cconf

__doc__ = """  %s%s!!! WARNING !!! WARNING !!! WARNING !!! WARNING !!! WARNING !!!%s

%sAll commands in this category can potentially overwrite configuration,
enable or disable monitoring and generate notifications. Do *NOT* use
these commands in a production environment.%s
""" % (color.yellow, color.bright, color.reset, color.red, color.reset)

config = {}
verbose = False
send_host_checks = True

class fake_peer_group:
	"""
	Represents one fake group of peered nodes, sharing the same
	object configuration
	"""
	def __init__(self, basepath, group_name, num_nodes=3, port=16000):
		self.nodes = []
		self.num_nodes = num_nodes
		self.group_name = group_name
		self.config_written = False
		self.master_groups = {}
		self.poller_groups = {}
		self.oconf_buf = False
		self.poller_oconf = ''
		i = 0
		while i < num_nodes:
			i += 1
			inst = fake_instance(basepath, "%s-%02d" % (group_name, i), port)
			self.nodes.append(inst)
			port += 1

		# add them as peers to each other
		for n in self.nodes:
			for node in self.nodes:
				if n == node:
					continue
				n.add_node('peer', node.name, port=node.port)

		print("Created peer group '%s' with %d nodes and base-port %d" %
			(group_name, num_nodes, port))
		return None

	def _add_oconf_object(self, otype, params):
		self.oconf_buf = 'define %s {\n' % otype
		for (k, v) in params.items():
			self.oconf_buf = "%s %s" % (k, v)


	def create_object_config(self, num_hosts=10, num_services_per_host=5):
		# master groups will call this for their pollers when
		# asked to create their own object configuration
		if self.oconf_buf:
			return True
		hostgroup = {
			'hostgroup_name': self.group_name,
			'alias': 'Alias for %s' % self.group_name
		}
		host = {
			'use': 'default-host-template',
			'host_name': '%s.@@host_num@@' % self.group_name,
			'alias': 'Just a stupid alias',
			'address': '127.0.0.1',
		}
		service = {
			'use': 'default-service',
			'service_description': 'lala-service.@@service_num@@',
		}

		new_objects = {
			'hostgroup': [hostgroup],
			'host': [],
			'service': [],
		}
		sdesc = copy.deepcopy(service['service_description'])
		hname = copy.deepcopy(host['host_name'])
		i = 0
		self.oconf_buf = 'define hostgroup {\n'
		for (k, v) in hostgroup.items():
			self.oconf_buf += '\t%s %s\n' % (k, v)
		self.oconf_buf += '}\n'
		while i < num_hosts:
			i += 1
			hobj = host
			hobj['host_name'] = hname.replace('@@host_num@@', "%04d" % i)
			self.oconf_buf += "define host{\n"
			for (k, v) in hobj.items():
				self.oconf_buf += "%s %s\n" % (k, v)
			if i < 5:
				self.oconf_buf += "hostgroups host%d_hosts,%s\n" % (i, self.group_name)
			else:
				self.oconf_buf += 'hostgroups %s\n' % self.group_name
			self.oconf_buf += "}\n"
			x = 0
			while x < num_services_per_host:
				x += 1
				sobj = service
				sobj['host_name'] = hobj['host_name']
				sobj['service_description'] = sdesc.replace('@@service_num@@', "%04d" % x)
				self.oconf_buf += "define service{\n"
				for (k, v) in sobj.items():
					self.oconf_buf += "%s %s\n" % (k, v)
				if x < 5:
					self.oconf_buf += 'service_groups service%d_services\n' % x
				self.oconf_buf += "}\n"

		poller_oconf_buf = ''
		pgroup_names = self.poller_groups.keys()
		pgroup_names.sort()
		for pgroup_name in pgroup_names:
			pgroup = self.poller_groups[pgroup_name]
			print("Generating config for poller group %s" % pgroup.group_name)
			pgroup.create_object_config(num_hosts, num_services_per_host)
			poller_oconf_buf += pgroup.oconf_buf

		self.oconf_buf = "%s\n%s" % (self.oconf_buf, poller_oconf_buf)
		for node in self.nodes:
			node.write_file('etc/oconf/generated.cfg', self.oconf_buf)
			node.write_file('etc/oconf/shared.cfg',
				test_config_in.shared_object_config)

		return True


	def add_master_group(self, mgroup):
		mgroup.poller_groups[self.group_name] = self
		self.master_groups[mgroup.group_name] = mgroup
		for poller in self.nodes:
			for master in mgroup.nodes:
				poller.add_node('master', master.name, port=master.port)
				master.add_node('poller', poller.name, port=poller.port)
		return True


class fake_instance:
	"""
	Represents one fake installation of Nagios, Merlin and mk_Livestatus
	and is responsible for creating directories and configurations for
	all of the above.
	"""
	def __init__(self, basepath, name, port=15551):
		self.name = name
		self.port = port
		self.home = "%s/%s" % (basepath, name)
		self.nodes = {}
		self.db_name = 'mdtest_%s' % name.replace('-', '_')
		self.substitutions = {
			'@@DIR@@': self.home,
			'@@NETWORK_PORT@@': "%d" % port,
			'@@DB_NAME@@': self.db_name,
		}
		self.merlin_config = test_config_in.merlin_config_in
		self.nagios_config = test_config_in.nagios_config_in
		self.nagios_cfg_path = "%s/etc/nagios.cfg" % self.home
		self.merlin_conf_path = "%s/merlin/merlin.conf" % self.home
		self.group_id = 0
		self.db = False
		self.pids = {}


	def start_daemons(self, nagios_binary, merlin_binary):
		nagios_cmd = [nagios_binary, '%s/etc/nagios.cfg' % self.home]
		merlin_cmd = [merlin_binary, '-d', '-c', '%s/merlin/merlin.conf' % self.home]
		self.pids['nagios'] = subprocess.Popen(nagios_cmd, stdout=subprocess.PIPE)
		self.pids['merlin'] = subprocess.Popen(merlin_cmd, stdout=subprocess.PIPE)


	def signal_daemons(self, sig):
		for name, proc in self.pids.items():
			proc.send_signal(sig)


	def shutdown_daemons(self):
		self.signal_daemons(signal.SIGTERM)


	def slay_daemons(self):
		self.signal_daemons(signal.SIGKILL)


	def add_subst(self, key, value):
		"""
		Key and value must both be strings, or weird things will happen
		"""
		self.substitutions[key] = value


	def add_node(self, node_type, node_name, **kwargs):
		"""
		Register a companion node, be it master, poller or peer
		"""
		if not self.nodes.get(node_type, False):
			self.nodes[node_type] = {}
		if not self.nodes[node_type].get(node_name, False):
			self.nodes[node_type][node_name] = {}

		for (k, v) in kwargs.items():
			self.nodes[node_type][node_name][k] = v
		if not self.nodes[node_type][node_name].get('address', False):
			self.nodes[node_type][node_name]['address'] = '127.0.0.1'
		port = kwargs.get('port', False)


	def create_core_config(self):
		print("Writing core config for %s" % self.name)
		configs = {}
		conode_types = self.nodes.keys()
		conode_types.sort()
		for ntype in conode_types:
			nodes = self.nodes[ntype]
			node_names = self.nodes[ntype].keys()
			node_names.sort()
			for name in node_names:
				nconf = ("%s %s {\n" % (ntype, name))
				node_vars = self.nodes[ntype][name]
				for (k, v) in node_vars.items():
					nconf += "\t%s = %s\n" % (k, v)
				if ntype == 'poller':
					group_name = name.split('-', 1)[0]
					nconf += "\thostgroup = %s\n" % group_name
				self.merlin_config += "%s}\n" % nconf

		configs[self.nagios_cfg_path] = self.nagios_config
		configs[self.merlin_conf_path] = self.merlin_config
		for (path, buf) in configs.items():
			for (key, value) in self.substitutions.items():
				buf = buf.replace(key, value)
			self.write_file(path, buf)
		return True


	def write_file(self, path, contents, mode=0644):
		if path[0] != '/':
			path = "%s/%s" % (self.home, path)
		f = open(path, 'w', mode)
		f.write(contents)
		f.close()
		return True

	def db_connect(self):
		if self.db:
			return True
		self.db = merlin_db.connect(self.mconf, False)
		self.dbc = self.db.cursor()


	def create_directories(self):
		dirs = [
			'etc/oconf', 'var/rw', 'var/archives',
			'var/spool/checkresults', 'var/spool/perfdata',
			'merlin/logs',
		]
		for d in dirs:
			mkdir_p("%s/%s" % (self.home, d))


def create_database(dbc, inst, schema_paths):
	try:
		dbc.execute('DROP DATABASE %s' % inst.db_name)
	except Exception, e:
		pass

	try:
		dbc.execute("CREATE DATABASE %s" % inst.db_name)
		dbc.execute("GRANT ALL ON %s.* TO merlin@'%%' IDENTIFIED BY 'merlin'" % inst.db_name)
		dbc.execute("USE merlin")
		dbc.execute('SHOW TABLES')
		for row in dbc.fetchall():
			# this isn't portable, but brings along indexes
			query = ('CREATE TABLE %s.%s LIKE %s' %
				(inst.db_name, row[0], row[0]))
			#query = ('CREATE TABLE %s.%s AS SELECT * FROM %s LIMIT 0' %
			#	(inst.db_name, row[0], row[0]))
			#print("Running query %s" % query)
			dbc.execute(query)
		print("Database %s for %s created properly" % (inst.db_name, inst.name))
	except Exception, e:
		print(e)
		sys.exit(1)
		return False


def cmd_dist(args):
	"""--basepath=<basepath>
	Tests various aspects of event forwarding with any number of
	hosts, services, peers and pollers, generating config and
	creating databases for the various instances and checking
	event distribution among them.
	"""
	basepath = '/tmp/merlin-dtest'
	merlin_mod_path = posix.getcwd()
	ocimp_path = "%s/ocimp" % merlin_mod_path
	livestatus_o = '/home/exon/git/monitor/livestatus/livestatus/src/livestatus.o'
	db_admin_user = 'exon'
	db_admin_password = False
	sql_schema_paths = []
	num_masters = 1
	poller_groups = 1
	pollers_per_group = 2
	merlin_binary = '%s/merlind' % merlin_mod_path
	nagios_binary = '/opt/monitor/bin/monitor'
	confgen_only = False
	for arg in args:
		if arg.startswith('--basepath='):
			basepath = arg.split('=', 1)[1]
		elif arg.endswith('.sql'):
			sql_schema_paths.append(arg)
		elif arg.startswith('--sql-user='):
			db_admin_user = arg.split('=', 1)[1]
		elif arg.startswith('--sql-pass=') or arg.startswith('--sql-passwd='):
			db_admin_password = arg.split('=', 1)[1]
		elif arg.startswith('--masters='):
			num_masters = int(arg.split('=', 1)[1])
		elif arg.startswith('--poller-groups='):
			poller_groups = int(arg.split('=', 1)[1])
		elif arg.startswith('--pollers-per-group='):
			pollers_per_group = int(arg.split('=', 1)[1])
		elif arg.startswith('--masters='):
			num_masters = int(arg.split('=', 1)[1])
		elif arg.startswith('--merlin-binary='):
			merlin_binary = arg.split('=', 1)[1]
		elif arg.startswith('--nagios-binary=') or arg.startswith('--monitor-binary='):
			nagios_binary = arg.split('=', 1)[1]
		elif arg == '--confgen-only':
			confgen_only = True
		else:
			prettyprint_docstring('dist', cmd_dist.__doc__,
				'Unknown argument: %s' % arg)
			sys.exit(1)

	# done parsing arguments, so get real paths to merlin and nagios
	if nagios_binary[0] != '/':
		nagios_binary = os.path.abspath(nagios_binary)
	if merlin_binary[0] != '/':
		merlin_binary = os.path.abspath(merlin_binary)

	# clear out playpen first
	os.system("rm -rf %s" % basepath)

	i = 1
	base_port = 16000
	port = base_port
	masters = fake_peer_group(basepath, 'master', num_masters, base_port)
	base_port += num_masters

	i = 0
	pgroups = []
	while i < poller_groups:
		i += 1
		group_name = "pg%d" % i
		pgroups.append(fake_peer_group(basepath, group_name, pollers_per_group, base_port))
		base_port += pollers_per_group

	instances = []
	for inst in masters.nodes:
		instances.append(inst)

	for pgroup in pgroups:
		pgroup.add_master_group(masters)
		for inst in pgroup.nodes:
			instances.append(inst)

	peer_groups = pgroups + [masters]

	mock_conf = mconf_mockup(host='localhost', user='exon', dbname='merlin', dbpass=False)
	db = merlin_db.connect(mock_conf)
	dbc = db.cursor()
	for inst in instances:
		inst.add_subst('@@OCIMP_PATH@@', ocimp_path)
		inst.add_subst('@@MODULE_PATH@@', merlin_mod_path)
		inst.add_subst('@@LIVESTATUS_O@@', livestatus_o)
		inst.create_directories()
		inst.create_core_config()
		if not confgen_only:
			create_database(dbc, inst, sql_schema_paths)

	masters.create_object_config()
	if confgen_only:
		sys.exit(0)

	for inst in instances:
		inst.start_daemons(nagios_binary, merlin_binary)

	# tests go here
	buf = sys.stdin.readline()

	for inst in instances:
		inst.shutdown_daemons()
		#dbc.execute("DROP DATABASE %s" % inst.db_name)

	# final round to nuke all remaining daemons
	for inst in instances:
		inst.slay_daemons()

	db.close()


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


class mconf_mockup:
	"""
	Mockup of merlin configuration object, used to handle multiple
	database connections from a single program
	"""
	dbopt = {}
	def __init__(self, **kwargs):
		self.dbopt = {}
		for k, v in kwargs.items():
			self.dbopt[k.replace('db', '')] = v


def cmd_pasv(args):
	"""
	Submits passive checkresults to the nagios.cmd pipe and
	verifies that the data gets written to database correctly
	and in a timely manner.
	Available options for 'mon test pasv'

	  --nagios-cfg=<file>   default /opt/monitor/var/etc/nagios.cfg
	  --counters=<int>      number of counters per object (default 30)
	  --hosts=<int>         number of hosts (default 1)
	  --services=<int>      number of services (default 5)
	  --loops=<int>         number of loops (default 1)
	  --interval=<int>      interval in seconds between loops (def 1800)
	  --delay=<int>         delay between submitting and checking (def 25)

	%s%s!!! WARNING !!!%s This command will disble active checks on your system.
	""" % (color.yellow, color.bright, color.reset)
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
		elif arg == '--verbose' or arg == '-v':
			verbose = True
		elif arg == '--nohostchecks':
			send_host_checks = False
		else:
			prettyprint_docstring("pasv", cmd_pasv.__doc__, "Unknown argument: %s" % arg)
			if arg == '--help' or arg == 'help':
				sys.exit(0)
			else:
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
		print("Can't run tests for %d hosts when only %d are configured" % (num_hosts, len(host_list)))

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
