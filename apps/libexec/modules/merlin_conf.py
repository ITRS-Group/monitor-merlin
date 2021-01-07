import sys
import os
import io
import re
import locale
import subprocess as sp
from compound_config import *
from merlin_apps_utils import *

class merlin_node:
	valid_types = ['poller', 'master', 'peer']
	exit_code = False

	def __str__(self):
		return self.name

	def __init__(self, name, ntype = 'poller'):
		self.options = {'type': 'poller', 'port': 15551}
		self.set('name', name)
		self.set('type', ntype)
		self.set("address", name)
		self.hostgroup = []
		self.pushed_logs_dir = ''
		self.ssh_key = ''
		self.ssh_user = False
		self.port = '15551'
		self.connect = True
		self.oconf_file = False
		self.num_peers = 0
		self.peer_nodes = {}
		self.in_db = False
		self.in_config = False
		self.assignment_conflicts = {}
		self.paths_to_sync = {}
		self.sync_requires_restart = False

		# the compound object has info we will need, so
		# prepare a storage area for it
		self.comp = False

	def verify(self):
		if not self.ntype in self.valid_types:
			print("'%s' is not a valid node type" % self.ntype)
			print("Any of %s would work" % "', '".join(self.valid_types))
			return False
		if not self.name:
			print("We lack a name. How is that possible?")
			return False
		if not self.address:
			print("No address given to node '%s'" % self.name)
			return False
		if self.ntype == 'poller' and not 'hostgroup' in self.options.keys():
			print("Pollers must have hostgroups.")
			return False
		return True

	def write(self, f):
		oconf_vars = {}
		f.write("%s %s {\n" % (self.ntype, self.name))
		valid_vars = ['address', 'port', 'connect', 'uuid', 'publickey', 'auto_delete']
		if self.ntype == 'poller':
			valid_vars.append('hostgroup')
			valid_vars.append('hostgroups')
			valid_vars.append('takeover')

		for (k, v) in self.options.items():
			# we handle object_config variables first
			if k.startswith('oconf_'):
				short_var = k.split('_', 1)[1]
				oconf_vars[short_var] = v
				continue
			if not k in valid_vars:
				continue
			if type(v) == type([]):
				v = ','.join(v)
			if k == "publickey":
			    f.write("\tencrypted = 1\n")

			# we store and print oconf variables specially
			f.write("\t%s = %s\n" % (k, v))

		if len(oconf_vars):
			f.write("\n\tobject_config {\n")
			for (k, v) in oconf_vars.items():
				f.write("\t\t%s = %s\n" % (k, v))
			f.write("\t}\n")

		f.write("}\n")


	def save(self):
		if not self.verify():
			print("Refusing to save a node that doesn't verify")
			return False
		f = open(self.path, "a")
		self.write(f)
		# If we unconditionally close f, it gets a lot
		# harder to test and debug this thing.
		if f != sys.stdout:
			f.close()
		return True


	# set a variable for the object. Return True on success
	# and false on errors.
	def set_arg(self, arg, verbose=True):
		arg = arg.strip()
		if not '=' in arg:
			print("Arguments should be in the form key=value. %s doesn't work" % arg)
			return False
		(k, v) = arg.split('=', 1)
		self.set(k, v)

	def set(self, k, v):
		k = k.lower()
		if k == 'name':
			self.name = v
		elif k == 'type':
			self.ntype = v
		elif k == 'address':
			self.address = v
		elif k == 'port':
			self.port = v
		elif k == 'connect':
			self.connect = v

		if k == 'hostgroup' or k == 'hostgroups':
			k = 'hostgroup'
			v = re.split("[\t ]*,[\t ]*", v)
			if self.options.has_key(k):
				self.options[k] += v
			else:
				self.options[k] = v
		else:
			self.options[k] = v


	def rename(self, arg):
		name = self.name
		self.name = arg
		return os.rename(node_conf_dir + '/' + name, node_conf_dir + '/' + arg)


	def show(self):
		for (k, v) in self.options.items():
			k = k.upper()

			if type(v) == type([]):
				v = ','.join(v)

			print("%s=%s" % (k, v))

	
	def ctrl(self, command, out=None, err=None):
		"""
		ctrl writes stdout to the instance given in out and stderr to the
		instance given in err.
		if both out and err are either omitted, None or of instance FileIO then
		stdout and stderr will not be buffered in memory.
		"""
		assert out is None or isinstance(out, (io.RawIOBase, io.BufferedIOBase,
											io.TextIOBase))
		assert err is None or isinstance(err, (io.RawIOBase, io.BufferedIOBase,
											io.TextIOBase))
		
		# Int Python 3 stdout and stderr are instances of TextIOBase but in
		# Python 2 they're still instances of file which is a none existent
		# type in Python 3. That's why we must wrap stdout/stderr in a FileIO
		# object if no other out/err is given.
		out = out or io.FileIO(sys.stdout.fileno(), mode='w', closefd=False)
		err = err or io.FileIO(sys.stderr.fileno(), mode='w', closefd=False)
		
		col = color.yellow + color.bright
		reset = color.reset

		try:
			locale.setlocale(locale.LC_CTYPE, '')
		except locale.Error:
			locale.setlocale(locale.LC_CTYPE, 'C')
		command = command.decode(locale.getpreferredencoding())

		if not self.ssh_user:
			prefix_args = ["ssh", self.address]
		else:
			prefix_args = ["ssh", self.ssh_user + '@' + self.address]
		prefix_args += ['-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes']
		if self.ssh_key:
			prefix_args += ['-i', self.ssh_key]
		cmd = prefix_args + [command]
		out.write(u'Connecting to \'%s\' with the following command:\n  %s\n'
			  % (self.name, ' '.join(cmd)))
		out.write(u'%s#--- REMOTE OUTPUT START ---%s\n' % (col, reset))

		# Ideally we would have liked to pass out and err directly to a
		# subprocess call in all cases, but unfortunately not all objects that
		# inherit the IO baseclasses implement all the necessary methods which
		# makes subprocess call fail. We added the FileIO expection so this
		# method can be called with out/err being written directly to file and
		# not being buffered in memory.
		if isinstance(out, io.FileIO) and isinstance(err, io.FileIO):
			ret = sp.call(cmd, stdout=out, stderr=err)
		else:
			p = sp.Popen(cmd, stdout=sp.PIPE, stderr=sp.PIPE)
			output, error = p.communicate()
			output and out.write(u'%s\n' % output)
			error and err.write(u'%s\n' % error)
			ret = p.returncode

		out.write(u'%s#--- REMOTE OUTPUT DONE ----%s\n' % (col, reset))
		self.exit_code = ret

		if ret < 0:
			out.write(u'ssh was killed by signal %d\n' % ret)
			return False
		if ret != 0:
			out.write(u'ssh exited with return code %d\n' % ret)
			return False
		return True

	def get_exit_code(self):
		return self.exit_code

