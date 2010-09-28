#!/usr/bin/python

import os, sys, subprocess, tempfile

merlin_dir = "/opt/monitor/op5/merlin"
libexec_dir = "/usr/libexec/merlin"
node_conf_dir = "/etc/op5/distributed/nodes"
pushed_logs = "/opt/monitor/pushed_logs"
archive_dir = "/opt/monitor/var/archives"

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


# run a generic helper from the libexec dir
def run_helper(helper, args):
	app = libexec_dir + "/" + helpers[helper]
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("Helper %s was killed by signal %d" % helper % ret)

## log commands ##
# force running push_logs on poller and peer systems
def cmd_log_fetch(args):
	since = ''
	for arg in args:
		if arg.startswith('--incremental='):
			since = '--since=' + arg[14:]

	for node in configured_nodes.values():
		if node.ntype == 'master':
			continue
		ctrl = "mon log push"
		if since:
			ctrl += ' ' + since
		if not node.ctrl(ctrl):
			print("Failed to force %s to push its logs. Exiting" % node.name)
			sys.exit(1)

def cmd_log_sortmerge(args):
	since = False
	for arg in args:
		if (arg.startswith('--since=')):
			since = arg.split('=', 1)[1]

	if since:
		since = '--incremental=' + since

	pushed = {}
	for (name, node) in configured_nodes.items():
		if node.ntype == 'master':
			continue
		if not os.access(pushed_logs + '/' + node.name, os.X_OK):
			print("Failed to access() pushed_logs dir for %s" % node.name)
			return False

		pushed[name] = os.listdir(pushed_logs + '/' + node.name)
		if len(pushed[name]) == 0:
			print("%s hasn't pushed any logs yet" % name)
			return False

		if 'in-transit.log' in pushed[name]:
			print("Log files still in transit for node '%s'" % node.name)
			return False

	if len(pushed) != num_nodes['peer'] + num_nodes['poller']:
		print("Some nodes haven't pushed their logs. Aborting")
		return False

	last_files = False
	for (name, files) in pushed.items():
		if last_files and not last_files == files:
			print("Some nodes appear to not have pushed the files they should have done")
			return False
		last_files = files

	app = merlin_dir + "/import"
	cmd_args = [app, '--list-files', args, archive_dir]
	stuff = subprocess.Popen(cmd_args, stdout=subprocess.PIPE)
	output = stuff.communicate()[0]
	sort_args = ['sort']
	sort_args += output.strip().split('\n')
	for (name, more_files) in pushed.items():
		for fname in more_files:
			sort_args.append(pushed_logs + '/' + name + '/' + fname)

	print("sort-merging %d files. This could take a while" % (len(sort_args) - 1))
	(fileno, tmpname) = tempfile.mkstemp()
	subprocess.check_call(sort_args, stdout=fileno)
	print("Logs sorted into temporary file %s" % tmpname)
	return tmpname


# run the import program
def cmd_log_import(args):
	since = ''
	fetch = False
	i = 0
	for arg in args:
		if arg.startswith('--incremental='):
			since = arg[14:]
		elif arg == '--truncate-db':
			since = '1'
		elif arg == '--fetch':
			fetch = True
			args.pop(i)
		i += 1

	if not '--list-files' in args:
		if num_nodes['poller'] or num_nodes['peer']:
			if fetch == True:
				cmd_log_fetch(since)
			tmpname = cmd_log_sortmerge(['--since=' + since])
			print("importing from %s" % tmpname)
			import_args = [merlin_dir + '/import', tmpname] + args
			subprocess.check_call(import_args, stdout=sys.stdout.fileno())
			return True

	app = merlin_dir + "/import"
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("The import program was killed by signal %d" % ret)
	return ret


# run the showlog program
def cmd_log_show(args):
	app = merlin_dir + "/showlog"
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("The showlog helper was killed by signal %d" % ret)
	return ret


## node commands ##
# list configured nodes, capable of filtering by type
def cmd_node_list(args):
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
def cmd_node_show(args):
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


def cmd_node_add(args):
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


def cmd_node_edit(args):
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


def cmd_node_remove(args):
	if len(args) == 0:
		print("Which node do you want to remove? Try the 'list' command")
		return

	for arg in args:
		if not arg in configured_nodes:
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			continue

		configured_nodes.pop(arg)
		os.unlink(node_conf_dir + '/' + arg)


def cmd_node_rename(args):
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


def cmd_start(args):
	print("This should start the op5 Monitor system")

