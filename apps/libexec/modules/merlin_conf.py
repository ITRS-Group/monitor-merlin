import re
from compound_config import *

class merlin_node:
	valid_types = ['poller', 'master', 'peer']

	def __init__(self, name, ntype = 'poller'):
		self.options = {'type': 'poller', 'port': 15551}
		self.set('name', name)
		self.set('type', ntype)
		self.set("address", name)
		self.hostgroup = []
		self.pushed_logs_dir = ''
		self.ssh_key = ''
		self.ssh_user = 'monitor'
		self.port = '15551'

		# the compound object has info we will need, so
		# prepare a storage area for it
		self.comp = False

	def verify(self):
		if not self.ntype in self.valid_types:
			print("'%s' is not a valid node type" % self.ntype)
			print("Any of %s would work" % "', '".join(self.valid_types))
			return False
		if not self.name:
			print("We lack a name. How the fuck is that possible?")
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
		valid_vars = ['address', 'port']
		if self.ntype != 'master':
			valid_vars.append('hostgroup')

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
		self.write(sys.stdout)
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
		elif k == 'hostgroup':
			v = re.split("[\t ]*,[\t ]", v)
		elif not k.startswith('oconf_'):
			print("Unknown key in key=value pair: %s=%s" % (k, v))
			raise hell
			return False

		if k == 'hostgroup' and self.options.has_key(k):
			print(self.name, k, self.options[k], v)
			self.options[k] += v
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


	def ctrl(self, args):
		if not self.ssh_user:
			self.ssh_user = 'root'
		prefix_args = ["ssh", self.ssh_user + "@" + self.address]
		if self.ssh_key:
			prefix_args += ['-i', self.ssh_key]
		all_args = prefix_args + [args]
		print("Connecting to '%s' with the following command:\n  %s"
			  % (self.name, ' '.join(all_args)))
		ret = os.spawnvp(os.P_WAIT, "ssh", all_args)
		if ret < 0:
			print("ssh was killed by signal %d" % ret)
			return False
		if ret != 0:
			print("ssh exited with return code %d" % ret)
			return False
		return True

config_file = '/opt/monitor/op5/merlin/merlin.conf'
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
configured_nodes = {}
dbopt = {}
_node_defaults = {}

def parse():
	conf = parse_conf(config_file)

	for comp in conf.objects:
		comp.name = comp.name.strip()
		# grab the database settings. fugly, but quick
		if comp.name == 'daemon':
			for dobj in comp.objects:
				dobj.name.strip()
				if dobj.name != 'database':
					continue
				for k, v in dobj.params:
					dbopt[k] = v
			continue

		ary = re.split("[\t ]+", comp.name, 1)
		if len(ary) != 2 or not ary[0] in merlin_node.valid_types:
			continue
		node = merlin_node(ary[1], ary[0])
		node.comp = comp
		node.path = config_file
		num_nodes[ntype] += 1
		configured_nodes[node.name] = node
		for (k, v) in comp.params:
			node.set(k, v)
		for sc in comp.objects:
			if sc.name != 'object_config':
				continue
			for (sk, sv) in sc.params:
				node.set('oconf_' + sk, sv)
