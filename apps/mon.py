#!/usr/bin/python

import os, sys, subprocess, tempfile

merlin_dir = "/opt/monitor/op5/merlin"
libexec_dir = "/usr/libexec/merlin"
pushed_logs = "/opt/monitor/pushed_logs"
archive_dir = "/opt/monitor/var/archives"

# run a generic helper from the libexec dir
def run_helper(helper, args):
	app = libexec_dir + "/" + helpers[helper]
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("Helper %s was killed by signal %d" % (app, ret))

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

def cmd_start(args):
	print("This should start the op5 Monitor system")

def cmd_stop(args):
	print("This should stop the op5 Monitor system")

# the list of commands
commands = { 'log.fetch': cmd_log_fetch, 'log.sortmerge': cmd_log_sortmerge,
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
		path = libexec_dir + '/' + rh

		# ignore entries starting with dot or dash
		if rh[0] == '.' or rh[0] == '-':
			continue

		# also ignore non-executables and directories
		if os.path.isdir(path) or not os.access(path, os.X_OK):
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
		if not raw_cmd in help_helpers:
			help_helpers.append(raw_cmd)
		continue

	(cat, cmd) = raw_cmd.split('.')
	if not cat in categories:
		categories[cat] = []
	if not cmd in categories[cat]:
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
