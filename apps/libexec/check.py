import os, sys, re, time, copy

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from merlin_status import *
import nagios_plugin as nplug
import merlin_conf as mconf
import merlin_db

dbc = False
mst = False
wanted_types = False
wanted_names = False
have_type_arg = False
have_name_arg = False

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg
	global dbc, mst

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
	dbc = merlin_db.connect(mconf).cursor()
	mst = merlin_status(dbc)
	return rem_args

def cmd_distribution(args):
	"""
	Checks to make sure work distribution works ok. Note that it's
	not expected to work properly the first couple of minutes after
	a new machine has been brought online or taken offline
	"""
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


def check_min_avg_max(args, col, defaults=False, filter=False):
	order = ['min', 'avg', 'max']
	thresh = {}
	otype = False

	if filter == False:
		filter = 'should_be_scheduled = 1 AND active_checks_enabled = 1'

	for arg in args:
		if arg.startswith('--warning=') or arg.startswith('--critical'):
			(what, thvals) = arg[2:].split('=')
			thvals = thvals.split(',')
			if len(thvals) != 3:
				nplug.unknown("Bad argument: %s" % arg)

			thresh[what] = []
			for th in thvals:
				thresh[what].append(float(th))

		elif arg == 'host' or arg == 'service':
			otype = arg
		else:
			nplug.unknown("Unknown argument: %s" % arg)

	for t in ['critical', 'warning']:
		if not thresh.get(t, False) and defaults.get(t, False) != False:
			thresh[t] = defaults[t]

	if not otype:
		nplug.unknown("Need 'host' or 'service' as argument")

	state = nplug.STATE_OK
	values = mst.min_avg_max(otype, col, filter)
	output = []
	for thresh_type in ['critical', 'warning']:
		if state != nplug.STATE_OK:
			break
		thr = threst[thresh_type]
		i = 0
		for th in thr:
			what = order[i]
			if values[what] >= th:
				# since we set state for critical first, we can
				# just overwrite it here as we'll never get here for
				# warnings if we already have a critical issue
				state = nplug.state_code(thresh_type)
				output.append()

	print(values, thresh['critical'])


def cmd_exectime(args=False):
	"""[host|service] --warning=<min,max,avg> --critical=<min,max,avg>
	Checks execution time of active checks
	"""
	thresh = {
		'warning': [5, 20, 50],
		'critical': [15, 45, 90]
	}
	check_min_avg_max(args, 'execution_time', thresh)

def cmd_latency(args=False):
	"""[host|service] --warning=<min,max,avg> --critical=<min,max,avg>
	Checks latency of active checks
	"""
	thresh = {
		'warning': [30, 60, 90],
		'critical': [60, 90, 120]
	}
	check_min_avg_max(args, 'latency', thresh)

def cmd_orphans(args=False):
	"""
	Checks for checks that haven't been run in too long a time
	"""
	state = nplug.OK
	otype = 'service'
	unit = ''
	warning = critical = 0
	for arg in args:
		if arg.startswith('--warning='):
			val = arg.split('=', 1)[1]
			if '%' in val:
				unit = '%'
				val = val.replace('%', '')
			warning = float(val)
		elif arg.startswith('--critical='):
			val = arg.split('=', 1)[1]
			if '%' in val:
				unit = '%'
				val = val.replace('%', '')
			critical = float(val)
		elif arg == 'host' or arg == 'service':
			otype = arg

	now = time.time()
	orphans = {'host': 0, 'service': 0}
	# Todo: Check things that are in their checking period
	query = ("""SELECT COUNT(1) FROM %s WHERE
		should_be_scheduled = 1 AND check_period = '24x7' AND
		next_check < %d""" % (otype, now - 1800))
	dbc.execute(query)
	row = dbc.fetchone()
	orphans = int(row[0])
	total = mst.num_entries(otype, "check_period = '24x7'")
	# by default, critical is at 1% and warning at 0.5%
	if not warning and not critical:
		critical = total * 0.01
		warning = total * 0.005
	if not warning and not critical:
		warning = critical = 1

	if orphans > critical:
		state = nplug.CRITICAL
	elif orphans > warning:
		state = nplug.WARNING

	sys.stdout.write("%s: Orphaned %s checks: %d / %d" % (nplug.state_name(state), otype, orphans, total))
	print("|'orphans'=%d;%d;%d;0;%d" % (orphans, warning, critical, total))
	sys.exit(state)
