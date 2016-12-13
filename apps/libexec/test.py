import time, os, sys
import errno
import random
import posix
import signal
import re
import copy
import subprocess
import livestatus
import traceback
from pprint import *
import traceback

try:
	import hashlib
except ImportError:
	import sha as hashlib

from merlin_apps_utils import *
from merlin_test_utils import *
from nagios_command import nagios_command
import merlin_db
from qhcheck import QhChannel
import nagios_plugin as nplug
import compound_config as cconf

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
modpytap_path = modpath + '/pytap'
if not modpytap_path in sys.path:
	sys.path.insert(0, modpytap_path)
import pytap

SIGNALS_TO_NAMES_DICT = dict((getattr(signal, n), n) \
    for n in dir(signal) if n.startswith('SIG') and '_' not in n )

__doc__ = """  %s%s!!! WARNING !!! WARNING !!! WARNING !!! WARNING !!! WARNING !!!%s

%sAll commands in this category can potentially overwrite configuration,
enable or disable monitoring and generate notifications. Do *NOT* use
these commands in a production environment.%s
""" % (color.yellow, color.bright, color.reset, color.red, color.reset)

config = {}
verbose = False
send_host_checks = True

baseport = 16000

class fake_peer_group:
	"""
	Represents one fake group of peered nodes, sharing the same
	object configuration
	"""
	def __init__(self, basepath, group_name, num_nodes=3, port=0, **kwargs):
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
		self.pg_name = group_name
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


	def create_object_config(self, num_hosts, num_services_per_host):
		# master groups will call this for their pollers when
		# asked to create their own object configuration
		if self.oconf_file:
			return True

		if len(self.master_groups):
			self.group_type = 'poller'
		else:
			self.group_type = 'master'
		print("Generating object config for %s group %s" % (self.group_type, self.group_name))
		if len(self.master_groups):
			print("  Poller group. Symlinking config for master to generate")
			for node in self.nodes:
				src = "%s/config/%s.cfg" % (self.mesh.oconf_cache_dir, node.name)
				dst = node.get_path("etc/oconf/from-master.cfg")
				print("  Symlinking %s to %s" % (src, dst))
				os.symlink(src, dst)


		if not num_hosts:
			num_hosts = 1 + (len(self.nodes) * 2)
		if not num_services_per_host:
			num_services_per_host = len(self.nodes)
		self.oconf_file = self.nodes[0].get_path("etc/oconf/generated.cfg")
		if not len(self.master_groups):
			f = self.nodes[0].create_file(self.oconf_file)

		ocbuf = []

		self.add_object('hostgroup', self.group_name)

		hostgroup = {
			'hostgroup_name': self.group_name,
			'alias': 'Alias for %s' % self.group_name
		}
		host = {
			'use': 'mtest-default-host-template',
			'host_name': '%s.@@host_num@@' % self.group_name,
			'alias': 'Alias text',
			'address': '1',
		}
		service = {
			'use': 'mtest-default-service-template',
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
			if num_hosts > 50 and os.isatty(sys.stdout.fileno()) and not i % 7:
				sys.stdout.write("\r%d hosts and %d services created" % (i, i * num_services_per_host))

			hobj = host
			hobj['host_name'] = hname.replace('@@host_num@@', "%04d" % i)
			if i == 1:
				hobj['check_command'] = 'check-host-tier%d' % i
				hobj['max_check_attempts'] = '4'
			elif i == 2:
				hobj['parents'] = self.group_name + '.0001'
				hobj['check_command'] = 'check-host-tier%d' % i
				hobj['max_check_attempts'] = '3'
			elif i > 2:
				hobj['parents'] = self.group_name + '.0002'
				hobj['check_command'] = 'check-host-tier3'

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

		if num_hosts > 50:
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
		if not len(self.master_groups):
			for node in self.nodes:
				node.write_file('etc/oconf/generated.cfg', self.oconf_buf)

		print("Peer group %s objects:" % self.group_name)
		print("  hosts=%d; services=%d; hostgroups=%d; servicegroups=%d" %
			(self.num_objects['host'], self.num_objects['service'],
			self.num_objects['hostgroup'], self.num_objects['servicegroup']))
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
	def __init__(self, basepath, group_name, group_id, port=0, **kwargs):
		self.valgrind_log = {}
		self.valgrind = False
		self.group = False
		self.group_name = group_name
		self.group_id = group_id
		self.name = "%s-%02d" % (group_name, group_id)
		# Use powers of 2 as port number increment. By the laws
		# of exponential numbers, that ensures that A+D!=B+C for
		# any distinct positive integers A, B, C and D that are
		# either 1 or a power of our base.
		# This is nifty since merlin uses ip+port to distinguish
		# one node from another, but tests run on the same system,
		# so there a fixed source port is used which is calculated
		# thus: connecting_node->port + listening_node->port
		self.port = baseport + (2**port)
		self.home = "%s/%s" % (basepath, self.name)
		self.nodes = {}
		self.db_name = 'mdtest_%s' % self.name.replace('-', '_')
		self.substitutions = {
			'@@DIR@@': self.home,
			'@@NETWORK_PORT@@': "%d" % self.port,
			'@@DB_NAME@@': self.db_name,
			'@@BASEPATH@@': basepath,
			'@@NODENAME@@': self.name,
		}
		self.merlin_config = test_config_in.merlin_config_in
		self.nagios_config = test_config_in.nagios_config_in
		self.macro_config = test_config_in.macro_config_in
		self.nagios_cfg_path = "%s/etc/nagios.cfg" % self.home
		self.merlin_conf_path = "%s/merlin/merlin.conf" % self.home
		self.macro_cfg_path = "%s/etc/macros.cfg" % self.home
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

	def fpath(self, path):
		return "%s/%s" % (self.home, path)

	def fsize(self, path):
		st = os.stat(self.fpath(path))
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
		fd = os.open("/dev/null", os.O_WRONLY)
		for name, program in daemons.items():
			if dname and name != dname:
				continue

			if self.proc.get(name):
				print("ERROR: %s already started, so not relaunching it" % name)
				sys.exit(1)

			if name == 'naemon':
				cmd = [program, '%s/etc/nagios.cfg' % self.home]
			elif name == 'merlin':
				cmd = [program, '-d', '-c', '%s/merlin/merlin.conf' % self.home]

			if self.valgrind:
				# get a unique log-id for this daemon
				log_id = self.valgrind_log.get(name, 0)
				self.valgrind_log[name] = log_id + 1

				real_cmd = ['valgrind', '--child-silent-after-fork=yes', '--leak-check=full', '--log-file=%s/valgrind.%s.%d' % (self.home, name, log_id)] + cmd
				cmd = real_cmd
			try:
				self.proc[name] = subprocess.Popen(cmd, stdout=fd, stderr=fd)
			except OSError, e:
				print("Failed to run command '%s'" % cmd)
				print(e)
				raise OSError(e)

		# do NOT close 'fd' here, or we'll end up getting a bazillion
		# "Inappropriate ioctl for device" due to a lot of failed
		# isatty() calls in the logging functions


	def signal_daemons(self, sig, dname=False):
		remove = False
		for name, proc in self.proc.items():
			if dname and dname != name:
				continue
			try:
				os.kill(proc.pid, sig)
			except OSError, e:
				alive = False
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
		configs = {}
		if self.name.startswith('pg1'):
			self.substitutions['#@@MERLIN_MODULE_EXTRAS@@'] = 'notifies = no'
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
					if group_name == 'pg1':
						nconf += "\tnotifies = no\n"
				self.merlin_config += "%s}\n" % nconf

		configs[self.nagios_cfg_path] = self.nagios_config
		configs[self.merlin_conf_path] = self.merlin_config
		configs[self.macro_cfg_path] = self.macro_config
		if not len(self.group.master_groups):
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
		self.shutting_down = False
		self.valgrind = False
		self.basepath = basepath
		self.num_masters = 3
		self.opt_pgroups = [5,3,1]
		self.instances = []
		self.db = False
		self.pgroups = []
		self.masters = []
		self.tap = pytap.pytap("Merlin distribution tests")
		self.tap.verbose = 1
		self.sleeptime = False
		self.use_database = False
		self.progs = {}
		self.oconf_cache_dir = False

		for (k, v) in kwargs.items():
			if k.startswith('prog_'):
				pname = k[5:]
				self.progs[pname] = v
				if not os.access(v, os.X_OK):
					print("Failed to access '%s'" % v)
					sys.exit(1)
			else:
				setattr(self, k, v)
		if self.valgrind:
			self.valgrind_multiplier = 5
		else:
			self.valgrind_multiplier = 1
		self._create_nodes()


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

		sleeptime *= self.valgrind_multiplier

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


	def _clear_notification_log(self):
		for inst in self.instances:
			for path in [inst.fpath('hnotify.log'), inst.fpath('snotify.log')]:
				try:
					os.remove(path)
				except OSError:
					# Most often file doesn't exist, thus pass. Other cases,
					# we catch the problem in next test case
					pass

	##############################################################
	#
	# Actual tests start here
	#
	# These first couple of tests must be run in the proper order,
	# and if one of them fails, we must break out early and print
	# an error, since the rest of the tests will be in unknown
	# state.
	def _test_proc_alive(self, proc, exp, sig_ok, msg_prefix, sub):
		msg = msg_prefix
		flags = (os.WNOHANG, 0)[self.shutting_down]

		try:
			result = os.waitpid(proc.pid, flags)
		except OSError, e:
			if e.errno == errno.ECHILD:
				return sub.test(False, exp, msg)
			return sub.fail("EXCEPTION for '%s': %s" % (msg, e))

		pid, status = result
		if not pid and not status:
			return sub.test(True, exp, msg)

		# we got a pid and a status, so check what happened
		if os.WIFEXITED(status):
			msg = "%s: %d exited with status %d" % (msg, proc.pid, os.WEXITSTATUS(status))
			if exp == False:
				return sub.test(os.WEXITSTATUS(status), 0, msg)
			return sub.fail(msg)

		if os.WIFSIGNALED(status):
			if exp == False and os.WTERMSIG(status) in sig_ok:
				clr = color.green
			else:
				clr = color.bright_red
			msg = "%s: %sGot sig %s%s" % (msg, clr, SIGNALS_TO_NAMES_DICT.get(os.WTERMSIG(status), os.WTERMSIG(status)), color.reset)
			if os.WCOREDUMP(status):
				msg = "%s (%score dumped%s)" % (msg, color.bright_red, color.reset)
				return sub.fail(msg)
			if exp == False and os.WTERMSIG(status) in sig_ok:
				return sub.ok(True, msg)
			return sub.test(os.WTERMSIG(status) in sig_ok, True, msg)

		msg = "%s: pid=%d; status=%d; %sunknown exit reason%s" % (msg, pid, status, color.red, color.reset)
		sub.fail(msg)


	def test_alive(self, sub=False, **kwargs):
		"""Tests to make sure daemons are still alive"""
		daemon = False
		what = False
		expect = True
		sig_ok = []
		arg_sub = sub

		for k, v in kwargs.items():
			if k == 'daemon' and v != False:
				daemon = v
			elif k == 'expect':
				expect = v
			elif k == 'sig_ok':
				if type(v) == type([]):
					sig_ok = v
				else:
					sig_ok = [v]

		if expect == True:
			what = 'aliveness'
			how = "must live"
		else:
			what = 'shutdown'
			how = "must exit nicely"

		if not sub:
			if daemon:
				sub = self.tap.sub_init("%s %s" % (daemon, what))
			else:
				sub = self.tap.sub_init("daemon %s" % (what))

		# let daemons start properly before we check them,
		# or tests will sporadically fail on multi-core
		# systems
		time.sleep(0.5)
		for inst in self.instances:
			for dname, proc in inst.proc.items():
				if daemon and dname != daemon:
					continue
				self._test_proc_alive(proc, expect, sig_ok, "%s on %s %s" % (dname, inst.name, how), sub)

		if arg_sub == False:
			ret = sub.done()
		else:
			ret = sub.get_status()
		if ret != 0:
			self.tap.fail('Daemons are misbehaving when they %s. Bad daemons!' % how)
			self.shutdown('Daemons are misbehaving when they %s. Bad daemons!' % how)
			return False
		return True


	def _test_connections(self, sub, **kwargs):
		old_verbose = sub.verbose
		sub.verbose = 1
		self.test_alive(sub)
		sub.verbose = old_verbose
		for inst in self.instances:
			inst.get_nodeinfo()

			exp_peer_id = int(inst.name.split('-', 1)[1]) - 1
			exp_num_peers = inst.group.num_nodes - 1

			sub.test(int(inst.info.peer_id), exp_peer_id,
				'%s should have peer-id %d' % (inst.name, exp_peer_id))
			sub.test(int(inst.info.configured_peers), exp_num_peers,
				'%s should have %d peers' % (inst.name, exp_num_peers))

			calc_con = int(inst.info.active_peers) + int(inst.info.active_pollers) + int(inst.info.active_masters)
			if inst.info.state == 'STATE_CONNECTED':
				calc_con += 1
			tot_con = 0
			for i in inst.nodeinfo:
				if sub.test(i.state, 'STATE_CONNECTED', "%s -> %s connection" % (inst.name, i.name)):
					tot_con += 1
				if i.type == 'peer':
					sub.test(i.peer_id, i.self_assigned_peer_id, "%s -> %s peer id agreement" % (inst.name, i.name))
				if int(i.instance_id) == 0:
					continue
				sub.test(i.active_peers, i.configured_peers,
					"%s thinks %s has %s/%s active peers" %
					(inst.name, i.name, i.active_peers, i.configured_peers))

				sub.test(i.active_pollers, i.configured_pollers,
					"%s thinks %s has %s/%s active pollers" %
					(inst.name, i.name, i.active_pollers, i.configured_pollers))

				sub.test(i.active_masters, i.configured_masters,
					"%s thinks %s has %s/%s active masters" %
					(inst.name, i.name, i.active_masters, i.configured_masters))
				if i.type != "local" and i.type != "master":
					result = i.expected_config_hash != ("0" * 40)
					sub.test(result, True, "expected_config_hash must be set")
				result = i.config_hash != ("0" * 40)
				sub.test(result, True, "config_hash must be set")

			sub.test(tot_con, inst.num_nodes, "%s has %d/%d connected systems" %
				(inst.name, tot_con, inst.num_nodes))
			sub.test(tot_con, calc_con, "%s should count connections properly" % inst.name)
		return sub.get_status() == 0

	def test_connections(self):
		"""Tests connections between nodes in the mesh.
		This is a mandatory test"""
		return self._test_until_or_fail('connections', self._test_connections, 45)


	def test_oconfsplit(self, dir):
		"""Verifies that configuration split works properly"""
		sub = self.tap.sub_init("poller configuration split")
		fd = os.open("/dev/null", os.O_WRONLY)
		for inst in self.instances:
			cfg_path = "%s/%s.cfg" % (dir, inst.name)
			ret = os.access(cfg_path, os.F_OK)
			if not len(inst.group.master_groups):
				sub.test(ret, False, "%s should not have a generated config" % inst.name)
				continue
			sub.test(ret, True, "%s should have a generated config" % inst.name)
			# as minimal as possible
			naemon_cfg = [
				'cfg_file=%s.cfg' % inst.name,
				'log_file=/tmp/montestdist.log',
				'check_result_path=/tmp',
				'illegal_macro_output_chars=!$<>|'
			]
			naemon_cfg_path = "%s/%s-naemon.conf" % (dir, inst.name)
			fp = open(naemon_cfg_path, "w")
			fp.write("\n".join(naemon_cfg) + "\n")
			fp.flush()
			fp.close()
			cmd = ['naemon', '-v', '-v', naemon_cfg_path]
			proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
			out, err = proc.communicate()
			ret = proc.wait()
			sub.test(ret, 0, "%s should be a valid config" % (cfg_path))
			sub.diag("COMMAND:")
			sub.diag(cmd)
			if len(out):
				sub.diag("Naemon STDOUT:")
				sub.diag(out.split('\n'))
			if len(err):
				sub.diag("Naemon STDERR:")
				sub.diag(err.split('\n'))

		return sub.done()

	def _test_restarts(self, daemon, deathtime=3, recontime=3, stagger=True):
		status = True
		self.stop_daemons(daemon, deathtime)
		self.start_daemons(daemon, stagger)
		return self.test_connections()

	def test_merlin_restarts(self):
		"""Tests merlin restarts"""
		return self._test_restarts('merlin', 3, 10)

	def test_nagios_restarts(self):
		"""Verifies that nodes reconnect after a core restart
		Also tests for log truncation and config pushing
		"""

		for inst in self.instances:
			inst.nslog_size = inst.fsize('nagios.log')
		ret = self._test_restarts('naemon', 8, 3)
		for inst in self.instances:
			lsize = inst.fsize('nagios.log')
			self.tap.test(lsize > inst.nslog_size, True, "%s must keep nagios.log" % inst.name)

		for inst in self.instances:
			inst.nslog_size = inst.fsize('nagios.log')
		self.stop_daemons('naemon', 8)
		self.start_daemons('naemon', 3)
		for inst in self.instances:
			lsize = inst.fsize('nagios.log')
			self.tap.test(lsize > inst.nslog_size, True, "%s must keep nagios.log" % inst.name)

		return ret

	def _test_parents(self, sub, predicate):
		"""test that parent notifications work as intended"""
		for inst in self.instances:
			baseline = predicate(inst)
			sub.test(baseline, os.path.exists(inst.fpath('hnotify.log')), "%s should %ssend a host notification" % (inst.name, baseline == False and "not " or ""))
		return sub.get_status() == 0

	def test_parents(self):
		"""Test that parenting works as expected.
		This test is run after the 'active check' tests so that all
		hosts are UP when we start. We force-schedule checks to be
		run so that child hosts are checked prior to their parents.
		Test results can be verified by checking self.basepath/tier?.log
		as well as inst.home/hnotify.log.
		For now, these tests don't cover master/poller setups.
		"""
		master = self.masters.nodes[0]
		vlist = {'state': 'CRITICAL', 'output': 'Down for parent tests'}
		now = time.time()
		# Clear notification log, so we know previous tests doesn't affect the behaviour of the test
		self._clear_notification_log()
		self._test_parents(self.tap.sub_init('prep parents'), lambda x: False)
		for i in xrange(1, 4):
			fname = "%s/tier%d-host-ok" % (self.basepath, i)
			fd = os.open(fname, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0644)
			for k, v in vlist.items():
				os.write(fd, "%s=%s\n" % (k, v))
			hname = 'master.%04d' % i
			offset = 20 - (i * 5)
			master.submit_raw_command('SCHEDULE_HOST_CHECK;%s;%d' % (hname, now + offset))

		master.submit_raw_command('START_EXECUTING_HOST_CHECKS')
		master.submit_raw_command('START_EXECUTING_SVC_CHECKS')
		status = self._test_until_or_fail('parents', self._test_parents, 90, predicate = lambda x: not x.name.startswith('pg1'))
		master.submit_raw_command('STOP_EXECUTING_HOST_CHECKS')
		master.submit_raw_command('STOP_EXECUTING_SVC_CHECKS')
		self.intermission('Letting active check disabling spread', 10)
		return status == 0


	def _test_active_checks(self, sub):
		# now get nodeinfo snapshot and check to make sure
		# * All nodes have all checks accounted for
		# * All nodes agree on which node ran which check
		# * Each node runs exactly their allotted number of checks
		for inst in self.instances:
			inst.get_nodeinfo()
			hchecks = 0
			schecks = 0
			for i in inst.nodeinfo:
				hchecks += int(i.host_checks_executed)
				schecks += int(i.service_checks_executed)
				sub.test(i.assigned_hosts, i.host_checks_executed,
					 "%s expected %s to run %s host checks, but thinks %s were run" % (inst.name, i.name, i.assigned_hosts, i.host_checks_executed))
				sub.test(i.assigned_services, i.service_checks_executed,
					 "%s expected %s to run %s service checks, but thinks %s were run" % (inst.name, i.name, i.assigned_services, i.service_checks_executed))
			sub.test(hchecks, inst.group.num_objects['host'],
				"%s should have all host checks accounted for" % inst.name)
			sub.test(schecks, inst.group.num_objects['service'],
				"%s should have all service checks accounted for" % inst.name)
		return sub.get_status() == 0

	def test_active_checks(self):
		"""
		Enables active checks and verifies (via the query handler)
		that checks are properly run by the nodes supposed to take
		care of them.
		Checks can be tracked by monitoring @@BASEPATH@@/*-checks.log
		"""
		self.intermission('Letting nodes connect to each other', 20)
		master = self.masters.nodes[0]
		master.submit_raw_command('START_EXECUTING_HOST_CHECKS')
		master.submit_raw_command('START_EXECUTING_SVC_CHECKS')
		status = self._test_until_or_fail('active checks', self._test_active_checks, 60)
		master.submit_raw_command('STOP_EXECUTING_HOST_CHECKS')
		master.submit_raw_command('STOP_EXECUTING_SVC_CHECKS')
		self.intermission('Letting active check disabling spread', 10)
		return self.tap.get_status() == 0

	def _test_global_command_spread(self, sub):
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
		i = 0
		for query in queries:
			cmd = self._global_commands[i]
			i += 1
			for inst in self.instances:
				lq = 'GET status\nFilter: %s\nColumns: %s' % (query, query.split('=')[0].strip())
				live_result = inst.live.query(lq)
				ret = sub.test(len(live_result), 0,
					"%s should spread and bounce to %s" % (cmd, inst.name))
				if ret == False:
					status = False
					sub.diag("  Command failed on '%s'" % inst.name)
					sub.diag("  query was:")
					sub.diag("%s" % lq)
		return sub.get_status() == 0

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
		self._global_commands = [
			'DISABLE_FLAP_DETECTION',
			'STOP_EXECUTING_HOST_CHECKS',
			'STOP_EXECUTING_SVC_CHECKS',
			'STOP_OBSESSING_OVER_HOST_CHECKS',
			'STOP_OBSESSING_OVER_SVC_CHECKS',
			'START_ACCEPTING_PASSIVE_HOST_CHECKS',
			'START_ACCEPTING_PASSIVE_SVC_CHECKS',
			'ENABLE_NOTIFICATIONS',
		]
		status = True
		sub = self.tap.sub_init("global command submission")
		master = self.masters.nodes[0]
		for cmd in self._global_commands:
			ret = master.submit_raw_command(cmd)
			sub.test(ret, True, "Should be able to submit %s" % cmd)
		if sub.done() != 0:
			return False
		return self._test_until_or_fail('global command spreading', self._test_global_command_spread)


	def _test_notifications(self, sub, inst, **kwargs):
		"""Tests for the presence of notifications"""
		hosts = kwargs.get('hosts', False)
		services = kwargs.get('services', False)
		hpath = "%s/hnotify.log" % inst.home
		spath = "%s/snotify.log" % inst.home
		sub.test(os.access(hpath, os.R_OK), hosts,
			'%s should %ssend host notifications' % (inst.name, ('', 'not ')[hosts == False]))
		sub.test(os.access(spath, os.R_OK), services,
			'%s should %ssend service notifications' % (inst.name, ('', 'not ')[services == False]))


	def _test_passive_checks(self, sub):
		"""verifies passive check propagation
		One host per peer-group should be DOWN, the rest should be
		UNREACHABLE.
		"""
		queries = {
			'DOWN hosts': 'GET hosts\nColumns: host_name\nFilter: state = 1', # host state=1: down
			'UNREACHABLE hosts': 'GET hosts\nColumns: host_name\nFilter: state = 2', # host state=2: unreachable
			'CRITICAL services': 'GET services\nColumns: host_name service_description\nFilter: state = 2', # service state=2: critical
		}
		for inst in self.instances:
			# There are hosts master.0001 master.0002... that is checked from
			# master nodes. Those nodes have master.0001 as parent to
			# master.0002...master.000n
			#
			# There are similar hosts for poller groups, where pg1.0001 is
			# parent to pg1.0002...pg1.000n
			# Thus, as seen from master, it has DOWN hosts master.0001,
			# pg1.0001, pg2.0001, but master1.0002, pg1.0002 is UNREACHABLE
			#
			# Thus, there are 1 master.0001 host and one host per poller group
			# that is down when seen from a master, but only the poller group
			# host 0001 that is down (which is 1) when seen from the poller
			# node
			expect_down = 1
			if inst.name.startswith('master'):
				expect_down += len(self.pgroups)
			expected = {
				'DOWN hosts': expect_down,
				'UNREACHABLE hosts': inst.group.num_objects['host'] - expect_down,
				'CRITICAL services': inst.group.num_objects['service'],
			}
			for otype, query in queries.items():
				value = inst.live.query(query)
				ret = (sub.test(len(value), expected[otype], '%s should have %d %s, had %d' % (inst.name, expected[otype], otype, len(value))))
				if ret == False:
					sub.diag('got:')
					if 'host' in otype:
						for l in value:
							sub.diag('  %s' % l[0])
					else:
						for l in value:
							sub.diag('  %s;%s' % (l[0], l[1]))
		return sub.get_status() == 0


	def test_passive_checks(self):
		"""Verify that passive checks are working properly"""
		master = self.masters.nodes[0]

		# Clear notification log, so we know previous tests doesn't affect
		self._clear_notification_log()

		# services first, or the host state will block service
		# notifications
		sub = self.tap.sub_init("passive check submission")
		# must submit 3 check results per service to get notified
		for x in range(1, 4):
			for srv in self.masters.have_objects['service']:
				ret = master.submit_raw_command('PROCESS_SERVICE_CHECK_RESULT;%s;2;Service plugin output' % (srv))
				sub.test(ret, True, "Setting status of service %s" % srv)

		for host in self.masters.have_objects['host']:
			ret = master.submit_raw_command('PROCESS_HOST_CHECK_RESULT;%s;1;Plugin output for host %s' % (host, host))
			sub.test(ret, True, "Setting status of host %s" % host)
		
		# make sure all hosts are in some down state, so parents will be masked
		self.intermission("Letting down states propagate", 5)
		
		# resubmit to mask hosts, according to previously down states
		for host in self.masters.have_objects['host']:
			ret = master.submit_raw_command('PROCESS_HOST_CHECK_RESULT;%s;1;Plugin output for host %s' % (host, host))
			sub.test(ret, True, "Setting status of host %s" % host)

		# no point verifying if we couldn't even submit results
		if sub.done() != 0:
			return False

		# if passive checks don't spread, there's no point checking
		# for notifications
		if not self._test_until_or_fail("passive check distribution", self._test_passive_checks, 30):
			return sub.done() == 0

		# make sure 'master1' has sent notifications
		self.intermission("Letting notifications trigger", 5)
		sub = self.tap.sub_init('passive check notifications')
		# All masters should have at least one object, thus one notification. No pollers should notify
		for n in self.instances:
			should_have_notified = True
			# Due to create_core_config() says that pg1 doesn't notify...
			if n.name.startswith('pg1'):
				should_have_notified = False
			self._test_until_or_fail('passive check notifications for '+n.name, self._test_notifications, tap=sub, inst=n, hosts=should_have_notified, services=should_have_notified)
		return sub.done() == 0

	def _test_ack_spread(self, sub):
		for inst in self.instances:
			value = inst.live.query('GET hosts\nColumns: host_name\nFilter: acknowledged = 0')
			ret = sub.test(0, len(value), "All host acks should register on %s" % inst.name)
			if not ret:
				sub.diag("unacked hosts:")
				for l in value:
					sub.diag("  %s" % (l[0]))

			value = inst.live.query('GET services\nColumns: host_name service_description\nFilter: acknowledged = 0')
			ret = sub.test(0, len(value), 'All service acks should register on %s' % inst.name)
			if not ret:
				sub.diag("%d unacked services:" % len(value))
				for l in value:
					sub.diag("  %s;%s" % (l[0], l[1]))

			value = inst.live.query('GET comments\nStats: entry_type = 4\nStats: type = 1\nStatsAnd: 2')[0][0]
			sub.test(value, len(inst.group.have_objects['host']), "Host acks should generate one comment each on %s" % inst.name)
			value = inst.live.query('GET comments\nStats: entry_type = 4\nStats: type = 2\nStatsAnd: 2')[0][0]
			sub.test(value, len(inst.group.have_objects['service']), 'Service acks should generate one comment each on %s' % inst.name)

		return sub.get_status() == 0


	def test_acks(self):
		"""
		Adds acknowledgement commands to various nodes in the network
		and makes sure they get propagated to the nodes that need to
		know about them.
		XXX: Should also spawn notifications and check for them
		"""
		sub = self.tap.sub_init('ack submission')
		master = self.masters.nodes[0]
		for host in self.masters.have_objects['host']:
			ret = master.submit_raw_command(
				'ACKNOWLEDGE_HOST_PROBLEM;%s;0;1;0;mon testsuite;ack comment for host %s'
				% (host, host)
			)
			sub.test(ret, True, "Acking %s on %s" % (host, master.name))
		for srv in self.masters.have_objects['service']:
			(_hst, _srv) = srv.split(';')
			ret = master.submit_raw_command(
				'ACKNOWLEDGE_SVC_PROBLEM;%s;0;1;0;mon testsuite;ack comment for service %s on %s'
				% (srv, _srv, _hst)
			)
			sub.test(ret, True, "Acking %s on %s" % (srv, master.name))

		if sub.done() != 0:
			return False
		if not self._test_until_or_fail('ack distribution', self._test_ack_spread, 30):
			return False

		# ack notifications
		sub = self.tap.sub_init('ack notifications')
		for inst in self.instances:
			should_have_notified = True
			# Due to create_core_config() says that pg1 doesn't notify...
			if inst.name.startswith('pg1'):
				should_have_notified = False
			self._test_notifications(sub, inst, hosts=should_have_notified, services=should_have_notified)
		return sub.done() == 0

	def _schedule_downtime(self, sub, node, ignore={'host': {}, 'service': {}}):
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
			sub.test(ret, True, "Scheduling downtime for %s on %s" %
				(host, node.name)
			)
			ret = node.submit_raw_command(
				'SCHEDULE_HOST_DOWNTIME;%s;%d;%d;0;0;10;mon testsuite;Flexible downtime for host %s from %s' %
					(host, time.time() + 1, time.time() + 5,
					host, node.name)
			)
			sub.test(ret, True, "Adding flexible downtime for host %s from %s" % (host, node.name))


		for srv in node.group.have_objects['service']:
			if srv in ignore['service']:
				continue
			(_host_name, _service_description) = srv.split(';', 1)
			ret = node.submit_raw_command(
				'%s;%s;%d;%d;1;0;54321;mon testsuite;downtime for service %s on %s from %s' %
				('SCHEDULE_SVC_DOWNTIME', srv, time.time(),
				time.time() + 54321, _service_description, _host_name, node.name)
			)
			sub.test(ret, True, "Scheduling downtime for %s on %s" %
				(srv, node.name))
			ret = node.submit_raw_command(
				'SCHEDULE_SVC_DOWNTIME;%s;%d;%d;0;0;10;mon testsuite;Flexible downtime for service %s on %s from %s' %
					(srv, time.time() + 1, time.time() + 5,
					_service_description, _host_name, node.name)
			)
			sub.test(ret, True, "Adding flexible downtime for service %s on %s" % (srv, node.name))

		return self.tap.get_status() == 0

	def _unschedule_downtime(self, sub, node, host_ids, svc_ids):
		ret = True
		for dt in host_ids:
			ret &= node.submit_raw_command('DEL_HOST_DOWNTIME;%d' % dt);
		for dt in svc_ids:
			ret &= node.submit_raw_command('DEL_SVC_DOWNTIME;%d' % dt);
		sub.test(ret, True, "Deleting downtimes on %s" % (node.name))


	def _test_downtime_count(self, sub, inst, hosts=0, services=0):
		start_failed = sub.tcount.get('fail', 0)
		value = inst.live.query('GET hosts\nStats: scheduled_downtime_depth > 0')[0][0]
		sub.test(value, hosts, '%s: %d/%d hosts have scheduled_downtime_depth > 0' % (inst.name, value, hosts))
		value = inst.live.query('GET downtimes\nStats: downtime_type = 2')[0][0]
		sub.test(value, hosts, '%s: %d/%d host downtime entries' % (inst.name, value, hosts))
		value = inst.live.query('GET comments\nColumns: host_name\nFilter: type = 1\nFilter: entry_type = 2\nAnd: 2')
		sub.test(len(value), hosts, '%s: %d host downtime comments present, expected %d' % (inst.name, len(value), hosts))
		if len(value) != hosts:
			sub.diag("Affected hosts:\n\t" + "\n\t".join((x[0] for x in value)))

		value = inst.live.query('GET services\nColumns: host_name description scheduled_downtime_depth\nFilter: scheduled_downtime_depth > 0')
		sub.test(len(value), services, '%s: %d services have scheduled_downtime_depth > 0, expected %d' % (inst.name, len(value), services))
		if len(value) != services:
			sub.diag("Affected services:\n\t" + "\n\t".join((("%s;%s: %s" % (x,y,z)) for x,y,z in value)))
		value = inst.live.query('GET downtimes\nStats: downtime_type = 1')[0][0]
		sub.test(value, services, '%s: %d/%d service downtime entries' % (inst.name, value, services))
		value = inst.live.query('GET comments\nStats: type = 2\nStats: entry_type = 2\nStatsAnd: 2')[0][0]
		sub.test(value, services, '%s: %d/%d service downtime comments present' % (inst.name, value, services))

		# return false if any of our tests failed
		return start_failed == sub.tcount.get('fail', 0)

	def _test_dt_count(self, sub, **kwargs):
		arg_hosts = kwargs.get('hosts', None)
		arg_services = kwargs.get('services', None)
		nodes = kwargs.get('nodes', self.instances)
		for inst in nodes:
			hosts = (arg_hosts, inst.group.num_objects['host'])[arg_hosts == None]
			services = (arg_services, inst.group.num_objects['service'])[arg_services == None]
			self._test_downtime_count(sub, inst, hosts=hosts, services=services)
		return sub.get_status() == 0


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
		poller = self.pgroups[0].nodes[0]

		sub = self.tap.sub_init('expiring downtime')
		print("Submitting fixed expiring downtime to master %s" % master.name)
		start_time = int(time.time()) + 1
		end_time = start_time + 15
		duration = end_time - start_time
		for h in master.group.have_objects['host']:
			master.submit_raw_command('SCHEDULE_HOST_DOWNTIME;%s;%d;%d;1;0;%d;mon test suite;expire downtime test for %s' %
				(h, start_time, end_time, duration, h))
		for s in master.group.have_objects['service']:
			master.submit_raw_command('SCHEDULE_SVC_DOWNTIME;%s;%d;%d;1;0;%d;mon test suite;expire downtime test for %s' %
				(s, start_time, end_time, duration, s))
		self._test_until_or_fail('downtime spread', self._test_dt_count, 15, sub)
		expire_start = time.time()
		self._test_until_or_fail("downtime expiration", self._test_dt_count,
			20, sub, hosts=0, services=0
		)
		sub.done()

		sub = self.tap.sub_init('poller-scheduled downtime')
		print("Submitting downtime to poller %s" % poller.name)
		self._schedule_downtime(sub, poller)
		self._schedule_downtime(sub, master, poller.group.have_objects)

		# give all nodes some time before we check to
		# make sure the downtime has spread
		self._test_until_or_fail('poller-scheduled downtime: spread', self._test_dt_count, 45, sub)

		host_downtimes = [x[0] for x in master.live.query('GET downtimes\nColumns: id\nFilter: is_service = 0')]
		service_downtimes = [x[0] for x in master.live.query('GET downtimes\nColumns: id\nFilter: is_service = 1')]
		self._unschedule_downtime(sub, master, host_downtimes, service_downtimes)
		self._test_until_or_fail('poller-scheduled downtime: deletion', self._test_dt_count, 30, sub, hosts=0, services=0)
		sub.done()

		sub = self.tap.sub_init('propagating triggered downtime')
		# propagating triggered
		print("Submitting propagating downtime to master %s" % master.name)
		master.submit_raw_command('SCHEDULE_AND_PROPAGATE_TRIGGERED_HOST_DOWNTIME;%s;%d;%d;%d;%d;%d;%s;%s' %
			(poller.group_name + '.0001', time.time(), time.time() + 54321, 1, 0, 0, poller.group_name + '.0001', master.name))
		self._test_until_or_fail('propagating downtime: spread', self._test_dt_count, 30, sub,
				hosts=poller.group.num_objects['host'],
				services=0,
				nodes=self.masters.nodes + poller.group.nodes
		)

		parent_downtime = master.live.query('GET downtimes\nColumns: id\nFilter: host_name = %s\nFilter: is_service = 0' %
		    (poller.group_name + '.0001'))
		sub.test(len(parent_downtime), 1, "There should be exactly one parent downtime")
		self._unschedule_downtime(sub, master, parent_downtime[0], [])
		self._test_until_or_fail('propagating downtime: deletion', self._test_dt_count, 30, sub, hosts=0, services=0)
		sub.done()

		return None


	def _add_comments(self, sub, node):
		"""
		Schedules downtime for all objects on a particular node from that
		particular node.
		"""
		is_master = node.name.startswith('master')
		for host in node.group.have_objects['host']:
			if is_master and host.startswith('pg'):
				continue
			ret = node.submit_raw_command(
				'%s;%s;1;mon testsuite;comment for host %s from %s' %
					('ADD_HOST_COMMENT', host, host, node.name)
			)
			sub.test(ret, True, "Adding comment for %s from %s" %
				(host, node.name)
			)
		for srv in node.group.have_objects['service']:
			if is_master and srv.startswith('pg'):
				continue
			(_host_name, _service_description) = srv.split(';', 1)
			ret = node.submit_raw_command(
				'%s;%s;1;mon testsuite;comment for %s on %s from %s' %
				('ADD_SVC_COMMENT', srv,
				_service_description, _host_name, node.name)
			)
			sub.test(ret, True, "Adding comment for service %s on %s from %s" %
				(_service_description, _host_name, node.name))

		return sub.get_status() == 0

	def _test_comment_spread(self, sub):
		for inst in self.instances:
			value = inst.live.query('GET comments\nStats: type = 1\nStats: entry_type = 1\nStatsAnd: 2')[0][0]
			sub.test(value, len(inst.group.have_objects['host']),
				"Host comments should spread to %s" % inst.name)
			value = inst.live.query('GET comments\nStats: type = 2\nStats: entry_type = 1\nStatsAnd: 2')[0][0]
			sub.test(value, len(inst.group.have_objects['service']),
				'Service comments should spread to %s' % inst.name)
		return sub.get_status() == 0

	def test_comments(self):
		"""
		Adds comment adding commands to various nodes in the network
		and makes sure they get propagated to the nodes that need to
		know about them.
		"""
		sub = self.tap.sub_init('comments')
		master = self.masters.nodes[0]
		for pg in self.pgroups:
			self._add_comments(sub, pg.nodes[0])
		self._add_comments(sub, master)
		ret = sub.done()

		# give all nodes some time before we check to make
		# sure the ack has spread
		if ret or self._test_until_or_fail('comment spread', self._test_comment_spread, 30):
			return False
		return True

	#
	# Actual tests end here
	#
	##########################################################

	def write_progress(self, sub, start):
		"""Write progress output for sub-suite sub"""
		if not os.isatty(sys.stdout.fileno()):
			return
		now = time.time()
		ok = sub.tcount.get('ok', 0) + sub.tcount.get('fixed', 0)
		sys.stdout.write("%.2fs %d / %d passed (other: %d)\r" %
			(now - start, ok, sub.num_tests, sub.tcount.get('todo', 0))
		)
		sys.stdout.flush()

	def _test_until_or_fail(self, sub_name, func, max_time=30, tap=False, **kwargs):
		"""progressively run tests and output status
		sub_name: name of subsuite
		func: Function to use for testing subsuite
		max_time: Max runtime of subsuite
		tap: tap object to create subsuite from
		"""
		if not tap:
			tap = self.tap
		sub = tap.sub_init(sub_name)
		start = time.time()
		max_time *= self.valgrind_multiplier
		interval = 0.25 * self.valgrind_multiplier
		while True:
			last = start + max_time < time.time()
			sub.verbose = last
			# catch exceptions until we run out of time
			try:
				ret = func(sub, **kwargs)
			except Exception, e:
				if last:
					raise
				time.sleep(interval)
				continue

			self.write_progress(sub, start)
			if sub.get_status() == 0 or last:
				break
			time.sleep(interval)
			sub.reset()

		if sub.done() == 0:
			return True
		return ret


	def start_daemons(self, dname=False, stagger=True):
		first = True
		i = 0
		if dname:
			sys.stdout.write("Launching %s daemons " % (dname))
		else:
			sys.stdout.write("Launching daemons ")
		for inst in self.instances:
			i += 1
			sys.stdout.write('.')
			sys.stdout.flush()
			inst.start_daemons(self.progs, dname)
			if stagger and i < len(self.instances):
				time.sleep(0.5 * self.valgrind_multiplier)
				if first:
					first = False
					time.sleep(3 * self.valgrind_multiplier)

		sys.stdout.write("\n")
		return


	def stop_daemons(self, dname=False, deathtime=3):
		for inst in self.instances:
			inst.stop_daemons(dname)
		msgname = 'all' if dname == False else dname
		if not self.shutting_down and deathtime > 0:
			self.intermission('Letting %s daemons die' % msgname, deathtime)
			self.test_alive(daemon=dname, verbose=True, expect=False, sig_ok=15)
		# processes must get to exit nicely when we're running in valgrind
		if self.shutting_down:
			reaped = 0
			ary = (0, 0)
			now = time.time()
			while time.time() < now + 5 and reaped < (len(self.instances) * 2):
				status = 0
				try:
					ary = os.waitpid(0, os.WNOHANG)
				except OSError, e:
					print(e)
				if len(ary) == 2:
					reaped += 1
					continue
				time.sleep(0.1)

		for inst in self.instances:
			inst.slay_daemons(dname)


	def destroy_playground(self):
		time.sleep(1)
		os.system("rm -rf %s" % self.basepath)


	def destroy(self):
		"""Removes all traces of the fake mesh"""
		print("Wiping the slate clean")
		self.destroy_databases()
		self.destroy_playground()


	def shutdown(self, msg=False):
		# if we're already shutting down, we should just unwind the
		# stack frames and get going from the first one
		if self.shutting_down:
			return

		# disable alive-tests
		self.shutting_down = True

		if msg != False:
			print("%s" % msg)

		ret = self.tap.done()
		if not self.batch:
			print("Tests completed at %f (%s)" % (time.time(), time.ctime()))
			print("When done testing and examining, just press enter")
			buf = sys.stdin.readline()

		print("Stopping daemons")
		self.stop_daemons()
		self.close_db()

		sys.exit(ret)


	def _create_nodes(self):
		"""
		Creates the in-memory nodes required for this mesh. So far we
		haven't written anything to disk.
		"""
		port = 0
		self.masters = fake_peer_group(self.basepath, 'master', self.num_masters, port, valgrind = self.valgrind)
		self.masters.mesh = self
		port += self.num_masters
		self.master1 = self.masters.nodes[0]
		i = 0
		self.pgroups = []
		for p in self.opt_pgroups:
			if not p:
				continue
			i += 1
			group_name = "pg%d" % i
			fpg = fake_peer_group(self.basepath, group_name, p, port, valgrind=self.valgrind)
			fpg.mesh = self
			self.pgroups.append(fpg)
			port += p

		for inst in self.masters.nodes:
			self.instances.append(inst)
			inst.valgrind = self.valgrind

		for pgroup in self.pgroups:
			pgroup.add_master_group(self.masters)
			for inst in pgroup.nodes:
				self.instances.append(inst)
				inst.valgrind = self.valgrind

		self.groups = self.pgroups + [self.masters]

	def create_playground(self, num_hosts=False, num_services_per_host=False):
		"""
		Writes the on-disk configuration for all nodes in the mesh,
		as well as databases for them to store their options.
		"""
		# first we wipe any and all traces from earlier runs
		self.destroy()

		for pg in self.groups:
			sys.stdout.write("Creating core config for peer-group %s with %d nodes\n  " %
				(pg.group_name, len(pg.nodes)))
			for inst in pg.nodes:
				sys.stdout.write("%s " % inst.name)
				sys.stdout.flush()
				inst.add_subst('@@MODULE_PATH@@', self.merlin_mod_path)
				inst.add_subst('@@LIVESTATUS_O@@', self.livestatus_o)
				inst.create_directories()
				inst.create_core_config()
			sys.stdout.write("\n")

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


dist_test_mesh = False
def dist_test_sighandler(signo, stackframe):
	print("Caught signal %d" % signo)
	if dist_test_mesh == False:
		sys.exit(1)
	print("Killing leftover daemons")
	dist_test_mesh.shutting_down = True
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

def _verify_path(p, perms, critical=True):
	if os.access(p, perms) < 0 and critical:
		print("Unable to access %s. Did you forget to build?")
		sys.exit(1)


def cmd_dist(args):
	"""[options]
	Where options can be any of the following:
	  --basepath=<basepath>     basepath to use
	  --sleeptime=<int>         seconds to sleep before testing
	  --masters=<int>           number of masters to create
	  --pgroups=<csv>           poller-group sizes [default: 5,3,1]
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
	  --list                    list available tests
	  --test=<list,of,tests>    run only the selected tests
	  --show=<list,of,tests>    show details about the selected tests
	  --no-confgen              don't generate a new config

	Tests various aspects of event forwarding in clusters with the
	selected parameters. Config is generated but not destroyed as
	part of the process.

	To launch a cluster without running the extensive testsuite,
	use 'mon test dist --test=connections <cluster-options>' and
	issue your commands/livestatus queries once the connection
	tests have completed.

	Use '--no-confgen' to launch a pre-generated cluster if you
	need to hand-modify the configuration. The config for each
	node has to exist.
	"""
	global dist_test_mesh

	num_hosts = False
	num_services_per_host = False
	setup = True
	destroy = True
	basepath = '/tmp/merlin-dtest'
	livestatus_o = '/usr/lib64/naemon-livestatus/livestatus.so'
	db_admin_user = 'root'
	db_admin_password = False
	db_name = 'merlin'
	db_host = 'localhost'
	sql_schema_paths = []
	num_masters = 3
	opt_pgroups = [4,2,1]
	merlin_path = '/opt/monitor/op5/merlin'
	merlin_mod_path = '%s/merlin.so' % merlin_path
	merlin_binary = '%s/merlind' % merlin_path
	nagios_binary = '/opt/monitor/bin/monitor'
	no_confgen = False
	confgen_only = False
	destroy_databases = False
	sleeptime = False
	batch = True
	use_database = False
	valgrind = False
	require_pollers = True

	tests = []
	arg_tests = []
	avail_tests = []
	test_doc = {}
	all_tests = dir(fake_mesh)

	if not os.getuid():
		from pwd import getpwnam
		try:
			monuser = getpwnam('monitor')
		except KeyError:
			print "mon test dist can't be run as root, and I couldn't find\n" \
				"a monitor user to become. Exiting"
			os.exit(1)
		os.setgid(monuser.pw_gid)
		os.setuid(monuser.pw_uid)

	for m in all_tests:
		thing = getattr(fake_mesh, m)
		if type(thing) == type(fake_mesh.test_connections) and m.startswith('test_'):
			tname = m.split('_', 1)[1]
			doc = getattr(thing, '__doc__')
			if not doc:
				doc = "Test '%s' lacks documentation" % tname
				print(doc)
			test_doc[tname] = doc
			avail_tests.append(tname)
	avail_tests.sort()

	show_tests = []
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
		elif arg.startswith('--pgroups='):
			opt_pgroups = []
			for p in arg.split('=', 1)[1].split(','):
				opt_pgroups.append(int(p))
		elif arg.startswith('--merlin-binary='):
			merlin_binary = arg.split('=', 1)[1]
		elif arg.startswith('--nagios-binary=') or arg.startswith('--monitor-binary='):
			nagios_binary = arg.split('=', 1)[1]
		elif arg == '--destroy-databases':
			destroy_databases = True
		elif arg == '--confgen-only':
			confgen_only = True
			require_pollers = False
		elif arg == '--no-confgen':
			no_confgen = True
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
		elif arg == '--list':
			arg_tests.append('list')
		elif arg.startswith('--test=') or arg.startswith('--tests='):
			arg_tests += arg.split('=', 1)[1].split(',')
		elif arg.startswith('--show='):
			show_tests = arg.replace('-', '_').split('=', 1)[1].split(',')
		else:
			if arg == '--help':
				text = False
				ecode = 0
			else:
				text = 'Unknown argument: %s' % arg
				ecode = 1
			prettyprint_docstring('dist', cmd_dist.__doc__, text)
			sys.exit(ecode)

	if 'list' in arg_tests:
		print("Available tests:\n  %s" % "\n  ".join(avail_tests))
		print("\nNote that not all tests are optional")
		sys.exit(0)

	for t in show_tests:
		print("%s%s%s" % (color.yellow, t, color.reset))
		if not t in avail_tests:
			print("  %s%s%s" % (color.red, "No such test. Typo?", color.reset))
		else:
			d = test_doc.get(t)
			print("  %s\n" % d.replace("\n\t\t", "\n  ").strip())

	if len(show_tests):
		sys.exit(0)

	selected_tests = []
	deselected_tests = []
	for t in arg_tests:
		if t == 'all' or t == 'list':
			stash = avail_tests
			continue
		if t[0] == '-':
			t = t[1:]
			stash = deselected_tests
		else:
			stash = selected_tests
		if '-' in t:
			t = t.replace('-', '_')
		if test_doc.get(t):
			stash.append(t)
			if t == 'acks':
				stash.append('passive_checks')
		else:
			print("No such test: %s" % t)
			sys.exit(1)

	if not len(selected_tests):
		selected_tests = avail_tests
	for t in selected_tests:
		if t not in deselected_tests:
			tests.append(t)

	if selected_tests == ['connections']:
		require_pollers = False
	else:
		print(selected_tests)

	have_pollers = False
	for p in opt_pgroups:
		if p > 0:
			have_pollers = True
			break

	if not have_pollers and require_pollers:
		print("Can't run tests with zero pollers")
		sys.exit(1)

	if num_masters < 2 and not confgen_only:
		print("Can't run proper tests with less than two masters")
		sys.exit(1)

	if sleeptime == False:
		sleeptime = 10

	# done parsing arguments, so get real paths to merlin and nagios
	if nagios_binary[0] != '/':
		nagios_binary = os.path.abspath(nagios_binary)
	if merlin_binary[0] != '/':
		merlin_binary = os.path.abspath(merlin_binary)

	_verify_path(nagios_binary, os.X_OK)
	_verify_path(merlin_binary, os.X_OK)
	_verify_path(livestatus_o, os.R_OK)
	_verify_path(merlin_mod_path, os.R_OK)

	mesh = fake_mesh(
		basepath,
		prog_merlin=merlin_binary,
		prog_naemon=nagios_binary,
		num_masters=num_masters,
		opt_pgroups=opt_pgroups,
		merlin_mod_path=merlin_mod_path,
		livestatus_o=livestatus_o,
		db_user=db_admin_user,
		db_pass=db_admin_password,
		db_name=db_name,
		db_host=db_host,
		use_database=use_database,
		sleeptime=sleeptime,
		valgrind=valgrind,
		oconf_cache_dir=cache_dir,
		batch=batch
	)
	if not no_confgen:
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
		if mesh.test_connections() == False:
			mesh.shutdown('Connection tests failed. Bailing out')

		if 'global_commands' in tests:
			if mesh.test_global_commands() == False:
				mesh.shutdown('Global command tests failed')
		if 'passive_checks' in tests:
			if mesh.test_passive_checks() == False:
				mesh.shutdown('Passive checks are broken')
			# acks must follow immediately upon passive checks
			if 'acks' in tests:
				mesh.test_acks()

		if 'oconfsplit' in tests:
			mesh.test_oconfsplit(cache_dir + "/config")
		if 'active_checks' in tests:
			mesh.test_active_checks()
		if 'parents' in tests:
			mesh.test_parents()
		if 'downtime' in tests:
			mesh.test_downtime()
		if 'comments' in tests:
			mesh.test_comments()

		# restart tests come last, as we now have some state to read back in
		if 'merlin_restarts' in tests:
			if mesh.test_merlin_restarts() == False:
				print("Merlin restart tests failed. Bailing out")
				mesh.shutdown('Merlin restart tests failed')
		if 'nagios_restarts' in tests:
			if mesh.test_nagios_restarts() == False:
				print("Nagios restart tests failed. Bailing out")
				mesh.shutdown('Nagios restart tests failed')

	except SystemExit:
		# Some of the helper functions call sys.exit(1) to bail out.
		# Let's assume they take care of cleaning up before doing so
		raise
	except Exception, e:
		# Don't leave stuff running, just because we messed up
		print '*'*40
		print 'Exception while running tests:'
		traceback.print_exc()
		print '*'*40
		mesh.tap.fail("System exception caught: %s" % e)
		mesh.shutdown()
		raise

	mesh.shutdown()

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
	This command will disable active checks on your system and have other
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

	if f:
		for line in f:
			line = line.strip()
			if line.startswith('state='):
				stext = line.split('=', 1)[1].upper()
				state = nplug.state_code(stext)
			elif line.startswith('output='):
				output = line.split('=', 1)[1]
			elif line.startswith('perfdata='):
				perfdata = line.split('=', 1)[1]

	if not f or stext == 'unset':
		stext = path.split('-')[-1].upper()
		if stext.lower() == 'random':
			state = random.randint(0, 3)
			stext = nplug.state_name(state)
			if not output:
				output = "Randomized check"

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


def cmd_check_flap(args):
	"""state=<int>"""
	state = 0
	for arg in args:
		if arg.startswith('state='):
			state = int(arg.split('=', 1)[1])
	if not state:
		estate = 2
	else:
		estate = 0

	print("Got state=%d, so exiting with %d" % (state, estate))
	sys.exit(0)

def _test_run(command):
	proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	(out, err) = proc.communicate()
	result = proc.wait()
	return (out, err, result)

def cmd_rsync(args):
	"""
	This test takes no arguments.
	It's designed to test the rsync commands used to send ancillary
	files during a 'mon oconf push'
	"""
	tap = pytap.pytap("mon oconf push rsync command tests")
	tap.verbose = 2
	path = os.tempnam(None,'rsync')
	fd = open(path, 'w')
	fd.write("""peer thepeer {
	address = localhost
	hostgroups = oddment, tweak, nitwit
	sync {
		/etc/passwd
		/tmp/foo = /usr/bin/lalala
	}
}
	""")
	fd.close()
	cmd = ['mon', '--merlin-conf=%s' % path, 'oconf', 'push', '--dryrun']
	expect_out_extras = """rsync command: rsync -aotzc --delete -b --backup-dir=%s/backups /tmp/foo -e ssh -C -o KbdInteractiveAuthentication=no localhost:/usr/bin/lalala
rsync command: rsync -aotzc --delete -b --backup-dir=%s/backups /etc/passwd -e ssh -C -o KbdInteractiveAuthentication=no localhost:/etc/passwd
""" % (cache_dir, cache_dir)
	(out, err, result) = _test_run(cmd + ['--push=extras'])
	tap.test(err, '', "Error output should be none")
	tap.test(result, 0, "Result should be 0")
	tap.test(out, expect_out_extras, "Output should match expectations")
	(out, err, result) = _test_run(cmd + ['--push=bsm'])
	tap.test(err, '', "Error output should be none")
	tap.test(result, 0, "Result should be 0")
	tap.test(out, '', "No bsm rules should yield no push")

	if os.getuid() != 0:
		expect_result = 0
		expect_out_oconf = """rsync command: rsync -aotzc --delete -b --backup-dir=%s/backups /opt/monitor/etc -e ssh -C -o KbdInteractiveAuthentication=no localhost:/opt/monitor
""" % cache_dir
	else:
		expect_result = 1
		expect_out_oconf = """  Using 'mon oconf push' to push object config as root is prohibited
  Use 'mon oconf push --push=extras' to safely push items as root
"""
	(out, err, result) = _test_run(cmd + ['--push=oconf'])
	tap.test(err, '', "Error output should be none")
	tap.test(result, expect_result, "Result should be 1")
	tap.test(out, expect_out_oconf, "object config output should match expectations")
	sys.exit(tap.done())
