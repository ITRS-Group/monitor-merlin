import time, os, sys
import random
import posix
import signal
import re
import copy
import subprocess
import livestatus
import traceback

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
from qhcheck import QhChannel
import nagios_plugin as nplug

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
	def __init__(self, basepath, group_name, num_nodes=3, port=16000, **kwargs):
		self.nodes = []
		self.num_nodes = num_nodes
		self.group_name = group_name
		self.config_written = False
		self.master_groups = {}
		self.poller_groups = {}
		self.oconf_buf = False
		self.oconf_file = False
		self.poller_oconf = ''
		self.expect_entries = {}
		self.num_objects = {
			'host': 0, 'service': 0,
			'hostgroup': 0, 'servicegroup': 0,
		}
		self.have_objects = {
			'host': {}, 'service': {},
			'hostgroup': {}, 'servicegroup': {},
		}
		i = 0
		while i < num_nodes:
			i += 1
			inst = fake_instance(basepath, group_name, i, port, **kwargs)
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


	def add_object(self, otype, name):
		if self.have_objects[otype].get(name, False) == False:
			self.num_objects[otype] += 1
			self.have_objects[otype][name] = True
		# this way objects will filter up to master groups as well
		for mg in self.master_groups.values():
			mg.add_object(otype, name)


	def create_object_config(self, num_hosts=3, num_services_per_host=2):
		# master groups will call this for their pollers when
		# asked to create their own object configuration
		if self.oconf_file:
			return True

		self.oconf_file = self.nodes[0].get_path("etc/oconf/generated.cfg")
		f = self.nodes[0].create_file(self.oconf_file)

		if len(self.master_groups):
			self.group_type = 'poller'
		else:
			self.group_type = 'master'

		print("Generating object config for %s group %s" % (self.group_type, self.group_name))
		ocbuf = []

		self.add_object('hostgroup', self.group_name)

		hostgroup = {
			'hostgroup_name': self.group_name,
			'alias': 'Alias for %s' % self.group_name
		}
		host = {
			'use': 'default-host-template',
			'host_name': '%s.@@host_num@@' % self.group_name,
			'alias': 'Alias text',
			'address': '1',
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
		obuf = 'define hostgroup {\n'
		for (k, v) in hostgroup.items():
			obuf = "%s\t%s %s\n" % (obuf, k, v)

		obuf = "%s}" % obuf
		ocbuf.append(obuf)
		while i < num_hosts:
			i += 1
			if os.isatty(sys.stdout.fileno()) and not i % 7:
				sys.stdout.write("\r%d hosts and %d services created" % (i, i * num_services_per_host))

			hobj = host
			hobj['host_name'] = hname.replace('@@host_num@@', "%04d" % i)
			if i != 1:
				hobj['parents'] = self.group_name + '.0001'
			self.add_object('host', hobj['host_name'])
			obuf = "define host{\n"
			for (k, v) in hobj.items():
				obuf = "%s%s %s\n" % (obuf, k, v)
			if i & 7:
				hg_name = 'host%d_hosts' % (i & 7)
				obuf += "hostgroups %s,%s\n" % (hg_name, self.group_name)
				self.add_object('hostgroup', hg_name)
			else:
				obuf += 'hostgroups %s\n' % self.group_name
			obuf += "}"
			ocbuf.append(obuf)
			x = 0
			while x < num_services_per_host:
				x += 1
				sobj = service
				sobj['host_name'] = hobj['host_name']
				sobj['service_description'] = sdesc.replace('@@service_num@@', "%04d" % x)
				sname = "%s;%s" % (sobj['host_name'], sobj['service_description'])
				self.add_object('service', sname)
				obuf = "define service{\n"
				for (k, v) in sobj.items():
					obuf = "%s%s %s\n" % (obuf, k, v)
				if x & 7:
					sg_name = 'service%d_services' % (x & 7)
					obuf = '%sservicegroups %s\n' % (obuf, sg_name)
					# only add each servicegroup once
					if i == 1:
						self.add_object('servicegroup', sg_name)
				obuf += "}"
				ocbuf.append(obuf)

		print("\r%d hosts and %d services created" % (i, i * num_services_per_host))
		self.oconf_buf = "\n".join(ocbuf)
		ocbuf = False
		poller_oconf_buf = ''
		pgroup_names = self.poller_groups.keys()
		pgroup_names.sort()
		for pgroup_name in pgroup_names:
			pgroup = self.poller_groups[pgroup_name]
			pgroup.create_object_config(num_hosts, num_services_per_host)
			poller_oconf_buf += pgroup.oconf_buf

		for otype in self.have_objects.keys():
			self.have_objects[otype] = self.have_objects[otype].keys()
			self.have_objects[otype].sort()

		self.oconf_buf = "%s\n%s" % (self.oconf_buf, poller_oconf_buf)
		for node in self.nodes:
			node.write_file('etc/oconf/generated.cfg', self.oconf_buf)

		print("Peer group %s objects:" % self.group_name)
		print("        hosts: %d" % self.num_objects['host'])
		print("     services: %d" % self.num_objects['service'])
		print("   hostgroups: %d" % self.num_objects['hostgroup'])
		print("servicegroups: %d" % self.num_objects['servicegroup'])
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
	def __init__(self, basepath, group_name, group_id, port=15551, **kwargs):
		self.valgrind = False
		self.group = False
		self.group_name = group_name
		self.group_id = group_id
		self.name = "%s-%02d" % (group_name, group_id)
		self.port = port
		self.home = "%s/%s" % (basepath, self.name)
		self.nodes = {}
		self.db_name = 'mdtest_%s' % self.name.replace('-', '_')
		self.substitutions = {
			'@@DIR@@': self.home,
			'@@NETWORK_PORT@@': "%d" % port,
			'@@DB_NAME@@': self.db_name,
			'@@BASEPATH@@': basepath,
			'@@NODENAME@@': self.name,
		}
		self.merlin_config = test_config_in.merlin_config_in
		self.nagios_config = test_config_in.nagios_config_in
		self.nagios_cfg_path = "%s/etc/nagios.cfg" % self.home
		self.merlin_conf_path = "%s/merlin/merlin.conf" % self.home
		self.group_id = 0
		self.db = False
		self.proc = {}
		self.cmd_pipe = '%s/var/rw/nagios.cmd' % self.home
		self.cmd_object = nagios_command()
		self.cmd_object.set_pipe_path(self.cmd_pipe)

		# actually counts entries in program_status, and we'll have one
		self.num_nodes = 1
		live_path = os.path.join(self.home, 'var', 'rw', 'live')
		self.live = livestatus.SingleSiteConnection('unix://%s' % live_path)
		if kwargs.get('valgrind'):
			self.valgrind = True

	def stat(self, path):
		return os.stat('%s/%s' (self.home, path))

	def fsize(self, path):
		st = os.stat('%s/%s' % (self.home, path))
		return st.st_size

	def get_nodeinfo(self):
		"""
		Grab this node's nodeinfo output and stash it per-system
		in a 'nodeinfo' list
		"""
		self.nodeinfo = []
		qh_path = '%s/%s' % (self.home, '/var/rw/nagios.qh')
		channel = QhChannel('merlin', qh_path, subscribe=False)
		for response in channel.query('nodeinfo'):
			self.nodeinfo.append(response)
		self.info = self.nodeinfo[0]

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

	def start_daemons(self, daemons, dname=False):
		if dname:
			print("Launching %s daemon for instance %s" % (dname, self.name))
		else:
			print("Launching daemons for instance %s" % self.name)
		fd = os.open("/dev/null", os.O_WRONLY)
		for name, program in daemons.items():
			if dname and name != dname:
				continue

			if self.proc.get(name):
				print("ERROR: %s already started, so not relaunching it" % name)
				sys.exit(1)

			if name == 'nagios':
				cmd = [program, '%s/etc/nagios.cfg' % self.home]
			elif name == 'merlin':
				cmd = [program, '-d', '-c', '%s/merlin/merlin.conf' % self.home]

			if self.valgrind:
				real_cmd = ['valgrind', '--child-silent-after-fork=yes', '--leak-check=full', '--log-file=%s/valgrind.log' % self.home] + cmd
				cmd = real_cmd
			self.proc[name] = subprocess.Popen(cmd, stdout=fd, stderr=fd)

		# do NOT close 'fd' here, or we'll end up getting a bazillion
		# "Inappropriate ioctl for device" due to a lot of failed
		# isatty() calls in the logging functions


	def signal_daemons(self, sig, dname=False):
		for name, proc in self.proc.items():
			remove = sig == signal.SIGKILL
			if dname and dname != name:
				continue
			try:
				os.kill(proc.pid, sig)
			except OSError, e:
				if e.errno == errno.ESRCH:
					remove = True
					pass
			if remove:
				self.proc.pop(name)


	def stop_daemons(self, dname=False):
		self.signal_daemons(signal.SIGTERM, dname)


	def slay_daemons(self, dname=False):
		self.signal_daemons(signal.SIGKILL, dname)


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
		configs["%s/etc/oconf/shared.cfg" % (self.home)] = test_config_in.shared_object_config
		for (path, buf) in configs.items():
			for (key, value) in self.substitutions.items():
				buf = buf.replace(key, value)
			self.write_file(path, buf)
		return True


	def create_file(self, path, mode=0644):
		return open(self.get_path(path), 'w', mode)

	def get_path(self, path):
		if path[0] != '/':
			path = "%s/%s" % (self.home, path)
		return path

	def write_file(self, path, contents, mode=0644):
		f = self.create_file(path, mode)
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
		self.valgrind = False
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
		self.use_database = False
		self.progs = {}

		for (k, v) in kwargs.items():
			if k.startswith('prog_'):
				pname = k[5:]
				self.progs[pname] = v
				if not os.access(v, os.X_OK):
					print("Failed to access '%s'" % v)
					sys.exit(1)
			else:
				setattr(self, k, v)

	def signal_daemons(self, signo):
		"""Sends the designated signal to all attached daemons"""
		for inst in self.instances:
			inst.signal_daemons(signo)

	def intermission(self, msg, sleeptime=False):
		"""Sleepytime between running of tests"""
		if self.sleeptime == False:
			self.sleeptime = 10
			#+ (len(self.instances) * 5)
			if self.sleeptime > 30:
				self.sleeptime = 30

		if sleeptime == False:
			sleeptime = self.sleeptime

		if self.valgrind:
			sleeptime *= 4

		# only print the animation if anyone's looking
		if os.isatty(sys.stdout.fileno()) == False:
			if msg:
				print("Sleeping %.2f seconds: %s" % (sleeptime, msg))
			else:
				print("Sleeping %.2f seconds" % sleeptime)
			time.sleep(sleeptime)
		else:
			i = sleeptime
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
			print("   Slept for %.2f seconds: %s      " % (sleeptime, msg))

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
		start_failed = self.tap.failed

		for inst in self.instances:
			inst.get_nodeinfo()

			exp_peer_id = int(inst.name.split('-', 1)[1]) - 1
			exp_num_peers = inst.group.num_nodes - 1

			self.tap.test(int(inst.info.peer_id), exp_peer_id,
				'%s should have peer-id %d' % (inst.name, exp_peer_id))
			self.tap.test(int(inst.info.configured_peers), exp_num_peers,
				'%s should have %d peers' % (inst.name, exp_num_peers))

			calc_con = int(inst.info.active_peers) + int(inst.info.active_pollers) + int(inst.info.active_masters)
			if inst.info.state == 'STATE_CONNECTED':
				calc_con += 1
			tot_con = 0
			for i in inst.nodeinfo:
				if self.tap.test(i.state, 'STATE_CONNECTED', "%s -> %s connection" % (inst.name, i.name)):
					tot_con += 1
				else:
					# no point pressing on if we're not connected
					continue
				if i.type == 'peer':
					self.tap.test(i.peer_id, i.self_assigned_peer_id, "%s -> %s peer id agreement" % (inst.name, i.name))
				if int(i.instance_id) == 0:
					continue
				self.tap.test(i.active_peers, i.configured_peers,
					"%s thinks %s has %s/%s active peers" %
					(inst.name, i.name, i.active_peers, i.configured_peers))

				self.tap.test(i.active_pollers, i.configured_pollers,
					"%s thinks %s has %s/%s active pollers" %
					(inst.name, i.name, i.active_pollers, i.configured_pollers))

				self.tap.test(i.active_masters, i.configured_masters,
					"%s thinks %s has %s/%s active masters" %
					(inst.name, i.name, i.active_masters, i.configured_masters))

			self.tap.test(tot_con, len(self.instances), "%s has %d/%d connected systems" %
				(inst.name, tot_con, len(self.instances)))
			self.tap.test(tot_con, calc_con, "%s should count connections properly" % inst.name)


		if self.tap.failed > start_failed:
			status = False
		return status

	def test_imports(self):
		"""make sure ocimp has run properly"""
		if not self.use_database:
			return True
		status = True
		for inst in self.instances:
			res = inst.dbc.execute('SELECT COUNT(1) FROM timeperiod')
			row = inst.dbc.fetchone()
			tps = row[0]
			ret = self.tap.test(tps, 2, "%s must import timeperiods" % inst.name)
			if ret == False:
				status = False
		return status


	def test_daemon_restarts(self):
		status = True
		for inst in self.instances:
			inst.nslog_size = inst.fsize('nagios.log')

		for n, discard in self.progs.items():
			self.stop_daemons(n)
			self.intermission("Letting %s daemons die" % n, 3)
			self.start_daemons(n)
			self.intermission("Letting systems reconnect and renegotiate")
			ret = self.test_connections()
			if ret == False:
				status = False
		for inst in self.instances:
			lsize = inst.fsize('nagios.log')
			self.tap.test(lsize > inst.nslog_size, True, "nagios.log must not be truncated")
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
			'STOP_EXECUTING_HOST_CHECKS',
			'STOP_EXECUTING_SVC_CHECKS',
			'STOP_OBSESSING_OVER_HOST_CHECKS',
			'STOP_OBSESSING_OVER_SVC_CHECKS',
			'START_ACCEPTING_PASSIVE_HOST_CHECKS',
			'START_ACCEPTING_PASSIVE_SVC_CHECKS',
			'ENABLE_NOTIFICATIONS',
		]

		queries = [
			'enable_flap_detection = 1',
			'execute_host_checks = 1',
			'execute_service_checks = 1',
			'obsess_over_hosts = 1',
			'obsess_over_services = 1',
			'accept_passive_host_checks = 0',
			'accept_passive_service_checks = 0',
			'enable_notifications = 0',
		]
		master = self.masters.nodes[0]
		for cmd in raw_commands:
			ret = master.submit_raw_command(cmd)
			self.tap.test(ret, True, "Should be able to submit %s" % cmd)
		self.intermission("Letting global commands spread", 10)
		i = 0
		for query in queries:
			cmd = raw_commands[i]
			i += 1
			for inst in self.instances:
				lq = 'GET status\nFilter: %s\nColumns: %s' % (query, query.split('=')[0].strip())
				live_result = inst.live.query(lq)
				ret = self.tap.test(len(live_result), 0,
					"%s should spread and bounce to %s" % (cmd, inst.name))
				if ret == False:
					status = False
					print("  Command failed on '%s'" % inst.name)
					print("  query was:")
					print("%s" % lq)
		return status


	def test_passive_checks(self):
		"""
		Submits a passive checkresult with status 2 (CRITICAL) for
		services and 1 (DOWN) for hosts.
		"""
		status = True
		master = self.masters.nodes[0]
		for host in self.masters.have_objects['host']:
			ret = master.submit_raw_command('PROCESS_HOST_CHECK_RESULT;%s;1;Plugin output for host %s' % (host, host))
			if ret == False:
				status = False
			ret = self.tap.test(ret, True, "Setting status of host %s" % host)
			if ret == False:
				status = False
		for srv in self.masters.have_objects['service']:
			ret = master.submit_raw_command('PROCESS_SERVICE_CHECK_RESULT;%s;2;Service plugin output' % (srv))
			if ret == False:
				status = False
			ret = self.tap.test(ret, True, "Setting status of service %s" % srv)
			if ret == False:
				status = False

		self.intermission('Letting passive checks spread', 15)
		queries = {
			'host': 'GET hosts\nStats: state = 1',
			'service': 'GET services\nStats: state = 2',
		}
		for inst in self.instances:
			for otype, query in queries.items():
				value = inst.live.query(query)[0][0]
				ret = (self.tap.test(value, len(inst.group.have_objects[otype]),
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
		for host in self.masters.have_objects['host']:
			ret = master.submit_raw_command(
				'ACKNOWLEDGE_HOST_PROBLEM;%s;0;0;0;mon testsuite;ack comment for host %s'
				% (host, host)
			)
			self.tap.test(ret, True, "Acking %s on %s" % (host, master.name))
		for srv in self.masters.have_objects['service']:
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
			value = inst.live.query('GET hosts\nStats: acknowledged = 1')[0][0]
			self.tap.test(value, len(inst.group.have_objects['host']),
				"All host acks should register on %s" % inst.name)

			value = inst.live.query('GET services\nStats: acknowledged = 1')[0][0]
			self.tap.test(value, len(inst.group.have_objects['service']),
				'All service acks should register on %s' % inst.name)

			value = inst.live.query('GET comments\nStats: entry_type = 4\nStats: type = 1\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['host']), "Host acks should generate one comment each on %s" % inst.name)
			value = inst.live.query('GET comments\nStats: entry_type = 4\nStats: type = 2\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['service']), 'Service acks should generate one comment each on %s' % inst.name)

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
		for host in node.group.have_objects['host']:
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
			ret = node.submit_raw_command(
				'SCHEDULE_HOST_DOWNTIME;%s;%d;%d;0;0;10;mon testsuite;Flexible downtime for host %s from %s' %
					(host, time.time() + 1, time.time() + 5,
					host, node.name)
			)
			self.tap.test(ret, True, "Adding flexible downtime for host %s from %s" % (host, node.name))


		for srv in node.group.have_objects['service']:
			if srv in ignore['service']:
				continue
			(_host_name, _service_description) = srv.split(';', 1)
			ret = node.submit_raw_command(
				'%s;%s;%d;%d;1;0;54321;mon testsuite;downtime for service %s on %s from %s' %
				('SCHEDULE_SVC_DOWNTIME', srv, time.time(),
				time.time() + 54321, _service_description, _host_name, node.name)
			)
			self.tap.test(ret, True, "Scheduling downtime for %s on %s" %
				(srv, node.name))
			ret = node.submit_raw_command(
				'SCHEDULE_SVC_DOWNTIME;%s;%d;%d;0;0;10;mon testsuite;Flexible downtime for service %s on %s from %s' %
					(srv, time.time() + 1, time.time() + 5,
					_service_description, _host_name, node.name)
			)
			self.tap.test(ret, True, "Adding flexible downtime for service %s on %s" % (srv, node.name))

		return self.tap.failed == 0

	def _unschedule_downtime(self, node, host_ids, svc_ids):
		ret = True
		for dt in host_ids:
			ret &= node.submit_raw_command('DEL_HOST_DOWNTIME;%d' % dt);
		for dt in svc_ids:
			ret &= node.submit_raw_command('DEL_SVC_DOWNTIME;%d' % dt);
		self.tap.test(ret, True, "Deleting downtimes on %s" % (node.name))


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
		self._schedule_downtime(master, poller.group.have_objects)

		# give all nodes some time before we check to make
		# sure the ack has spread
		self.intermission("Letting downtime spread")
		for inst in self.instances:
			value = inst.live.query('GET hosts\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, len(inst.group.have_objects['host']),
				'All host downtime should spread to %s' % inst.name)

			value = inst.live.query('GET services\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, len(inst.group.have_objects['service']),
				'All service downtime should spread to %s' % inst.name)

			value = inst.live.query('GET comments\nStats: type = 1\nStats: entry_type = 2\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['host']),
				"Host downtime should generate one comment each on %s" % inst.name)
			value = inst.live.query('GET comments\nStats: type = 2\nStats: entry_type = 2\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['service']),
				'Service downtime should generate one comment each on %s' % inst.name)

		host_downtimes = [x[0] for x in master.live.query('GET downtimes\nColumns: id\nFilter: is_service = 0')]
		service_downtimes = [x[0] for x in master.live.query('GET downtimes\nColumns: id\nFilter: is_service = 1')]
		self._unschedule_downtime(master, host_downtimes, service_downtimes)
		self.intermission("Letting downtime deletion spread")
		for inst in self.instances:
			value = inst.live.query('GET hosts\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, 0,
				'All host downtime should be gone on %s' % inst.name)

			value = inst.live.query('GET services\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, 0,
				'All service downtime should be gone on %s' % inst.name)

		print("Submitting propagating downtime to master %s" % master.name)
		master.submit_raw_command('SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME;%s;%d;%d;%d;%d;%d;%s;%s' %
			(poller.group_name + '.0001', time.time(), time.time() + 54321, 1, 0, 0, poller.group_name + '.0001', master.name))
		self.intermission("Letting downtime spread")
		for inst in self.masters.nodes:
			value = inst.live.query('GET hosts\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, len(poller.group.have_objects['host']),
				'All host downtime should spread to %s' % inst.name)
		parent_downtime = master.live.query('GET downtimes\nColumns: id\nFilter: host_name = %s\nFilter: is_service = 0' %
		    (poller.group_name + '.0001'))[0]
		self._unschedule_downtime(master, parent_downtime, [])
		self.intermission("Letting downtime deletion spread")
		for inst in self.instances:
			value = inst.live.query('GET hosts\nStats: scheduled_downtime_depth > 0')[0][0]
			self.tap.test(value, 0,
				'All host downtime should be gone on %s' % inst.name)

		return None


	def _add_comments(self, node, ignore={'host': {}, 'service': {}}):
		"""
		Schedules downtime for all objects on a particular node from that
		particular node.
		"""
		for host in node.group.have_objects['host']:
			if host in ignore['host']:
				continue
			ret = node.submit_raw_command(
				'%s;%s;1;mon testsuite;comment for host %s from %s' %
					('ADD_HOST_COMMENT', host, host, node.name)
			)
			self.tap.test(ret, True, "Adding comment for %s from %s" %
				(host, node.name)
			)
		for srv in node.group.have_objects['service']:
			if srv in ignore['service']:
				continue
			(_host_name, _service_description) = srv.split(';', 1)
			ret = node.submit_raw_command(
				'%s;%s;1;mon testsuite;comment for %s on %s from %s' %
				('ADD_SVC_COMMENT', srv,
				_service_description, _host_name, node.name)
			)
			self.tap.test(ret, True, "Adding comment for service %s on %s from %s" %
				(_service_description, _host_name, node.name))

		return self.tap.failed == 0

	def test_comments(self):
		"""
		Adds comment adding commands to various nodes in the network
		and makes sure they get propagated to the nodes that need to
		know about them.
		"""
		master = self.masters.nodes[0]
		poller = self.get_first_poller(master)
		print("Submitting comments to poller %s" % poller.name)
		self._add_comments(poller)
		self._add_comments(master, poller.group.have_objects)

		# give all nodes some time before we check to make
		# sure the ack has spread
		self.intermission("Letting comments spread")
		for inst in self.instances:
			value = inst.live.query('GET comments\nStats: type = 1\nStats: entry_type = 1\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['host']),
				"Host comments should spread to %s" % inst.name)
			value = inst.live.query('GET comments\nStats: type = 2\nStats: entry_type = 1\nStatsAnd: 2')[0][0]
			self.tap.test(value, len(inst.group.have_objects['service']),
				'Service comments should spread to %s' % inst.name)
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


	def start_daemons(self, dname=False, stagger=True):
		for inst in self.instances:
			inst.start_daemons(self.progs, dname)
			if stagger:
				self.intermission("Staggering daemon starts to enfore peer id's", 0.25)
		return


	def stop_daemons(self, dname=False):
		for inst in self.instances:
			inst.stop_daemons(dname)
		time.sleep(0.3)
		for inst in self.instances:
			inst.slay_daemons(dname)


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


	def create_playground(self, num_hosts=3, num_services_per_host=5):
		"""
		Sets up the directories and configuration required for testing
		"""
		port = self.baseport
		self.masters = fake_peer_group(self.basepath, 'master', self.num_masters, port, valgrind = self.valgrind)
		port += self.num_masters
		i = 0
		self.pgroups = []
		while i < self.poller_groups:
			i += 1
			group_name = "pg%d" % i
			self.pgroups.append(fake_peer_group(self.basepath, group_name, self.pollers_per_group, port, valgrind = self.valgrind))
			port += self.pollers_per_group

		for inst in self.masters.nodes:
			self.instances.append(inst)
			inst.valgrind = self.valgrind

		for pgroup in self.pgroups:
			pgroup.add_master_group(self.masters)
			for inst in pgroup.nodes:
				self.instances.append(inst)
				inst.valgrind = self.valgrind

		self.groups = self.pgroups + [self.masters]

		for inst in self.instances:
			inst.add_subst('@@OCIMP_PATH@@', self.ocimp_path)
			inst.add_subst('@@MODULE_PATH@@', self.merlin_mod_path)
			inst.add_subst('@@LIVESTATUS_O@@', self.livestatus_o)
			inst.create_directories()
			inst.create_core_config()

		self.masters.create_object_config(num_hosts, num_services_per_host)


	def _destroy_database(self, inst, verbose=False):
		if not self.use_database:
			return
		try:
			self.dbc.execute('DROP DATABASE %s' % inst.db_name)
		except Exception, e:
			if verbose:
				print("Failed to drop db %s for instance %s: %s" %
					(inst.db_name, inst.name, e))
			pass


	def destroy_databases(self, verbose=False):
		if not self.use_database:
			return
		self.close_db()
		self.db = False
		self.connect_to_db()
		for inst in self.instances:
			self._destroy_database(inst, verbose)


	def _create_database(self, inst):
		if not self.use_database:
			return
		self._destroy_database(inst)
		ret = []
		try:
			self.dbc.execute("CREATE DATABASE %s" % inst.db_name)
			self.dbc.execute("GRANT ALL ON %s.* TO merlin@'%%' IDENTIFIED BY 'merlin'" % inst.db_name)
			self.dbc.execute("USE merlin")
			self.dbc.execute('SHOW TABLES')
			for row in self.dbc.fetchall():
				tname = row[0]
				# avoid autogenerated testing tables
				if tname[0] == tname[0].upper():
					continue
				if len(tname) >= 30 and tname != 'serviceescalation_contactgroup':
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
		"""
		Sets up databases for all nodes in the mesh, and makes
		sure each node is connected to 'their' database.
		"""
		if not self.use_database:
			return
		self.connect_to_db()
		for inst in self.instances:
			self._create_database(inst)
		for inst in self.instances:
			inst.db_connect()


	def close_db(self):
		if not self.db:
			return True
		try:
			print("Closing database connection")
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
	mesh.close_db()

	if mesh.tap.failed == 0:
		sys.exit(0)
	sys.exit(1)


dist_test_mesh = False
def dist_test_sighandler(signo, stackframe):
	print("Caught signal %d" % signo)
	if dist_test_mesh == False:
		sys.exit(1)
	print("Killing leftover daemons")
	dist_test_mesh.stop_daemons()
	sys.exit(1)

def ocount_compare(have, expected):
	expect = []
	ret = {'fail': [], 'pass': []}
	for exp in expected:
		ary = exp.split('=')
		k = '='.join(ary[:-1])
		v = int(ary[-1])
		expect.append([k, v])

	for (k, v) in expect:
		exp = have.get(k, 0)
		if v == exp:
			ret['pass'].append([k, v, exp])
		else:
			ret['fail'].append([k, v, exp])
	return ret

def cmd_ocount(args):
	"""path [<path2> <pathN>] [[--expect=]name=<int>]
	Counts objects of each specific type in 'path' and prints them in
	shell eval()'able style to stdout in sorted order.
	Note that
	  somekey=0
	will yield a passing test if somekey isn't found in the result.
	Also note that we can't parse files with equal signs in them.
	"""
	expect = []
	path = []
	for arg in args:
		if '=' in arg:
			expect.append(arg)
		else:
			path.append(arg)

	if not len(path):
		print("No path specified")
		sys.exit(1)

	ret = {}
	for p in path:
		xret = cconf.count_compound_types(p)
		for k, v in xret.items():
			if ret.get(k, False) == False:
				ret[k] = 0
			ret[k] += v

	total = 0

	result = ocount_compare(ret, expect)

	# sort alphabetically
	otypes = list(ret.items())
	otypes.sort()
	for t, num in otypes:
		total += num
		print("%s=%d" % (t, num))
	print("passed=%d" % (len(result['pass'])))
	print("failed=%d" % (len(result['fail'])))
	if len(result['fail']):
		sys.exit(1)
	sys.exit(0)


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
	  --hosts=<int>             hosts per peer-group (3)
	  --services-per-host=<int> services per host (5)
	  --valgrind                run binaries through valgrind

	Tests various aspects of event forwarding with any number of
	hosts, services, peers and pollers, generating config and
	creating databases for the various instances and checking
	event distribution among them.
	"""
	global dist_test_mesh

	num_hosts = 3
	num_services_per_host = 6
	setup = True
	destroy = True
	basepath = '/tmp/merlin-dtest'
	livestatus_o = '/opt/monitor/op5/livestatus/livestatus.o'
	db_admin_user = 'root'
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
	use_database = False
	valgrind = False
	if os.isatty(sys.stdout.fileno()):
		batch = False
	for arg in args:
		if arg == '--batch':
			batch = True
		elif arg.startswith('--basepath='):
			basepath = arg.split('=', 1)[1]
		elif arg.startswith('--use-database='):
			use_database = bool(int(arg.split('=')[1]))
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
		elif arg.startswith('--hosts='):
			num_hosts = arg.split('=', 1)[1]
			num_hosts = int(num_hosts)
		elif arg.startswith('--services-per-host='):
			num_services_per_host = arg.split('=', 1)[1]
			num_services_per_host = int(num_services_per_host)
		elif arg.startswith('--valgrind'):
			valgrind = True
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
		prog_merlin=merlin_binary,
		prog_nagios=nagios_binary,
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
		use_database=use_database,
		sleeptime=sleeptime,
		valgrind=valgrind,
	)
	mesh.create_playground(num_hosts, num_services_per_host)

	if confgen_only:
		sys.exit(0)


	mesh.create_databases()
	mesh.start_daemons()
	dist_test_mesh = mesh
	signal.signal(signal.SIGINT, dist_test_sighandler)

	# tests go here. Important ones come first so we can
	# break out early in case one or more of the required
	# ones fail hard.
	try:
		mesh.intermission("Allowing nodes to connect to each other", 10)
		if mesh.test_connections() == False:
			print("Connection tests failed. Bailing out")
			_dist_shutdown(mesh, 'Connection tests failed', batch)

		if mesh.test_imports() == False:
			_dist_shutdown(mesh, 'Imports failed. This is a known spurious error when running tests often', batch)

		mesh.intermission("Stabilizing all daemon connections", 20)

		if mesh.test_global_commands() == False:
			_dist_shutdown(mesh, 'Global command tests failed', batch)
		if mesh.test_passive_checks() == False:
			_dist_shutdown(mesh, 'Passive checks are broken', batch)

		# we only test acks if passive checks distribute properly
		mesh.test_downtime()
		mesh.test_acks()
		mesh.test_comments()

		# restart tests come last, as we now have some state to read back in
		if mesh.test_daemon_restarts() == False:
			print("Daemon restart tests failed. Bailing out")
			_dist_shutdown(mesh, 'Restart tests failed', batch)

	except SystemExit:
		# Some of the helper functions call sys.exit(1) to bail out.
		# Let's assume they take care of cleaning up before doing so
		raise
	except:
		# Don't leave stuff running, just because we messed up
		print '*'*40
		print 'Exception while running tests:'
		traceback.print_exc()
		print '*'*40
		mesh.tap.failed = True
		_dist_shutdown(mesh, destroy_databases, batch)
		raise

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

	!!! WARNING !!!     !!! WARNING !!!
	This command will disble active checks on your system and have other
	side-effects as well.
	!!! WARNING !!!     !!! WARNING !!!
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


def mark(path, mark_name='mark', params=[], oneline=False):
	"""The business end of 'cmd_mark'"""

	if not mark_name:
		mark_name = 'mark'

	if oneline:
		s = "%d %s %s\n" % (time.time(), mark_name, ' '.join(params))
	else:
		params.insert(0, 'timestamp=%d' % time.time())
		s = "%s {\n\t%s\n}\n" % (mark_name, '\n\t'.join(params))

	f = open(path, "a")
	f.write(s)
	f.close()


def cmd_mark(args):
	"""--mark-name=<name> [--mark-file=<logfile>] <key1=value1> <keyN=valueN>

	Adds a compound entry into the file pointed to by '--mark-file'.
	This can be used to let a tester know when some event occurred.

	Each key=value pair becomes a key=value pair in the written compound,
	with 'timestamp=<unix-timestamp>' added as the first entry.
	"""

	path = False
	params = []
	sep = "\t"
	mark_type = 'hostmark'
	mark_name = False
	oneline = False

	for arg in args:
		if arg.startswith('--mark-name=') or arg.startswith('--name'):
			mark_name = arg.split('=', 1)[1]
		elif arg.startswith('--mark-file=') or arg.startswith('--file'):
			path = arg.split('=')[1]
		elif arg.startswith('--field-sep='):
			sep = arg.split('=')[1]
			if sep == '\n':
				sep = "\n"
		elif arg == '--oneline':
			oneline = True
		else:
			if not path and arg.startswith('log='):
				path = os.path.dirname(arg.split('=', 1)[1]) + '/marks.log'
			if not mark_name and arg.startswith('SERVICE'):
				mark_type = 'servicemark'
			params.append(arg)

	if not mark_name:
		mark_name = mark_type

	if not path:
		prettyprint_docstring('mark', cmd_mark.__doc__,
			'No path parameter supplied. Where do I put my mark?')
		sys.exit(1)

	mark(path, mark_name, params, oneline)
	sys.exit(0)


def build_output(what, args):
	if len(args):
		msg = "%s: %s" % (what, ' '.join(args))
	else:
		msg = '%s: Static mon test output' % what
	return msg


def cmd_check(args):
	"""[<path>] [options]
	Options can be any of
	  --state=<str>       The state we should exit with
	  --perfdata=<str>    The perfdata string we should print
	  --output=<str>      The output we should print
	  --mark-file=<path>  The file we should leave a mark in when running
	  --mark-name=<str>   The 'name' of the mark we should leave
	  --oneline           Leave a oneline mark
	If path exists, it should point to a file looking like this:
		state=CRITICAL
		output=Some plugin output
		perfdata=<valid performance data>
	and its data will be used to set the check state.
	If it doesn't exist, its last element, separated by dashes (-)
	will be considered a state-name for us to use.
	If no arguments are passed, a random state will be used.
	"""
	path = False
	perfdata = ''
	output = False
	mark_file = False
	mark_name = False
	mark_params = []
	oneline = False
	for arg in args:
		if arg.startswith('--state='):
			stext = arg.split('=', 1)[1]
			state = nplug.state_code(stext)
		elif arg.startswith('--perfdata='):
			perfdata += arg.split('=')[1]
		elif arg.startswith('--output='):
			output = arg.split('=')[1]
		elif arg.startswith('--mark-file='):
			mark_file = arg.split('=', 1)[1]
		elif arg.startswith('--mark-name='):
			mark_name = arg.split('=', 1)[1]
		elif arg.startswith('--oneline'):
			oneline = True
		else:
			path = arg

	if not path:
		path = '/dev/null/check-random'

	stext = "unset"

	f = False
	if path:
		try:
			f = open(path)
		except:
			f = False
			pass

	if not f:
		stext = path.split('-')[-1].upper()
		if stext.lower() == 'random':
			state = random.randint(0, 3)
			stext = nplug.state_name(state)
			output = "Randomized check"
	else:
		for line in f:
			line = line.strip()
			if line.startswith('state='):
				stext = line.split('=', 1)[1].upper()
				state = nplug.state_code(stext)
			elif line.startswith('output='):
				output = line.split('=', 1)[1]
			elif line.startswith('perfdata='):
				perfdata = line.split('=', 1)[1]

	if not output:
		output = ' '.join(args)
	if perfdata:
		print(build_output(stext, [output, '|', perfdata]))
	else:
		print(build_output(stext, [output]))

	if mark_file:
		params = []
		if perfdata:
			params.append('perfdata=%s' % perfdata)
		params.append("state=%s" % stext)
		params.append('output=%s' % output)
		mark(mark_file, mark_name, params, oneline)

	sys.exit(nplug.state_code(stext))
