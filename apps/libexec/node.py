import os, sys, re, time, socket, select

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from merlin_qh import *
import compound_config as cconf

wanted_types = []
wanted_names = []
have_type_arg = False
have_name_arg = False

qh = '/opt/monitor/var/rw/nagios.qh'

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg
	global qh

	comp = cconf.parse_conf(nagios_cfg)
	for v in comp.params:
		if v[0] == 'query_socket':
			qh = v[1]
			break

	wanted_types = mconf.merlin_node.valid_types
	rem_args = []
	i = -1
	for arg in args:
		i += 1
		# stop parsing immediately if we hit double-dashes, or we
		# may well break recursive commands sent with mon node ctrl.
		if arg == '--':
			rem_args += args[i:]
			break
		if arg.startswith('--merlin-cfg=') or arg.startswith('--config='):
			mconf.config_file = arg.split('=', 1)[1]
		elif arg.startswith('--type='):
			wanted_types = arg.split('=')[1].split(',')
			have_type_arg = True
		elif arg.startswith('--name='):
			wanted_names = arg.split('=')[1].split(',')
			have_name_arg = True
		else:
			# not an argument we recognize, so stash it and move on
			rem_args += [arg]
			continue

	# load the merlin configuration, and thus all nodes
	mconf.parse()
	return rem_args

def cmd_status(args):
	"""
	Show status of all nodes configured in the running Merlin daemon
	Red text points to problem areas, such as high latency or the node
	being inactive, not handling any checks,  or not sending regular
	enough program_status updates.
	"""

	high_latency = {}
	inactive = {}
	mentioned = {}

	sinfo = get_merlin_nodeinfo(qh)

	if not sinfo:
		print("Found no checks")
		return

	host_checks = 0
	service_checks = 0
	for n in sinfo:
		t = n.get('type', False)
		# we only count the local node and its peers for totals
		if t == 'peer' or t == 'local':
			host_checks += int(n['host_checks_handled'])
			service_checks += int(n['service_checks_handled'])
	print("Total checks (host / service): %s / %s" % (host_checks, service_checks))

	latency_thresholds = {'min': -1.0, 'avg': 100.0, 'max': -1.0}

	num_peers = mconf.num_nodes['peer']
	num_pollers = mconf.num_nodes['poller']
	num_masters = mconf.num_nodes['master']
	num_helpers = num_peers + num_pollers
	for info in sinfo:
		print("")
		iid = int(info.pop('instance_id', 0))
		node = mconf.configured_nodes.get(info['name'], False)
		is_running = info.pop('state') == 'STATE_CONNECTED'
		if is_running:
			# latency is in milliseconds, so convert to float
			latency = float(info.pop('latency')) / 1000.0
			if latency > 2.0 or latency < 0.0:
				lat_color = color.yellow
			else:
				lat_color = color.green

		peer_id = int(info.pop('peer_id', 0))
		name = "#%02d.%d %s %s" % (iid, peer_id, info['type'], info['name'])

		if is_running:
			name += " (%sACTIVE%s - %s%.3fs%s latency)" % (color.green, color.reset, lat_color, latency, color.reset)
			name_len = len(name) - (len(color.green) + len(lat_color) + (len(color.reset) * 2))
		else:
			name += " (%sINACTIVE%s)" % (color.red, color.reset)
			name_len = len(name) - (len(color.red) + len(color.reset))

		print("%s\n%s" % (name, '-' * name_len))

		sa_peer_id = int(info.pop('self_assigned_peer_id', 0))
		if info['type'] == 'peer' and sa_peer_id != peer_id:
			print("%sPeer id mismatch: self-assigned=%d; real=%d%s" %
				(color.yellow, sa_peer_id, peer_id, color.reset))
		if iid and not node:
			print("%sThis node is currently not in the configuration file%s" %
				(color.yellow, color.reset))

		last_alive = int(info.pop('last_action'))
		if not last_alive:
			print("%sUnable to determine when this node was last alive%s" %
				(color.red, color.reset))
		else:
			if last_alive + 30 > time.time():
				la_color = color.green
			else:
				la_color = color.red

			delta = time_delta(last_alive)
			dtime = time.strftime("%F %H:%M:%S", time.localtime(last_alive))
			print("Last alive: %s (%d) (%s%s ago%s)" %
				(dtime,	last_alive, la_color, delta, color.reset))

		hchecks = int(info.pop('host_checks_executed'))
		schecks = int(info.pop('service_checks_executed'))
		hc_color = sc_color = ''

		# master nodes should never run checks
		hc_color = ''
		sc_color = ''
		if node:
			# if the node is a master and it's running checks,
			# that's an error
			if node.ntype == 'master':
				if hchecks:
					hc_color = color.red
				if schecks:
					sc_color = color.red
			else:
				# if it's not and it's not running checks,
				# that's also an error
				if not hchecks:
					hc_color = color.red
				if not schecks:
					sc_color = color.red

		# if this is the local node, we have helpers attached
		# and we're still running all checks, that's bad
		if not iid and num_helpers:
			if hchecks == host_checks:
				hc_color = color.red
			if schecks == service_checks:
				sc_color = color.red

		hpercent = 0
		if host_checks != 0:
			hpercent = float(hchecks) / float(host_checks) * 100
		spercent = 0
		if service_checks != 0:
			spercent = float(schecks) / float(service_checks) * 100

		print("Checks (host/service): %s%d%s / %s%d%s  (%s%.2f%%%s / %s%.2f%%%s)" %
			(hc_color, hchecks, color.reset, sc_color, schecks, color.reset,
			hc_color, hpercent, color.reset,
			sc_color, spercent, color.reset))