config_file = '/opt/monitor/op5/merlin/merlin.conf'
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
configured_nodes = {}
sorted_nodes = []
dbopt = {}
daemon = {}
module = {}
_node_defaults = {}

def node_cmp(a, b):
	if a.ntype == 'master' and b.ntype != 'master':
		return -1
	if b.ntype == 'master' and a.ntype != 'master':
		return 1
	if b.ntype == 'poller' and a.ntype != 'poller':
		return -1
	if a.ntype == 'poller' and b.ntype != 'poller':
		return 1
	return cmp(a.name, b.name)

def parse():
	try:
		conf = parse_conf(config_file)
	except Exception, e:
		print("Failed to parse %s: %s" % (config_file, e.strerror))
		sys.exit(1)

	for comp in conf.objects:
		comp.name = comp.name.strip()
		# grab the database settings. fugly, but quick
		if comp.name == 'daemon':
			for k, v in comp.params:
				daemon[k] = v

			for dobj in comp.objects:
				dobj.name.strip()
				if dobj.name == 'database':
					for dk, dv in dobj.params:
						dbopt[dk] = dv
			continue

		if comp.name == 'module':
			for k, v in comp.params:
				module[k] = v
			continue

		ary = re.split("[\t ]+", comp.name, 1)
		if len(ary) != 2 or not ary[0] in merlin_node.valid_types:
			continue
		(ntype, name) = ary
		if ntype == 'slave':
			ntype = 'poller'
		elif ntype == 'noc':
			ntype = 'master'

		node = merlin_node(name, ntype)
		node.in_config = True
		node.comp = comp
		node.path = config_file
		num_nodes[ntype] += 1
		configured_nodes[node.name] = node
		sorted_nodes.append(node)
		for (k, v) in comp.params:
			node.set(k, v)
		for sc in comp.objects:
			if sc.name == 'object_config':
				for (sk, sv) in sc.params:
					node.set('oconf_' + sk, sv)
			elif sc.name == 'sync':
				for sk, sv in sc.params:
					if sk == 'restart':
						node.sync_requires_restart = strtobool(sv)
						continue
					node.paths_to_sync[sk] = sv

	if len(sorted_nodes):
		sorted_nodes.sort(node_cmp)

	# check and store how many peers each node has.
	i = 0
	for node in configured_nodes.values():
		i += 1
		# all masters should be peers to each other
		if node.ntype == 'master':
			node.num_peers = num_nodes['master'] - 1
			continue

		# Since we're not configured ourselves, we skip
		# subtracting one from each num_nodes['peer']
		# for all peers
		if node.ntype == 'peer':
			node.num_peers = num_nodes['peer']
			continue

		# poller. check how many nodes shares this poller's
		# hostgroup configuration.
		for n2 in configured_nodes.values()[i:]:
			# the node is not a peer to itself, and it's not a
			# peer to nodes of different kinds.
			if n2.name == node.name or n2.ntype != node.ntype:
				continue
			hg1 = set(node.options.get('hostgroup', [0]))
			hg2 = set(n2.options.get('hostgroup', [1]))
			# if hostgroup sets match, they're peers
			if hg1 == hg2:
				node.num_peers += 1
				n2.num_peers += 1
				node.peer_nodes[n2.name] = n2
				n2.peer_nodes[node.name] = node
			elif hg1 & hg2:
				# One poller handles a subset of hostgroups that
				# another hostgroup also handles. Very bad indeed.
				node.assignment_conflicts[n2.name] = hg1 & hg2
				n2.assignment_conflicts[node.name] = hg2 & hg1
				#print("Hostgroup assignment conflict for %s and %s" %
				#	(node.name, n2.name))
				#print("  They share the following, but not all, hostgroups: %s" %
				#	', '.join(hg1 & hg2))
