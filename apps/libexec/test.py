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
from nagios_command import nagios_command
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
		self.expect_entries = {}
		self.objects = {
			'host': [], 'service': [],
			'hostgroup': [], 'servicegroup': [],
		}
		i = 0
		while i < num_nodes:
			i += 1
			inst = fake_instance(basepath, "%s-%02d" % (group_name, i), port)
			inst.group = self
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

	def add_object(self, otype, name):
		if not name in self.objects[otype]:
			self.objects[otype].append(name)
		# this way objects will filter up to master groups as well
		for mg in self.master_groups.values():
			mg.add_object(otype, name)


	def create_object_config(self, num_hosts=3, num_services_per_host=2):
		# master groups will call this for their pollers when
		# asked to create their own object configuration
		if self.oconf_buf:
			return True
		self.objects['hostgroup'].append(self.group_name)

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
			'service_description': 'service.@@service_num@@',
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
			self.add_object('host', hobj['host_name'])
			self.oconf_buf += "define host{\n"
			for (k, v) in hobj.items():
				self.oconf_buf += "%s %s\n" % (k, v)
			if i < 5:
				hg_name = 'host%d_hosts' % i
				self.oconf_buf += "hostgroups %s,%s\n" % (hg_name, self.group_name)
				self.add_object('hostgroup', hg_name)
			else:
				self.oconf_buf += 'hostgroups %s\n' % self.group_name
			self.oconf_buf += "}\n"
			x = 0
			while x < num_services_per_host:
				x += 1
				sobj = service
				sobj['host_name'] = hobj['host_name']
				sobj['service_description'] = sdesc.replace('@@service_num@@', "%04d" % x)
				sname = "%s;%s" % (sobj['host_name'], sobj['service_description'])
				self.add_object('service', sname)
				self.oconf_buf += "define service{\n"
				for (k, v) in sobj.items():
					self.oconf_buf += "%s %s\n" % (k, v)
				if x < 5:
					sg_name = 'service%d_services' % x
					self.oconf_buf += 'service_groups %s\n' % sg_name
					# only add each servicegroup once
					if i == 1:
						self.add_object('servicegroup', sg_name)
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

		print("Peer group %s objects:" % self.group_name)
		print("        hosts: %d" % len(self.objects['host']))
		print("     services: %d" % len(self.objects['service']))
		print("   hostgroups: %d" % len(self.objects['hostgroup']))
		print("servicegroups: %d" % len(self.objects['servicegroup']))
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
		self.group = False
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
		self.cmd_pipe = '%s/var/rw/nagios.cmd' % self.home
		self.cmd_object = nagios_command()
		self.cmd_object.set_pipe_path(self.cmd_pipe)

		# actually counts entries in program_status, and we'll have one
		self.num_nodes = 1

	def active_connections(self):
		"""
		Test to make sure all systems supposed to connect to this
		node have actually done so. This is fundamental in order
		to get all other tests to fly and does some initial setup
		which means it has to be run before all other tests.
		"""
		self.db_connect()
		self.dbc.execute('SELECT COUNT(1) FROM program_status WHERE is_running = 1')
		row = self.dbc.fetchall()
		return row[0][0]


	def submit_raw_command(self, cmd):
		if self.cmd_object.submit_raw(cmd) == False:
			print("Failed to submit command to %s" % self.cmd_pipe)
			return False
		return True

	def submit_command(self, name, **kwargs):
		if self.cmd_object.submit(name, kwargs) == False:
			print("Failed to submit %s to %s" % (name, self.cmd_pipe))
			return False
		return True

	def start_daemons(self, nagios_binary, merlin_binary):
		print("Launching daemons for instance %s" % self.name)
		nagios_cmd = [nagios_binary, '-d', '%s/etc/nagios.cfg' % self.home]
		merlin_cmd = [merlin_binary, '-c', '%s/merlin/merlin.conf' % self.home]
		self.pids['nagios'] = subprocess.Popen(nagios_cmd, stdout=subprocess.PIPE)
		self.pids['merlin'] = subprocess.Popen(merlin_cmd, stdout=subprocess.PIPE)


	def load_pids(self):
		if self.pids:
			return True
		paths = {
			'nagios': '%s/var/nagios.lock' % self.home,
			'merlin': '%s/merlin/merlind.pid' % self.home
		}
		for program, path in paths.items():
			f = open(path, 'r')
			buf = f.read().strip()
			self.pids[program] = int(buf)
		return True


	def signal_daemons(self, sig):
		self.load_pids()
		for name, pid in self.pids.items():
			try:
				os.kill(pid, sig)
			except OSError, e:
				if e.errno == errno.ESRCH:
					pass


	def stop_daemons(self):
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
		self.num_nodes += 1


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
		self.mconf = mconf_mockup(
			host='localhost', user='merlin',
			dbname=self.db_name, dbpass='merlin'
		)
		self.db = merlin_db.connect(self.mconf, False)
		self.dbc = self.db.cursor()


	def create_directories(self, extras=[]):
		dirs = [
			'etc/oconf', 'var/rw', 'var/archives',
			'var/spool/checkresults', 'var/spool/perfdata',
			'merlin/logs',
		]
		for d in set(dirs + extras):
			mkdir_p("%s/%s" % (self.home, d))


