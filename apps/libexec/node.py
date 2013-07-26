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

query_socket = False

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg
	global query_socket

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
		elif arg.startswith('--socket='):
			query_socket = arg.split('=', 1)[1]
		else:
			# not an argument we recognize, so stash it and move on
			rem_args += [arg]
			continue

	if not query_socket:
		if os.access(nagios_cfg, os.R_OK):
			comp = cconf.parse_nagios_cfg(nagios_cfg)
			query_socket = comp.query_socket
		else:
			query_socket = '/opt/monitor/var/rw/nagios.qh'

	# load the merlin configuration, and thus all nodes
	mconf.parse()
	return rem_args

def cmd_status(args):
	"""
	Shows status of all nodes configured in the running Merlin daemon.
	Red text points to problem areas, such as high latency or the node
	being inactive, not handling any checks, or not sending regular
	enough program_status updates.
	"""

	high_latency = {}
	inactive = {}
	mentioned = {}
	pg_oconf_hash = {}
	pg_conf = {}

	sinfo = list(get_merlin_nodeinfo(query_socket))

	if not sinfo:
		print("Found no checks")
		return

	host_checks = 0
	service_checks = 0
	for n in sinfo:
		host_checks += int(n['assigned_hosts'])
		service_checks += int(n['assigned_services'])

	print("Total checks (host / service): %s / %s" % (host_checks, service_checks))

	latency_thresholds = {'min': -1.0, 'avg': 100.0, 'max': -1.0}

	for info in sinfo:
		print("")
		iid = int(info.get('instance_id', 0))
		node = mconf.configured_nodes.get(info['name'], False)
		is_running = info.get('state') == 'STATE_CONNECTED'
		if is_running:
			# latency is in milliseconds, so convert to float
			latency = float(info.get('latency')) / 1000.0
			if latency > 5.0 or latency < -2.0:
				lat_color = color.yellow
			else:
				lat_color = color.green
			pg_id = int(info.get('pgroup_id', -1))
			if pg_id != -1:
				oconf_hash = info.get('oconf_hash', False)
				if not pg_oconf_hash.get(pg_id, False):
					pg_oconf_hash[pg_id] = {}
				pg_oconf_hash[pg_id][info['name']] = oconf_hash
				confed_masters = info['configured_masters']
				confed_peers = info['configured_peers']
				confed_pollers = info['configured_pollers']
				if not pg_conf.get(pg_id):
					pg_conf[pg_id] = {}
				pg_conf[pg_id][info['name']] = {
					'masters': confed_masters,
					'peers': confed_peers,
					'pollers': confed_pollers,
				}

		if info['type'] == 'peer':
			peer_id = int(info.get('peer_id', 0))
		else:
			peer_id = int(info.get('self_assigned_peer_id', 0))
		name = "#%02d %d/%d:%d %s %s" % (
			iid, peer_id,
			int(info.get('active_peers', -1)), int(info.get('configured_peers', -1)),
			info['type'], info['name']
		)

		if is_running:
			name += ": %sACTIVE%s - %s%.3fs%s latency" % (color.green, color.reset, lat_color, latency, color.reset)
		else:
			name += " (%sINACTIVE%s)" % (color.red, color.reset)

		print("%s" % name)

		sa_peer_id = int(info.get('self_assigned_peer_id', 0))
		conn_time = int(info.get('connect_time', 0))
		if is_running and info['type'] == 'peer' and sa_peer_id != peer_id:
			if conn_time + 30 > int(time.time()):
				print("%sPeer id negotiation in progress%s" % (color.green, color.reset))
			else:
				print("%sPeer id mismatch: self-assigned=%d; real=%d%s" %
				(color.yellow, sa_peer_id, peer_id, color.reset))

		if iid and not node:
			print("%sThis node is currently not in the configuration file%s" %
				(color.yellow, color.reset))

		proc_start = float(info.get('start', False))
		if proc_start:
			uptime = time_delta(proc_start)
		else:
			uptime = 'unknown'
		conn_time = int(info.get('connect_time', False))
		if conn_time:
			conn_delta = time_delta(conn_time)
		else:
			conn_delta = 'unknown'
		last_alive = int(info.get('last_action'))
		if not last_alive:
			alive_delta = 'UNKNOWN'
			la_color = color.red
		else:
			alive_delta = time_delta(last_alive) + ' ago'
			if last_alive + 30 > time.time():
				la_color = color.green
			else:
				la_color = color.red

		print("Uptime: %s. Connected: %s. Last alive: %s%s%s" %
			(uptime, conn_delta, la_color, alive_delta, color.reset))

		hchecks = int(info.get('host_checks_executed'))
		schecks = int(info.get('service_checks_executed'))
		assigned_hchecks = int(info.get('assigned_hosts'))
		assigned_schecks = int(info.get('assigned_services'))
		pg_hosts = int(info.get('pgroup_hosts'))
		pg_services = int(info.get('pgroup_services'))

		hc_color = ''
		sc_color = ''
		if node:
			if hchecks != assigned_hchecks:
				hc_color = color.red
			if schecks != assigned_schecks:
				sc_color = color.red

		hpercent = 0
		if host_checks != 0:
			hpercent = float(hchecks) / float(host_checks) * 100
		spercent = 0
		if service_checks != 0:
			spercent = float(schecks) / float(service_checks) * 100

		print("Host checks (handled/assigned/total)   : %s%d%s/%d (%d) (%s%.2f%%%s : %.2f%%)" %
			(hc_color, hchecks, color.reset, assigned_hchecks, pg_hosts,
			hc_color, float(hchecks) / pg_hosts * 100, color.reset, hpercent))
		print("Service checks (handled/assigned/total): %s%d%s/%d (%d) (%s%.2f%%%s - %.2f%%)" %
			(sc_color, schecks, color.reset, assigned_schecks, pg_services,
			sc_color, float(schecks) / pg_services * 100, color.reset, spercent))

	oconf_bad = {}
	for pg_id, d in pg_oconf_hash.items():
		last = None
		if len(d) == 1:
			continue
		for name, hash in d.items():
			if last != None and last != hash:
				oconf_bad[pg_id] = d
				break

	if len(oconf_bad):
		print("\n%s%sObject config sync problem detected in the following groups%s" %
			(color.red, color.bright, color.reset))
		for pg_id, bad_nodes in oconf_bad.items():
			bad = bad_nodes.keys()
			bad.sort()
			print("  peer-group %d: %s" % (pg_id, ', '.join(bad)))
		print("%sPlease ensure that configuration synchronization works properly%s" %
			(color.yellow, color.reset))

	# now check merlin configuration
	pconf_bad = {}
	for pg_id, d in pg_conf.items():
		have_first = False
		if len(d) == 1:
			continue
		for name, d2 in d.items():
			cur_masters = d2.get('masters', -1)
			cur_peers = d2.get('peers', -1)
			cur_pollers = d2.get('pollers', -1)
			if have_first:
				if last_masters != cur_masters or last_peers != cur_peers or last_pollers != cur_pollers:
					pconf_bad[pg_id] = d

			last_masters = d2.get('masters', -1)
			last_peers = d2.get('peers', -1)
			last_pollers = d2.get('pollers', -1)
			have_first = True


	if len(pconf_bad):
		print("\n%s%sMerlin config error detected in the following groups%s" %
			(color.red, color.bright, color.reset))
		for pg_id, bad_nodes in pconf_bad.items():
			bad = bad_nodes.keys()
			bad.sort()
			print("  peer-group %d: %s" % (pg_id, ', '.join(bad)))
		print("%sPlease ensure that all peered nodes share neighbours%s" %
			(color.yellow, color.reset))


## node commands ##
# list configured nodes, capable of filtering by type
def cmd_list(args):
	"""[--type=poller,peer,master]
	Lists all nodes of the (optionally) specified type.
	"""
	global wanted_types

	names = mconf.configured_nodes.keys()
	names.sort()
	for name in names:
		node = mconf.configured_nodes[name]
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


def cmd_show(args):
	"""[--type=poller,peer,master]
	Display all variables for all nodes, or for one node in a fashion
	suitable for being used as
	  eval $(mon node show nodename)
	from shell scripts and scriptlets.
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
	Adds a node with the designated type and variables.
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

	blocked = 0
	for name, node in nodes.items():
		if str(node.connect) == "no":
			blocked += 1
		else:
			node.ctrl(cmd)

	if by_name and blocked == len(wanted_names):
		print("Only nodes which we can't connect to were chosen. Can't run command")
		sys.exit(3)

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
	Prints verbose information about all merlin nodes.
	"""
	nodes = get_merlin_nodeinfo(qh)
	for node in nodes:
		print node['name']
		items = node.items()
		items.sort()
		for key, val in items:
			print '    %s = %s' % (key, val)
		print ''