def cmd_stop(args):
	print("This should stop the op5 Monitor system")

# load the configured_nodes we'll be using
configured_nodes = {}
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
if os.access(node_conf_dir, os.X_OK):
	for f in os.listdir(node_conf_dir):
		node = merlin_node(f)
		num_nodes[node.ntype] += 1
		configured_nodes[node.name] = node

# the list of commands
commands = { 'node.remove': cmd_node_remove, 'node.add': cmd_node_add,
	'node.list': cmd_node_list, 'node.show': cmd_node_show,
	'node.edit': cmd_node_edit, 'node.rename': cmd_node_rename,
	'log.fetch': cmd_log_fetch, 'log.sortmerge': cmd_log_sortmerge,
	'log.import': cmd_log_import, 'log.show': cmd_log_show}
categories = {}
help_helpers = []
helpers = {}

def load_command_module(path):
	global commands
	ret = False

	if not libexec_dir in sys.path:
		sys.path.append(libexec_dir)

	modname = os.path.basename(path)[:-3]
	module = __import__(os.path.basename(path)[:-3])

	if getattr(module, "pure_script", False):
		return False
	init_func = getattr(module, "module_init", False)
	if init_func != False:
		if init_func() == False:
			return False

	for f in dir(module):
		if not f.startswith('cmd_'):
			continue
		ret = True
		func = getattr(module, f)
		callname = modname + '.' + func.__name__[4:]
		commands[callname] = func

	return ret

if os.access(libexec_dir, os.X_OK):
	raw_helpers = os.listdir(libexec_dir)
	for rh in raw_helpers:
		# ignore non-executables
		if not os.access(libexec_dir + "/" + rh, os.X_OK):
			continue

		# remove script suffixes
		ary = rh.split('.')
		if len(ary) > 1 and ary[-1] in ['sh', 'php', 'pl', 'py', 'pyc']:
			if ary[-1] == 'pyc':
				continue
			if len(ary) == 2 and ary[-1] == 'py':
				if load_command_module(libexec_dir + '/' + rh):
					continue

			helper = '.'.join(ary[:-1])
		else:
			helper = '.'.join(ary)

		cat_cmd = helper.split('.', 1)
		if len(cat_cmd) == 2:
			(cat, cmd) = cat_cmd
			helper = cat + '.' + cmd
			if not cat in categories:
				categories[cat] = []
			if cmd in categories[cat]:
				print("Can't override builtin category+command with helper %s" % helper)
				continue
			categories[cat].append(cmd)
		else:
			help_helpers.append(helper)

		if helper in commands:
			print("Can't override builtin command with helper '%s'" % helper)
			continue

		commands[helper] = run_helper
		helpers[helper] = rh

# we break things down to categories and subcommands
for raw_cmd in commands:
	if not '.' in raw_cmd:
		help_helpers.append(raw_cmd)
		continue

	(cat, cmd) = raw_cmd.split('.')
	if not cat in categories:
		categories[cat] = []
	categories[cat].append(cmd)

def show_usage():
	print('''usage: mon [category] <command> [options]

Where category is optional and their respective commands are as follows:\n''')
	cat_keys = categories.keys()
	cat_keys.sort()
	for cat in cat_keys:
		print("%9s: %s" % (cat, ', '.join(categories[cat])))
	if len(help_helpers):
		print("%9s: %s" % ("", ', '.join(help_helpers)))

	print('''\nand options simply depend on what command+command you choose.
Some commands accept a --help flag to print some helptext.''')
	print("""\nExamples:
Show monitor's logfiles, with filtering and colors etc
	mon log show [options]

Import logfiles to populate the database for reports
	mon log import [options]

List all configured nodes for distributed systems
	mon node list

Show all pollers configured for distributed monitoring
	mon node show --type=poller

Remove a configured node
	mon node remove <nodename>""")
	sys.exit(1)

if len(sys.argv) < 2 or sys.argv[1] == '--help' or sys.argv == 'help':
	show_usage()

cmd = cat = sys.argv[1]
if cat in commands:
	args = sys.argv[2:]
elif len(sys.argv) > 2:
	cmd = cat + '.' + sys.argv[2]
	args = sys.argv[3:]
if not cmd in commands:
	print("Bad category/command: %s" % cmd.replace('.', ' '))
	sys.exit(1)

if commands[cmd] == run_helper:
	run_helper(cmd, args)
else:
	commands[cmd](args)
