import os, sys, re, time, copy

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from merlin_status import *
import nagios_plugin as nplug
import merlin_conf as mconf
import merlin_db

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg

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

def cmd_help(args=False):
	print("""
usage: mon node <command> [options]

Command overview
----------------
 orphans
   Checks for apparently orphaned checks

 stale
   Checks for apparently stale checks

 nodes
   Checks for offline peers, pollers and masters

 configsync
   Checks configuration synchronization between all peers and
   pollers in a very basic way. Pollers are only checked to make
   sure the config have a later timestamp than its master

 distribution
   Checks to make sure configuration distribution works ok. Note
   that it's not expected to work properly the first couple of
   minutes after a new machine has been brought online or taken
   offline

 latency
   Checks for unnaturally high latency values in active checks

 exectime
   Checks for unnaturally long execution time in active checks
""")

dbc = False

def cmd_distribution(args):
	global dbc
	dbc = merlin_db.connect(mconf).cursor()
	mst = merlin_status(dbc)

	# min and max number of checks
	total_checks = {
		'host': mst.num_entries('host'),
		'service': mst.num_entries('service'),
	}

	state = nplug.OK
	nodes = mst.status()
	pdata = ""
	state_str = ""
	bad = {}
	should = {}
	# loop all nodes, checking their status and creating the various
	# strings we'll want to print later
	for (name, info) in nodes.items():
		node = info['node']
		should[name] = ac = mst.assigned_checks(node)
		if node.ntype == 'master':
			cmax = cmin = {'host': 0, 'service': 0}
		elif node.num_peers:
			cmin = ac
			cmax = {'host': ac['host'], 'service': ac['service']}
		else:
			cmin = cmax = ac

		# pollers can never run more checks than they should. The
		# rest of them can always end up running all
		ctot = copy.copy(total_checks)
		if node.ntype == 'poller':
			ctot['host'] = ac['host'] * (node.num_peers + 1)
			ctot['service'] = ac['service'] * (node.num_peers + 1)

		# get the actual data immediately
		runs_checks = {
			'host': mst.num_entries('host', info['basic']['iid']),
			'service': mst.num_entries('service', info['basic']['iid']),
		}
		for ctype, num in total_checks.items():
			# see if we should add one to the OK count
			plus = 0
			if node.ntype != 'master' and node.num_peers:
				if cmax[ctype] % (node.num_peers + 1):
					cmax[ctype] += 1

			# we now how many checks the node should run, so
			# construct performance-data, check our state and
			# set up data dicts so we later can construct a
			# meaningful error message
			run = runs_checks[ctype]
			if run < cmin[ctype] or run > cmax[ctype]:
				state = nplug.STATE_CRITICAL
				bad[name] = copy.deepcopy(runs_checks)

			absmax = total_checks[ctype]
			ok_str = "@%d:%d" % (cmin[ctype], cmax[ctype] + plus)
			pdata += (" '%s_%ss'=%dchecks;%s;%s;0;%d" %
				(name, ctype, run, ok_str, ok_str, ctot[ctype] + plus))

	for name, b in bad.items():
		if not len(b):
			continue
		state_str += ("%s runs %d / %d checks (should be %d / %d). " %
			(name, bad[name]['host'], bad[name]['service'],
			should[name]['host'], should[name]['service']))

	sys.stdout.write("%s: " % nplug.state_name(state))
	if not state_str:
		state_str = "All %d nodes run their assigned checks." % len(nodes)
	sys.stdout.write("%s" % state_str.rstrip())
	print("|%s" % pdata.lstrip())
	sys.exit(state)

def cmd_exectime(args=False):
	print("stub")

def cmd_orphans(args=False):
	query = "SELECT"
	print("stub")
	sys.exit(0)
