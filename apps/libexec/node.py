import os, sys

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from compound_config import *

node_conf_dir = "/etc/op5/distributed/nodes"
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
configured_nodes = {}
merlin_conf = '/opt/monitor/op5/merlin/merlin.conf'

def module_init():
	global merlin_conf
	# load the configured_nodes we'll be using
	if os.access(node_conf_dir, os.X_OK):
		for f in os.listdir(node_conf_dir):
			node = merlin_node(f)
			num_nodes[node.ntype] += 1
			configured_nodes[node.name] = node

	for arg in sys.argv:
		if arg.startswith('--merlin-cfg=') or arg.startswith('--config='):
			merlin_conf = arg.split('=', 1)[1]

class merlin_node:
	valid_types = ['poller', 'master', 'peer']
	ntype = 'poller'
	name = False
	address = ''
	pushed_logs_dir = ''
	ssh_key = ''
	ssh_user = 'monitor'
	options = {'ntype': 'poller', 'ssh_user': 'monitor'}

	def __init__(self, name):
		self.name = name
		if os.access(node_conf_dir + '/' + name, os.R_OK):
			self.load()

		if not self.address:
			self.set("address=" + name, False)


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
		f.write("TYPE=%s\n" % self.ntype)
		f.write("ADDRESS=%s\n" % self.address)
		if self.ssh_key:
			f.write("SSH_KEY=%s\n" % self.ssh_key)


	def save(self):
		if not self.verify():
			print("Refusing to save a node that doesn't verify")
			return False
		f = open(node_conf_dir + '/' + self.name, "w")
		self.write(f)
		f.close()
		return True


	def load(self):
		f = open(node_conf_dir + '/' + self.name)
		for line in f:
			line = line.strip()
			# ignore comments and empty lines
			if line[0] == '#' or len(line) == 0:
				continue
			# warn about bogus lines
			if not '=' in line:
				print("'%s' has a malformed configuration")
				continue
			self.set(line, False)


	# set a variable for the object. Return True on success
	# and false on errors.
	def set(self, arg, verbose=True):
		arg = arg.strip()
		if not '=' in arg:
			print("Arguments should be in the form key=value. %s doesn't work" % arg)
			return False
		(k, v) = arg.split('=', 1)
		k = k.lower()
		self.options[k] = v
		if k == 'type':
			self.ntype = v
		elif k == 'address':
			self.address = v
		elif k == 'pushed_logs_dir':
			self.pushed_logs_dir = v
		elif k == 'ssh_key' or k == 'key':
			if len(v) and not os.access(v, os.R_OK):
				print("It seems I can't access the key '%s'. Misconfigured?" % v)
			self.ssh_key = v
		elif k == 'ssh_user' or k == 'user':
			self.ssh_user = v
		else:
			print("Unknown key in key=value pair: %s=%s" % (k, v))
			return False
		if verbose:
			print("Setting '%s=%s' for node '%s'" % (k, v, self.name))


	def rename(self, arg):
		name = self.name
		self.name = arg
		return os.rename(node_conf_dir + '/' + name, node_conf_dir + '/' + arg)


	def show(self):
		self.write(sys.stdout)


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

## node commands ##
# list configured nodes, capable of filtering by type
def cmd_list(args):
	wanted_types = merlin_node.valid_types
	for arg in args:
		if arg.startswith('--type='):
			wanted_types = arg.split('=')[1].split(',')

	for node in configured_nodes.values():
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


# display all variables for all nodes, or for one node in a fashion
# suitable for being used as "eval $(mon node show nodename)" from
# shell scripts
def cmd_show(args):
	wanted_types = merlin_node.valid_types
	i = 0
	for arg in args:
		if arg.startswith('--type='):
			wanted_types = arg.split('=')[1].split(',')
			args.pop(i)
		i += 1

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
		print("%s is already configured. Use 'edit' command to alter it" % name)
		return False

	node = merlin_node(name)

	if len(args) == 1:
		node.address = node.name

	for arg in args[1:]:
		node.set(arg, False)
	
	if node.save():
		print("Successfully added %s node '%s'" % (node.ntype, node.name))
		return True

	print("Failed to add %s node '%s'" % (node.ntype, node.name))
	return False


def cmd_edit(args):
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

		configured_nodes.pop(arg)
		os.unlink(node_conf_dir + '/' + arg)


def cmd_rename(args):
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
