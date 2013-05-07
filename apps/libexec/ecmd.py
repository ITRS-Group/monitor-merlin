import os, sys, re, time, errno
modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from nagios_command import *
from compound_config import parse_nagios_cfg


def search(arg):
	arg = arg.upper()
	for cname, ci in nagios_command.command_info.items():
		if ci['description'] == 'This command is not implemented':
			continue

		if re.match(arg, cname) or cname.find(arg) != -1:
			template = ci['template'].split(';')
			if len(template) > 1:
				separator = '%s%s;%s' % (color.blue, color.bright, color.reset)
				t_str = separator.join(template[1:])
			else:
				t_str = 'This command takes no parameters'
			print("%s%s%s\n  %s" % (color.green, cname, color.reset, t_str))


def cmd_search(args):
	"""<regex>
	Prints 'templates' for all available commands matching <regex>. The
	search is case insensitive.
	"""
	for arg in args:
		search(arg)


def cmd_submit(args):
	"""[options] command <parameters>
	Submits a command to the monitoring engine using the supplied values.
	Available options:
	  --pipe-path=</path/to/nagios.cmd>

	An example command to add a new service comment for the service PING
	on the host foo would look something like this:

	  mon ecmd submit add_svc_comment service='foo;PING' persistent=1 \\
	         author='John Doe' comment='the comment'

	Note how services are written. You can also use positional arguments,
	in which case the arguments have to be in the correct order for the
	command's syntactic template. The above example would then look thus:

	  mon ecmd submit add_svc_comment 'foo;PING' 1 'John Doe' 'the comment'
	"""

	cname = False
	nconf = parse_nagios_cfg(nagios_cfg)
	pipe_path = nconf.command_file
	i = 0
	params = []
	cmd = False
	for arg in args:
		i += 1
		if arg.startswith('--pipe-path='):
			pipe_path = arg.split('=', 1)[1]
			continue
		if not arg.startswith('--'):
			if not cmd:
				cmd = nagios_command(arg)
				if not cmd.info:
					print("Failed to find command '%s'" % arg)
					search(args[0])
					sys.exit(1)
			else:
				params.append(arg)

	if not cmd or not cmd.info:
		prettyprint_docstring('submit', cmd_submit.__doc__, 'Not enough arguments')
		sys.exit(1)

	cmd.set_pipe_path(pipe_path)
	if len(params) and len(params[0].split('=')) != 1:
		# dictionary-based parameters
		param_dict = {}
		for p in params:
			ary = p.split('=', 1)
			if len(ary) != 2:
				print("You can't mix positional arguments and key=value pairs")
				sys.exit(1)
			param_dict[ary[0]] = ary[1]
		params = param_dict
	if cmd.set_params(params) == False:
		print("Failed to set parameters for command %s" % cmd.name)
		print("The following parameters are required:")
		search(cmd.name)
		sys.exit(1)

	if cmd.submit() == True:
		print("%sOK%s: Successfully submitted %s for processing" %
			(color.green, color.reset, cmd.command_string))
	else:
		print("%sERROR%s: Failed to submit %s for processing" %
			(color.red, color.reset, cmd.command_string))
		sys.exit(1)