class fake_mesh:
	"""
	A set of peer groups that together constitute an entire network
	of nagios + merlin nodes, cooperating to do fake monitoring of
	a fake network for the sake of testing event propagation in
	different setups.
	"""
	def __init__(self, basepath, **kwargs):
		self.basepath = basepath
		self.baseport = 16000
		self.num_masters = 3
		self.poller_groups = 1
		self.pollers_per_group = 3
		self.instances = []
		self.db = False
		self.pgroups = []
		self.masters = []
		self.tap = tap("Merlin distribution tests")
		self.sleeptime = False

		for (k, v) in kwargs.items():
			setattr(self, k, v)


	def intermission(self, msg=False):
		"""Sleepytime between running of tests"""
		if self.sleeptime == False:
			self.sleeptime = 10 + (len(self.instances) * 5)
			if self.sleeptime > 30:
				self.sleeptime = 30

		# only print the animation if anyone's looking
		if os.isatty(sys.stdout.fileno()) == False:
			sys.write.stdout("Sleeping %d seconds")
			if msg:
				print("Sleeping %.2f seconds: %s" % (self.sleeptime, msg))
			else:
				print("Sleeping %.2f seconds" % self.sleeptime)
			time.sleep(self.sleeptime)

		i = self.sleeptime
		rotor = ['|', '/', '-', '\\']
		r = 0
		while i > 0:
			r += 1
			if msg:
				sys.stdout.write(" %c Sleeping %.2f seconds: %s    \r" %
					(rotor[r % len(rotor)], i, msg))
				sys.stdout.flush()
			else:
				sys.stdout.write(" %c Sleeping %.2f seconds   \r" %
					(rotor[r % len(rotor)], i))
				sys.stdout.flush()
			i -= 0.07
			time.sleep(0.07)
		print("   Sleeping 0.00 seconds")

	##############################################################
	#
	# Actual tests start here
	#
	# These first couple of tests must be run in the proper order,
	# and if one of them fails, we must break out early and print
	# an error, since the rest of the tests will be in unknown
	# state.
	#
	def test_connections(self):
		status = True
		for inst in self.instances:
			ret = inst.active_connections()
			self.tap.test(ret, inst.num_nodes, "%s has %d/%d connected systems" % (inst.name, ret, inst.num_nodes))
			if ret != inst.num_nodes:
				status = False

		return status

	def test_imports(self):
		"""make sure ocimp has run properly"""
		status = True
		for inst in self.instances:
			inst.dbc.execute("SELECT COUNT(1) FROM host")
			hosts = inst.dbc.fetchall()[0][0]
			inst.dbc.execute("SELECT COUNT(1) FROM service")
			services = inst.dbc.fetchall()[0][0]
			ret = self.tap.test(hosts, len(inst.group.objects['host']),
				'%s must import hosts properly' % inst.name)
			if ret == False:
				status = False
			ret = self.tap.test(services, len(inst.group.objects['service']),
				'%s must import services properly' % inst.name)
			if ret == False:
				status = False
		return status

	def test_global_commands(self):
		"""
		Sends a series of global commands to each master and makes
		sure they alter parameters on all connected peers and
		pollers.
		This is part of the setup, responsible for disabling active
		checks so we can control the state of all monitored objects
		ourselves, so it must be run first thing after we that all
		nodes are connected to each other.
		"""
		status = True
		raw_commands = [
			'DISABLE_FLAP_DETECTION',
			'DISABLE_FAILURE_PREDICTION',
			'STOP_EXECUTING_HOST_CHECKS',
			'STOP_EXECUTING_SVC_CHECKS',
			'STOP_OBSESSING_OVER_HOST_CHECKS',
			'STOP_OBSESSING_OVER_SVC_CHECKS',
			'START_ACCEPTING_PASSIVE_HOST_CHECKS',
			'START_ACCEPTING_PASSIVE_SVC_CHECKS',
		]
		# queries get 'SELECT COUNT(1) FROM program_status WHERE'
		# prepended and expect to get 0 as a proper answer
		queries = [
			'flap_detection_enabled = 1',
			'failure_prediction_enabled = 1',
			'active_host_checks_enabled = 1',
			'active_service_checks_enabled = 1',
			'obsess_over_hosts = 1',
			'obsess_over_services = 1',
			'passive_host_checks_enabled = 0',
			'passive_service_checks_enabled = 0',
		]
		master = self.masters.nodes[0]
		for cmd in raw_commands:
			ret = master.submit_raw_command(cmd)
			self.tap.test(ret, True, "Should be able to submit %s" % cmd)
		self.intermission("Letting global commands spread")
		i = 0
		for query in queries:
			cmd = raw_commands[i]
			i += 1
			for inst in self.instances:
				inst.dbc.execute('SELECT node_type, instance_name FROM program_status WHERE %s' % query)
				rows = inst.dbc.fetchall()
				ret = self.tap.test(len(rows), 0,
					"%s should spread and bounce to %s" % (cmd, inst.name))
				if ret == False:
					status = False
				if len(rows) != 0:
					print("  On '%s', command failed for the following systems:" % inst.name)
					for row in rows:
						print("    type: %d; name: %s" % (row[0], row[1]))
		return status


	def test_passive_checks(self):
		"""
		Submits a passive checkresult with status 2 (CRITICAL) for
		services and 1 (DOWN) for hosts.
		"""
		status = True
		master = self.masters.nodes[0]
		for host in self.masters.objects['host']:
			ret = master.submit_raw_command('PROCESS_HOST_CHECK_RESULT;%s;1;Plugin output for host %s' % (host, host))
			if ret == False:
				status = False
			ret = self.tap.test(ret, True, "Setting status of host %s" % host)
			if ret == False:
				status = False
		for srv in self.masters.objects['service']:
			ret = master.submit_raw_command('PROCESS_SERVICE_CHECK_RESULT;%s;2;Service plugin output' % (srv))
			if ret == False:
				status = False
			ret = self.tap.test(ret, True, "Setting status of service %s" % srv)
			if ret == False:
				status = False

		self.intermission('Letting passive checks spread')
		queries = {
			'host': 'SELECT COUNT(1) FROM host WHERE current_state != 1',
			'service': 'SELECT COUNT(1) FROM service WHERE current_state != 2',
		}
		for inst in self.instances:
			for otype, query in queries.items():
				inst.dbc.execute(query)
				value = inst.dbc.fetchall()[0][0]
				ret = (self.tap.test(value, len(inst.group.objects[otype]),
					'Passive %s checks should propagate to %s' %
						(otype, inst.name))
				)
				if ret == False:
					status = False

		return status


	def test_acks(self):
		"""
		Adds acknowledgement commands to various nodes in the network
		and makes sure they get propagated to the nodes that need to
		know about them.
		XXX: Should also spawn notifications and check for them
		"""
		master = self.masters.nodes[0]
		for host in self.masters.objects['host']:
			ret = master.submit_raw_command(
				'ACKNOWLEDGE_HOST_PROBLEM;%s;0;0;0;mon testsuite;ack comment for host %s'
				% (host, host)
			)
			self.tap.test(ret, True, "Acking %s on %s" % (host, master.name))
		for srv in self.masters.objects['service']:
			(_hst, _srv) = srv.split(';')
			ret = master.submit_raw_command(
				'ACKNOWLEDGE_SVC_PROBLEM;%s;0;0;0;mon testsuite;ack comment for service %s on %s'
				% (srv, _srv, _hst)
			)
			self.tap.test(ret, True, "Acking %s on %s" % (srv, master.name))

		# give all nodes some time before we check to make
		# sure the ack has spread
		self.intermission("Letting acks spread")
		for inst in self.instances:
			inst.dbc.execute("SELECT COUNT(1) FROM host WHERE problem_has_been_acknowledged = 1")
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['host']),
				"All host acks should register on %s" % inst.name)
			inst.dbc.execute('SELECT COUNT(1) FROM service WHERE problem_has_been_acknowledged = 1')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['service']),
				'All service acks should register on %s' % inst.name)

			inst.dbc.execute('SELECT COUNT(1) FROM comment_tbl WHERE entry_type = 4 AND comment_type = 1')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['host']), "Host acks should generate one comment each on %s" % inst.name)
			inst.dbc.execute('SELECT COUNT(1) FROM comment_tbl WHERE entry_type = 4 AND comment_type = 2')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['service']), 'Service acks should generate one comment each on %s' % inst.name)

		return None

	def get_first_poller(self, master):
		"""
		There's no real concept of 'first' here, since the instances is a dict,
		but we'll at least be consistent in that we return the same poller each
		time we're called.
		"""
		for name, p in master.group.poller_groups.items():
			return p.nodes[0]


	def _schedule_downtime(self, node, ignore={'host': {}, 'service': {}}):
		"""
		Schedules downtime for all objects on a particular node from that
		particular node.
		"""
		for host in node.group.objects['host']:
			if host in ignore['host']:
				continue
			ret = node.submit_raw_command(
				'%s;%s;%d;%d;1;0;54321;mon testsuite;downtime for host %s from %s' %
					('SCHEDULE_HOST_DOWNTIME', host, time.time(),
					time.time() + 54321, host, node.name)
			)
			self.tap.test(ret, True, "Scheduling downtime for %s on %s" %
				(host, node.name)
			)
		for srv in node.group.objects['service']:
			(_host_name, _service_description) = srv.split(';', 1)
			ret = node.submit_raw_command(
				'%s;%s;%d;%d;1;0;54321;mon testsuite;downtime for service %s on %s from %s' %
				('SCHEDULE_SVC_DOWNTIME', srv, time.time(),
				time.time() + 54321, _service_description, _host_name, node.name)
			)
			self.tap.test(ret, True, "Scheduling downtime for %s on %s" %
				(srv, node.name))

		return self.tap.failed == 0


	def test_downtime(self):
		"""
		Adds downtime scheduling commands to various nodes in the
		network and makes sure they get propagated to the nodes that
		need to know about them.

		Since downtime deletion works by deleting downtime with a
		specific id, we need to submit downtime to one poller-group
		first and then ignore that poller-group when submitting to
		the master, since we want to make sure things are getting
		deleted even if the id doesn't match.
		"""
		# stash to keep track of which objects we've already scheduled
		scheduled = {'host': {}, 'service': {}}


		master = self.masters.nodes[0]
		poller = self.get_first_poller(master)
		print("Submitting downtime to poller %s" % poller.name)
		self._schedule_downtime(poller)
		self._schedule_downtime(master, poller.group.objects)

		# give all nodes some time before we check to make
		# sure the ack has spread
		self.intermission("Letting downtime spread")
		for inst in self.instances:
			inst.dbc.execute("SELECT COUNT(1) FROM host WHERE scheduled_downtime_depth > 0")
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['host']),
				'All host downtime should spread to %s' % inst.name)
			inst.dbc.execute('SELECT COUNT(1) FROM service WHERE scheduled_downtime_depth > 0')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['service']),
				'All service downtime should spread to %s' % inst.name)
			inst.dbc.execute('SELECT COUNT(1) FROM comment_tbl WHERE comment_type = 1 AND entry_type = 2')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['host']),
				"Host downtime should generate one comment each on %s" % inst.name)
			inst.dbc.execute('SELECT COUNT(1) FROM comment_tbl WHERE comment_type = 2 AND entry_type = 2')
			value = inst.dbc.fetchall()[0][0]
			self.tap.test(value, len(inst.group.objects['service']),
				'Service downtime should generate one comment each on %s' % inst.name)
		return None


	def test_comments(self):
		"""
		Adds comment adding commands to various nodes in the network
		and makes sure they get propagated to the nodes that need to
		know about them.
		"""
		print("test_comments() is a stub")
		return None
	#
	# Actual tests end here
	#
	##########################################################

	def test_finalize(self):
		if self.tap.failed:
			print("passed: %s%d%s" % (color.green, self.tap.passed, color.reset))
			print("failed: %s%d%s" % (color.red, self.tap.failed, color.reset))
			return False
		print("All %s%d%s tests passed. Yay! :)" % (color.green, self.tap.passed, color.reset))
		return self.tap.failed == 0


	def start_daemons(self):
		try:
			pid = os.fork()
		except OSError, e:
			pid = -1
			pass

		if pid < 0:
			print("Failed to fork(): %s" % (os.strerror(os.errno)))
			return False

		if pid > 0:
			(ret, status) = os.waitpid(pid, 0)
			return True

		if not pid:
			print("in child. pid=%d; pgid=%d; sid=%d" %
				(os.getpid(), os.getpgid(os.getpid()), os.getsid(os.getpid())))
			# child becomes process group leader, starts a new session
			# and then starts the deamons
			os.setsid()
			for inst in self.instances:
				inst.start_daemons(self.nagios_binary, self.merlin_binary)
			sys.exit(0)


	def stop_daemons(self):
		for inst in self.instances:
			inst.stop_daemons()
		time.sleep(1)
		for inst in self.instances:
			inst.slay_daemons()


	def destroy_playground(self):
		time.sleep(1)
		os.system("rm -rf %s" % self.basepath)


	def destroy(self, destroy_databases=False):
		"""
		Shuts down and removes all traces of the fake mesh
		"""
		self.stop_daemons()
		if destroy_databases:
			self.destroy_databases()
		self.destroy_playground()


	def create_playground(self):
		"""
		Sets up the directories and configuration required for testing
		"""
		port = self.baseport
		self.masters = fake_peer_group(self.basepath, 'master', self.num_masters, port)
		port += self.num_masters
		i = 0
		self.pgroups = []
		while i < self.poller_groups:
			i += 1
			group_name = "pg%d" % i
			self.pgroups.append(fake_peer_group(self.basepath, group_name, self.pollers_per_group, port))
			port += self.pollers_per_group

		for inst in self.masters.nodes:
			self.instances.append(inst)

		for pgroup in self.pgroups:
			pgroup.add_master_group(self.masters)
			for inst in pgroup.nodes:
				self.instances.append(inst)

		self.groups = self.pgroups + [self.masters]

		for inst in self.instances:
			inst.add_subst('@@OCIMP_PATH@@', self.ocimp_path)
			inst.add_subst('@@MODULE_PATH@@', self.merlin_mod_path)
			inst.add_subst('@@LIVESTATUS_O@@', self.livestatus_o)
			inst.create_directories()
			inst.create_core_config()

		self.masters.create_object_config()


	def _destroy_database(self, inst, verbose=False):
		try:
			self.dbc.execute('DROP DATABASE %s' % inst.db_name)
		except Exception, e:
			if verbose:
				print("Failed to drop db %s for instance %s: %s" %
					(inst.db_name, inst.name, e))
			pass


	def destroy_databases(self, verbose=False):
		self.close_db()
		self.db = False
		self.connect_to_db()
		for inst in self.instances:
			self._destroy_database(inst, verbose)


	def _create_database(self, inst):
		self._destroy_database(inst)
		ret = []
		try:
			self.dbc.execute("CREATE DATABASE %s" % inst.db_name)
			self.dbc.execute("GRANT ALL ON %s.* TO merlin@'%%' IDENTIFIED BY 'merlin'" % inst.db_name)
			self.dbc.execute("USE merlin")
			self.dbc.execute('SHOW TABLES')
			for row in self.dbc.fetchall():
				tname = row[0]
				# indicates an autogenerated testing table
				if len(tname) >= 50 or tname[0] == tname[0].upper():
					continue
				# ninja tables aren't interesting, so skip those too
				if tname.startswith('ninja'):
					continue
				ret.append(tname)
				# this isn't portable, but brings along indexes
				query = ('CREATE TABLE %s.%s LIKE %s' %
					(inst.db_name, tname, tname))
				#query = ('CREATE TABLE %s.%s AS SELECT * FROM %s LIMIT 0' %
				#	(inst.db_name, tname, tname))
				#print("Running query %s" % query)
				self.dbc.execute(query)
			print("Database %s for %s created properly" % (inst.db_name, inst.name))

		except Exception, e:
			print(e)
			sys.exit(1)
			return False

		return ret


	def connect_to_db(self):
		"""Self-explanatory, really"""
		if self.db:
			return True
		mock_conf = mconf_mockup(host=self.db_host, user=self.db_user, dbname=self.db_name, dbpass=self.db_pass)
		self.db = merlin_db.connect(mock_conf)
		if not self.db:
			return False
		self.dbc = self.db.cursor()
		return True


	def create_databases(self):
		"""Sets up databases for all nodes in the mesh"""
		self.connect_to_db()
		for inst in self.instances:
			self._create_database(inst)


	def close_db(self):
		if not self.db:
			return True
		try:
			self.db.close()
			self.dbc = False
			self.db = False
		except Exception, e:
			pass