## node commands ##
# list configured nodes, capable of filtering by type
def cmd_list(args):
	"""[--type=poller,peer,master]
	Lists all nodes of the (optionally) specified type
	"""
	global wanted_types
	for node in mconf.configured_nodes.values():
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


def cmd_show(args):
	"""[--type=poller,peer,master]
	Display all variables for all nodes, or for one node in a fashion
	suitable for being used as
	  eval $(mon node show nodename)
	from shell scripts and scriptlets
	"""

	if len(mconf.configured_nodes) == 0:
		print("No nodes configured")
		return
	if len(args) == 0:
		names = mconf.configured_nodes.keys()
		names.sort()
		for name in names:
			node = mconf.configured_nodes[name]
			if not node.ntype in wanted_types:
				continue
			print("\n# %s" % name)
			node.show()

	for arg in args:
		if not arg in mconf.configured_nodes.keys():
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			# scripts will list one node at a time. If the command
			# fails, they don't have to check the output to see if
			# the got anything sensible or not
			if len(args) == 1:
				sys.exit(1)
			continue
		if not mconf.configured_nodes[arg].ntype in wanted_types:
			continue
		if len(args) > 1:
			print("\n# %s" % arg)
		mconf.configured_nodes[arg].show()


def cmd_add(args):
	"""<name> type=[peer|poller|master] [var1=value] [varN=value]
	Adds a node with the designated type and variables
	"""
	if len(args) < 1:
		return False
	name = args[0]
	if name in mconf.configured_nodes.keys():
		print("%s is already configured. Aborting" % name)
		return False

	node = mconf.merlin_node(name)
	node.path = mconf.config_file

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
	if not name in mconf.configured_nodes.keys():
		print("%s isn't configured yet. Use 'add' to add it" % name)
		return False

	node = mconf.merlin_node(args[0])
	for arg in args[1:]:
		node.set(arg)

	return node.save()


def cmd_remove(args):
	"""<name1> [name2] [nameN]
	Removes one or more nodes from the merlin configuration.
	"""
	if len(args) == 0:
		print("Which node do you want to remove? Try the 'list' command")
		return

	for arg in args:
		if not arg in mconf.configured_nodes:
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			continue

		node = mconf.configured_nodes.pop(arg)
		sed_range = str(node.comp.line_start) + ',' + str(node.comp.line_end)
		cmd_args = ['sed', '-i', sed_range + 'd', mconf.config_file]
		os.spawnvp(os.P_WAIT, 'sed', cmd_args)


def cmd_ctrl(args):
	"""<name1> <name2> [--self] [--all|--type=<peer|poller|master>] -- <command>
	Execute <command> on the remote node(s) named. --all means run it on
	all configured nodes, as does making the first argument '--'.
	--type=<types> means to run the command on all configured nodes of
	the given type(s).
	The first not understood argument marks the start of the command,
	but always using double dashes is recommended. Use single-quotes
	to execute commands with shell variables, output redirection or
	scriptlets, like so:
	  mon node ctrl -- '(for x in 1 2 3; do echo $x; done) > /tmp/foo'
	  mon node ctrl -- cat /tmp/foo
	"""

	global wanted_names

	if len(args) == 0:
		print("Control without commands seems quite pointless. I'm going home")
		print("Try 'mon node help' for some assistance")
		sys.exit(1)

	run_on_self = False
	by_name = False
	nodes = {}
	cmd_args = []
	i = -1
	for arg in args:
		i += 1
		if arg == '--all':
			wanted_names = mconf.configured_nodes.keys()
			continue
		elif arg == '--self':
			run_on_self = True
			continue
		elif arg == '--':
			# double dashes means "here's the command"
			cmd_args = args[i + 1:]
			# if it's the first argument we got, the user wants
			# to run the command on all configured nodes, possibly
			# including --self
			if not len(wanted_names):
				wanted_names = mconf.configured_nodes.keys()
			break

		node = mconf.configured_nodes.get(arg, False)
		if node == False:
			cmd_args = args[i:]
			break
		wanted_names += [node.name]
		by_name = True

	if have_type_arg and not len(wanted_names):
		wanted_names = mconf.configured_nodes.keys()

	for name in wanted_names:
		node = mconf.configured_nodes.get(name, False)
		if not node:
			print("%s is not a configured node. Aborting" % name)
			sys.exit(1)

		# check type and add it if we want it
		if node.ntype in wanted_types:
			nodes[name] = node

	if (not len(nodes) or not len(cmd_args)) and not run_on_self:
		print("No nodes, or no commands to send. Aborting")
		sys.exit(1)

	cmd = ' '.join(cmd_args)
	if run_on_self:
		os.spawnvp(os.P_WAIT, "/bin/sh", ["/bin/sh", "-c", cmd])

	for name, node in nodes.items():
		if str(node.connect) != "no":
			node.ctrl(cmd)

	if by_name and len(wanted_names) == 1:
		sys.exit(node.get_exit_code())

def _cmd_rename(args):
	if len(args) != 2 or not args[0] in mconf.configured_nodes.keys():
		print("Which node do you want to rename? Try the 'list' command")
		return False

	node = mconf.configured_nodes[args[0]]
	dest = args[1]
	if dest in mconf.configured_nodes.keys():
		print("A node named '%s' already exists. Remove it first" % dest)
		return False
	mconf.configured_nodes.pop(args[0])
	mconf.configured_nodes[dest] = node
	node.rename(dest)

def cmd_info(args):
    """
    Print verbose information about all merlin nodes
    """
    nodes = get_merlin_nodeinfo(qh)
    for node in nodes:
        print node['name']
        items = node.items()
        items.sort()
        for key, val in items:
            print '    %s = %s' % (key, val)
        print ''
