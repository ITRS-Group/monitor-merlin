import os, sys, re, time, copy, errno

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from merlin_status import *
import nagios_plugin as nplug
import merlin_conf as mconf
import merlin_db
from coredump import *

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

	dbc = merlin_db.connect(mconf).cursor()
	mst = merlin_status(dbc)
	return rem_args

def cmd_distribution(args):
	"""[--no-perfdata]
	Checks to make sure work distribution works ok. Note that it's
	not expected to work properly the first couple of minutes after
	a new machine has been brought online or taken offline
	"""
	print_perfdata = True
	for arg in args:
		if arg == '--no-perfdata':
			print_perfdata = False

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
		should[name] = ac = mst.assigned_checks(node, info)
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
			'host': mst.num_entries('host', None, info['basic']['iid']),
			'service': mst.num_entries('service', None, info['basic']['iid']),
		}
		for ctype, num in total_checks.items():
			# see if we should add one to the OK count
			plus = 0
			if node.ntype != 'master' and node.num_peers:
				if cmax[ctype] % (node.num_peers + 1):
					cmax[ctype] = cmax[ctype] + 1

			# we now how many checks the node should run, so
			# construct performance-data, check our state and
			# set up data dicts so we later can construct a
			# meaningful error message
			run = runs_checks[ctype]
			if run < cmin[ctype] or run > cmax[ctype]:
				state = nplug.STATE_CRITICAL
				bad[name] = copy.deepcopy(runs_checks)

			absmax = total_checks[ctype]
			ok_str = "%d:%d" % (cmin[ctype], cmax[ctype] + plus)
			pdata += (" '%s_%ss'=%d;%s;%s;0;%d" %
				(name, ctype, run, ok_str, ok_str, ctot[ctype] + plus))

	for name, b in bad.items():
		if not len(b):
			continue
		state_str += ("%s runs %d / %d checks (should be %d / %d)." %
			(name, bad[name]['host'], bad[name]['service'],
			should[name]['host'], should[name]['service']))

	sys.stdout.write("%s: " % nplug.state_name(state))
	if not state_str:
		state_str = "All %d nodes run their assigned checks." % len(nodes)
	sys.stdout.write("%s" % state_str.rstrip())
	if print_perfdata:
		print("|%s" % pdata.lstrip())
	else:
		sys.stdout.write("\n")
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
		thr = thresh[thresh_type]
		i = 0
		for th in thr:
			what = order[i]
			if values[what] >= th:
				# since we set state for critical first, we can
				# just overwrite it here as we'll never get here for
				# warnings if we already have a critical issue
				state = nplug.state_code(thresh_type)
				output.append()

	i = 0
	state = nplug.STATE_OK
	perfdata_prefix = "%s_%s_" % (otype, col)
	perfdata = ''
	for o in order:
		cval = thresh['critical'][i]
		wval = thresh['warning'][i]
		i += 1
		value = values[o]
		perfdata = "%s '%s%s'=%.3f;%.3f;%.3f;0;" % (
			perfdata, perfdata_prefix, o, value, wval, cval)
		if value >= cval:
			state = nplug.STATE_CRITICAL
		elif value >= wval and state != nplug.STATE_CRITICAL:
			state = nplug.STATE_WARNING
	print("%s: %s %s min/avg/max = %.2f/%.2f/%.2f|%s" %
		(nplug.state_name(state), otype, col, values['min'], values['avg'], values['max'], perfdata))
	sys.exit(state)


