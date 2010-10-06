import os, sys, re

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
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

node_conf_dir = "/etc/op5/distributed/nodes"
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
configured_nodes = {}
merlin_conf = '/opt/monitor/op5/merlin/merlin.conf'
wanted_types = merlin_node.valid_types
wanted_names = []
ntype = 'poller'

def module_init():
	global merlin_conf, wanted_types, wanted_names, configured_nodes

	i = 2
	for arg in sys.argv[i:]:
		if arg.startswith('--merlin-cfg=') or arg.startswith('--config='):
			merlin_conf = arg.split('=', 1)[1]
		elif arg.startswith('--type='):
			wanted_types = arg.split('=')[1].split(',')
		elif arg.startswith('--name='):
			wanted_names = arg.split('=')[1].split(',')
		else:
			# not an argument we recognize, so ignore it
			i += 1
			continue

		popped = sys.argv.pop(i)
		i += 1

	# load the configured nodes
	conf = parse_conf(merlin_conf)
	for comp in conf.objects:
		comp.name = comp.name.strip()
		ary = re.split("[\t ]+", comp.name, 1)
		if len(ary) != 2 or not ary[0] in merlin_node.valid_types:
			continue
		node = merlin_node(ary[1], ary[0])
		node.comp = comp
		node.path = merlin_conf
		configured_nodes[node.name] = node
		for (k, v) in comp.params:
			node.set(k, v)
		for sc in comp.objects:
			if sc.name != 'object_config':
				continue
			for (sk, sv) in sc.params:
				node.set('oconf_' + sk, sv)

## node commands ##
# list configured nodes, capable of filtering by type
def cmd_list(args):
	global wanted_types
	for node in configured_nodes.values():
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


# display all variables for all nodes, or for one node in a fashion
# suitable for being used as "eval $(mon node show nodename)" from
# shell scripts
def cmd_show(args):
	if len(configured_nodes) == 0:
		print("No nodes configured")
		return
	if len(args) == 0:
		for (name, node) in configured_nodes.items():
			if not node.ntype in wanted_types:
				continue
			print("\n# %s" % name)
			node.show()

	for arg in args:
		if not arg in configured_nodes.keys():
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			# scripts will list one node at a time. If the command
			# fails, they don't have to check the output to see if
			# the got anything sensible or not
			if len(args) == 1:
				sys.exit(1)
			continue
		if not configured_nodes[arg].ntype in wanted_types:
			continue
		if len(args) > 1:
			print("\n# %s" % arg)
		configured_nodes[arg].show()


def cmd_add(args):
	if len(args) < 1:
		return False
	name = args[0]
	if name in configured_nodes.keys():
		print("%s is already configured. Aborting" % name)
		return False

	node = merlin_node(name)
	node.path = merlin_conf

	for arg in args[1:]:
		if not '=' in arg:
			continue
		ary = arg.split('=', 1)
		node.set(ary[0], ary[1])

	if node.save():
		print("Successfully added %s node '%s'" % (node.ntype, node.name))
		return True

	print("Failed to add %s node '%s'" % (node.ntype, node.name))
	return False


def _cmd_edit(args):
	if len(args) < 2:
		print("No key=value pairs to modify")
		return False

	name = args[0]
	if not name in configured_nodes.keys():
		print("%s isn't configured yet. Use 'add' to add it" % name)
		return False

	node = merlin_node(args[0])
	for arg in args[1:]:
		node.set(arg)

	return node.save()


def cmd_remove(args):
	if len(args) == 0:
		print("Which node do you want to remove? Try the 'list' command")
		return

	for arg in args:
		if not arg in configured_nodes:
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			continue

		node = configured_nodes.pop(arg)
		sed_range = str(node.comp.line_start) + ',' + str(node.comp.line_end)
		cmd_args = ['sed', '-i', sed_range + 'd', merlin_conf]
		os.spawnvp(os.P_WAIT, 'sed', cmd_args)


def _cmd_rename(args):
	if len(args) != 2 or not args[0] in configured_nodes.keys():
		print("Which node do you want to rename? Try the 'list' command")
		return False

	node = configured_nodes[args[0]]
	dest = args[1]
	if dest in configured_nodes.keys():
		print("A node named '%s' already exists. Remove it first" % dest)
		return False
	configured_nodes.pop(args[0])
	configured_nodes[dest] = node
	node.rename(dest)