def _dist_shutdown(mesh, msg=False, batch=False, destroy_databases=False):
	if msg != False:
		print("%s" % msg)

	mesh.test_finalize()
	if batch == False:
		print("When done testing and examining, just press enter")
		buf = sys.stdin.readline()

	print("Stopping daemons")
	mesh.stop_daemons()
	if destroy_databases:
		print("Destroying databases")
		mesh.destroy_databases(True)
	print("Destroying on-disk mesh")
	mesh.destroy()
	print("Closing database connection")
	mesh.close_db()

	if mesh.tap.failed == 0:
		sys.exit(0)
	sys.exit(1)


def cmd_dist(args):
	"""[options]
	Where options can be any of the following:
	  --basepath=<basepath>     basepath to use
	  --sleeptime=<int>         seconds to sleep before testing
	  --masters=<int>           number of masters to create
	  --poller-groups=<int>     number of poller groups to create
	  --pollers-per-group=<int> number of pollers per created group
	  --merlin-binary=<path>    path to merlin daemon binary
	  --nagios-binary=<path>    path to nagios daemon binary
	  --destroy-databases       only destroy databases
	  --confgen-only            only generate configuration
	  --sql-admin-user=<user>   administrator user for MySQL
	  --sql-db-name=<db name>   name of database to use as template
	  --sql-host=<host>         ip-address or dns name of db host

	Tests various aspects of event forwarding with any number of
	hosts, services, peers and pollers, generating config and
	creating databases for the various instances and checking
	event distribution among them.
	"""
	setup = True
	destroy = True
	basepath = '/tmp/merlin-dtest'
	livestatus_o = '/home/exon/git/monitor/livestatus/livestatus/src/livestatus.o'
	db_admin_user = 'exon'
	db_admin_password = False
	db_name = 'merlin'
	db_host = 'localhost'
	sql_schema_paths = []
	num_masters = 3
	poller_groups = 1
	pollers_per_group = 3
	merlin_path = '/opt/monitor/op5/merlin'
	merlin_mod_path = '%s/merlin.so' % merlin_path
	merlin_binary = '%s/merlind' % merlin_path
	ocimp_path = "%s/ocimp" % merlin_path
	nagios_binary = '/opt/monitor/bin/monitor'
	confgen_only = False
	destroy_databases = False
	sleeptime = False
	batch = True
	if os.isatty(sys.stdout.fileno()):
		batch = False
	for arg in args:
		if arg == '--batch':
			batch = True
		elif arg.startswith('--basepath='):
			basepath = arg.split('=', 1)[1]
		elif arg.startswith('--sql-admin-user='):
			db_admin_user = arg.split('=', 1)[1]
		elif arg.startswith('--sql-pass=') or arg.startswith('--sql-passwd='):
			db_admin_password = arg.split('=', 1)[1]
		elif arg.startswith('--sql-db-name='):
			db_name = arg.split('=', 1)[1]
		elif arg.startswith('--sql-host='):
			db_host = arg.split('=', 1)[1]
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
		elif arg == '--destroy-databases':
			destroy_databases = True
		elif arg == '--confgen-only':
			confgen_only = True
		elif arg.startswith('--sleeptime='):
			sleeptime = arg.split('=', 1)[1]
			sleeptime = int(sleeptime)
		else:
			prettyprint_docstring('dist', cmd_dist.__doc__,
				'Unknown argument: %s' % arg)
			sys.exit(1)

	if sleeptime == False:
		sleeptime = 3 + (num_masters + (poller_groups * pollers_per_group))

	if not poller_groups or not pollers_per_group:
		print("Can't run tests with zero pollers")
		sys.exit(1)

	if num_masters < 2:
		print("Can't run proper tests with less than two masters")
		sys.exit(1)

	# done parsing arguments, so get real paths to merlin and nagios
	if nagios_binary[0] != '/':
		nagios_binary = os.path.abspath(nagios_binary)
	if merlin_binary[0] != '/':
		merlin_binary = os.path.abspath(merlin_binary)

	mesh = fake_mesh(
		basepath,
		nagios_binary=nagios_binary,
		merlin_binary=merlin_binary,
		num_masters=num_masters,
		poller_groups=poller_groups,
		pollers_per_group=pollers_per_group,
		merlin_mod_path=merlin_mod_path,
		ocimp_path=ocimp_path,
		livestatus_o=livestatus_o,
		db_user=db_admin_user,
		db_pass=db_admin_password,
		db_name=db_name,
		db_host=db_host,
	)
	mesh.create_playground()

	if confgen_only:
		sys.exit(0)


	mesh.create_databases()
	mesh.start_daemons()

	# tests go here. Important ones come first so we can
	# break out early in case one or more of the required
	# ones fail hard.
	mesh.intermission("Allowing nodes to connect to each other")
	if mesh.test_connections() == False:
		print("Connection tests failed. Bailing out")
		_dist_shutdown(mesh, 'Connection tests failed', batch)
	if mesh.test_imports() == False:
		_dist_shutdown(mesh, 'Imports failed. This is a known spurious error when running tests often', batch)
	if mesh.test_global_commands() == False:
		_dist_shutdown(mesh, 'Global command tests failed', batch)
	if mesh.test_passive_checks() == False:
		_dist_shutdown(mesh, 'Passive checks are broken', batch)

	# we only test acks if passive checks distribute properly
	mesh.test_acks()
	mesh.test_downtime()
	mesh.test_comments()

	_dist_shutdown(mesh, destroy_databases, batch)


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