def cmd_exectime(args=False):
	"""[host|service] --warning=<min,max,avg> --critical=<min,max,avg>
	Checks execution time of active checks
	"""
	thresh = {
		'warning': [5, 30, 65],
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
	max_age = 1800
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
		elif arg.startswith('--maxage=') or arg.startswith('--max-age='):
			max_age = arg.split('=', 1)[1]
			max_age = int(max_age)
		elif arg == 'host' or arg == 'service':
			otype = arg

	now = time.time()
	orphans = {'host': 0, 'service': 0}
	# Todo: Check things that are in their checking period
	query = ("""SELECT COUNT(1) FROM %s WHERE
		should_be_scheduled = 1 AND check_period = '24x7' AND
		next_check < %d""" % (otype, now - max_age))
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


def get_files(dir, regex=False, result=[]):
	"""
	Fetches files from dir matching regex and stashes them in result.
	Used by fe 'mon check cores'.
	"""
	if dir[-1] == '/':
		dir = dir[:-1]

	for d in os.listdir(dir):
		path = "%s/%s" % (dir, d)
		if regex == False or re.match(regex, d):
			result.append(path)

	return result

def cmd_spool(args=False):
	"""[--maxage=<seconds>] [--warning=X] [--critical=X] <path> [--delete]
	Checks a certain spool directory for files (and files only) that
	are older than 'maxage'. It's intended to prevent buildup of
	checkresult files and unprocessed performance-data files in the
	various spool directories used by op5 Monitor.
	  --delete causes too old files to be removed.
	  --maxage is given in seconds and defaults to 300 (5 minutes).
	  <path> may be 'perfdata' or 'checks', in which case directory
	  names will be taken from op5 defaults
	  --warning and --critical have no effect if '--delete' is given
	  and will otherwise specify threshold values.

	Only one directory at a time may be checked.
	"""
	maxage = 300
	warning = 5
	critical = 10
	path = False
	delete = False
	for arg in args:
		if arg.startswith('--maxage='):
			maxage = str_to_seconds(arg.split('=', 1)[1])
		elif arg.startswith('--warning='):
			warning = int(arg.split('=', 1)[1])
		elif arg.startswith('--critical='):
			critical = int(arg.split('=', 1)[1])
		elif arg == '--delete':
			delete = True
		elif path == False:
			path = arg

	if path == False:
		nplug.unknown("'path' is a required argument")

	if path == 'checks':
		path = '/opt/monitor/var/spool/checkresults'
	elif path == 'perfdata':
		path = '/opt/monitor/var/spool/perfdata'

	bad = 0
	bad_paths = []
	now = int(time.time())
	try:
		result = get_files(path)
	except OSError, e:
		nplug.die(nplug.STATE_UNKNOWN, "Spool directory \"%s\" doesn't exist" % (path,))

	for p in result:
		try:
			st = os.stat(p)
		except OSError, e:
			# since it's a spool directory it's quite normal
			# for files to disappear from it while we're
			# scanning it.
			if e.errno == errno.ESRCH:
				pass

		if st.st_mtime < (now - maxage):
			bad_paths.append(p)
			bad += 1

	perfdata = "old_files=%d;%d;%d;;" % (bad, warning, critical)
	# if we're supposed to just clean up, remove all bad files
	if delete:
		try:
			for p in bad_paths:
				os.unlink(p)
		except OSError, e:
			pass

	state = nplug.STATE_OK
	if bad >= critical:
		state = nplug.STATE_CRITICAL
	elif bad >= warning:
		state = nplug.STATE_WARNING

	nplug.die(state, "%d files too old|%s" % (bad, perfdata))
	sys.exit(state)

def cmd_cores(args=False):
	"""--warning=X --critical=X [--dir=]
	Checks for memory dumps resulting from segmentation violation from
	core parts of op5 Monitor. Detected core-files are moved to
	/tmp/mon-cores in order to keep working directories clean.
	  --warning  default is 0
	  --critical default is 1 (any corefile results in a critical alert)
	  --dir      lets you specify more paths to search for corefiles. This
	             option can be given multiple times.
	  --delete   deletes corefiles not coming from 'merlind' or 'monitor'
	"""
	warn = 0
	crit = 1
	dirs = ['/opt/monitor', '/opt/monitor/op5/merlin']
	delete = False
	debug = False
	for arg in args:
		if arg.startswith('--warning='):
			warn = int(arg.split('=', 1)[1])
		elif arg.startswith('--critical='):
			crit = int(arg.split('=', 1)[1])
		elif arg.startswith('--dir='):
			dirs.append(arg.split('=', 1)[1])
		elif arg == '--delete' or arg == '-D':
			delete = True
		elif arg == '--debug' or arg == '-d':
			debug = True
		else:
			nplug.unknown("Unknown argument: %s" % arg)

	core_pattern = '^core\..*'
	result = []
	for d in dirs:
		get_files(d, core_pattern, result)
	cores = 0
	for corefile in result:
		core = coredump(corefile)
		core.examine()
		if core.invalid:
			if debug:
				print("Core is invalid: %s" % core.invalid_str())
			elif delete:
				try:
					os.unlink(corefile)
				except OSError:
					pass
			continue
		cores += 1
	if not cores:
		valid = ''
		if len(result):
			valid = '(valid) '
		nplug.ok("No %scorefiles found|cores=0;%d;%d;;" % (valid, warn, crit))

	state = nplug.STATE_OK
	if cores >= crit:
		state = nplug.STATE_CRITICAL
	elif cores >= warn:
		state = nplug.STATE_WARNING
	print("%s: %d corefiles found" % (nplug.state_name(state), cores))
	sys.exit(state)
