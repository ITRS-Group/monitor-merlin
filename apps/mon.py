#!/usr/bin/python -tt

import os, sys, subprocess, tempfile

merlin_dir = "/opt/monitor/op5/merlin"
libexec_dir = "/usr/libexec/merlin"

module_dir = libexec_dir + '/modules'
if not libexec_dir in sys.path:
	sys.path.insert(0, libexec_dir)
import node
from merlin_apps_utils import *

if not module_dir in sys.path:
	sys.path.append(module_dir)
import merlin_conf as mconf

mconf.config_file = '%s/merlin.conf' % merlin_dir

# run a generic helper from the libexec dir
def run_helper(helper, args):
	app = libexec_dir + "/" + helpers[helper]
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("Helper %s was killed by signal %d" % (app, ret))

def cmd_start(args):
	print("This should start the op5 Monitor system")

def cmd_stop(args):
	print("This should stop the op5 Monitor system")

# the list of commands
commands = {}
categories = {}
help_helpers = []
helpers = {}
init_funcs = {}
command_mod_load_fail = {}
docstrings = {}


def load_command_module(path):
	global commands, init_funcs, mconf, docstrings
	global command_mod_load_fail
	ret = False

	if not libexec_dir in sys.path:
		sys.path.append(libexec_dir)

	modname = os.path.basename(path)[:-3]
	try:
		module = __import__(modname)
	except BaseException, ex:
		# catch-all, primarily for development purposes
		command_mod_load_fail[modname] = ex
		return -1

	if getattr(module, "pure_script", False):
		return False

	# this is a command module, so pass on some sensible defaults
	module.mconf = mconf
	module.merlin_dir = merlin_dir
	module.module_dir = module_dir

	docstrings[modname] = {'__doc__': module.__doc__}

	# we grab the init function here, but delay running it
	# until we know which module we'll be using.
	init_func = getattr(module, "module_init", False)

	for f in dir(module):
		if not f.startswith('cmd_'):
			continue
		ret = True
		func = getattr(module, f)
		funcname = func.__name__[4:].replace('_', '-')
		callname = modname + '.' + funcname
		if func.__doc__:
			docstrings[modname][funcname] = func.__doc__
		commands[callname] = func
		init_funcs[callname] = init_func

	return ret

def load_all_commands():
	global commands, categories, helpers, help_helpers

	if os.access(libexec_dir, os.X_OK):
		raw_helpers = os.listdir(libexec_dir)
		for rh in raw_helpers:
			path = libexec_dir + '/' + rh

			# ignore entries starting with dot or dash
			if rh[0] == '.' or rh[0] == '-':
				continue

			# ignore directories
			if os.path.isdir(path):
				continue

			# ignore non-executables, unless they're python scriptlets
			if not os.access(path, os.X_OK) and not rh.endswith('.py'):
				continue

			# remove script suffixes
			ary = rh.split('.')
			if len(ary) > 1 and ary[-1] in ['sh', 'php', 'pl', 'py', 'pyc']:
				if ary[-1] == 'pyc':
					continue
				if len(ary) == 2 and ary[-1] == 'py':
					if load_command_module(libexec_dir + '/' + rh) == True:
						continue
					# if we failed to load due to syntax error, don't
					# bother presenting it as a standalone helper
					if command_mod_load_fail.get(rh, False) != -1:
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



def mod_fail_print(text, mod):
	msg = command_mod_load_fail.get(mod)
	print("%s %sFailed to load command module %s%s%s%s:\n   %s%s%s%s\n" %
		(text, color.red, color.yellow, color.bright, mod, color.reset,
		color.red, color.bright, msg, color.reset))

def show_usage():
	print('''usage: mon [category] <command> [options]

Where category is sometimes optional.\n''')
	topic = 'Available categorized commands:'
	print("%s\n%s" % (topic, '-' * len(topic)))
	cat_keys = categories.keys()
	cat_keys.sort()
	for cat in cat_keys:
		print("  %-7s: %s" % (cat, ', '.join(categories[cat])))
	if len(help_helpers):
		topic = "Commands without category:"
		print("\n%s\n%s\n   %s" % (topic, '-' * len(topic), ', '.join(help_helpers)))

	print('''\nOptions naturally depend on which command you choose.

Some commands accept a --help flag to print some helptext, and some
categories have a help-command associated with them.
''')

	for mod in command_mod_load_fail.keys():
		mod_fail_print("(nonfatal)", mod)
	sys.exit(1)

# now we load all the commands so we can actually do things
load_all_commands()

if len(sys.argv) < 2 or sys.argv[1] == '--help' or sys.argv[1] == 'help':
	show_usage()

args = []
autohelp = False
cmd = cat = sys.argv[1]
if cat in commands:
	args = sys.argv[2:]
elif len(sys.argv) > 2:
	if sys.argv[2] == '--help' or sys.argv[2] == 'help':
		cmd = cat + '.help'
		autohelp = True
	else:
		cmd = cat + '.' + sys.argv[2]
	args = sys.argv[3:]
else:
	# only one argument passed, and it's not a stand-alone command.
	# Take it to mean 'help' for that category
	cmd = cat + '.help'
	autohelp = True

i = 0
rem_args = []
for arg in args:
	i += 1
	if arg.startswith('--merlin-cfg='):
		mconf.config_file = arg.split('=', 1)[1]
	elif arg.startswith('--nagios-cfg='):
		nagios_cfg = arg.split('=', 1)[1]
	elif arg.startswith('--object-cache='):
		object_cache = arg.split('=', 1)[1]
	else:
		rem_args.append(arg)
		continue

# now we parse the merlin config file and stash remaining args
mconf.parse()
args = rem_args

# if a dev's managed to hack in a bug in a command module, we
# should tell the user so politely and not just fail to do anything
if cat in command_mod_load_fail.keys():
	mod_fail_print('%sfatal%s:' % (color.yellow, color.reset), cat)
	sys.exit(1)

if not cmd in commands:
	print("")
	if not cat in categories.keys():
		print("No category '%s' available, and it's not a raw command\n" % cat)
	elif autohelp == True:
		if not docstrings.get(cat):
			print("Category '%s' has no help overview." % cat)
		if docstrings.get(cat, {}).get('__doc__'):
			print(docstrings[cat]['__doc__'])
		cat_keys = categories[cat]
		cat_keys.sort()
		print("Available commands in category %s%s%s%s:" %
			(color.blue, color.bright, cat, color.reset))
		for cmd in cat_keys:
			doc_string = docstrings.get(cat, {}).get(cmd, '\n(documentation missing)')
			prettyprint_docstring(cmd, doc_string)
	else:
		print("Bad category/command: %s" % cmd.replace('.', ' '))
	sys.exit(1)

if commands[cmd] == run_helper:
	run_helper(cmd, args)
else:
	# now we run the command module's possibly costly
	# intialization routines
	init = init_funcs.get(cmd, False)
	if init != False:
		rem_args = init(args)
		if type(rem_args) == type([]):
			args = rem_args

	commands[cmd](args)
