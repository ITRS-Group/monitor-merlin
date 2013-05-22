import os, sys, re, time, errno

import livestatus

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
from merlin_status import *
import nagios_plugin as nplug
import merlin_conf as mconf
import merlin_db
from coredump import *
import compound_config as cconf

# defaults, will use nagios.cfg for guidance
ls_path = '/opt/monitor/var/rw/live'
qh = '/opt/monitor/var/rw/nagios.qh'

lsc = False
wanted_types = False
wanted_names = False
have_type_arg = False
have_name_arg = False

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg
	global mst, lsc, ls_path, qh

	qh_path = False
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
		elif arg.startswith('--qh-socket='):
			qh_path = arg.split('=', 1)[1]
		else:
			# not an argument we recognize, so stash it and move on
			rem_args += [arg]
			continue

	if os.access(nagios_cfg, os.R_OK):
		comp = cconf.parse_nagios_cfg(nagios_cfg)
		qh = comp.query_socket
		for k, v in comp.params:
			if k == 'broker_module' and 'livestatus' in v:
				ary = v[1].rsplit(' ')
				for p in ary[1:]:
					if not '=' in p:
						ls_path = p
						break

	if qh_path:
		qh = qh_path
	lsc = livestatus.SingleSiteConnection('unix:' + ls_path)
	return rem_args


# When casting to int *everywhere* gets tedious...
class _dict2obj:
	def __init__(self, d):
		for k, v in d.items():
			try:
				i = int(v)
				setattr(self, k, i)
			except:
				try:
					x = float(v)
					setattr(self, k, x)
				except:
					setattr(self, k, v)
					pass

	def get(self, what, dflt=None):
		return getattr(self, what, dflt)

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

	info = get_merlin_nodeinfo(qh)
	checktot = {
		'host': { 'assigned': 0, 'executed': 0 },
		'service': { 'assigned': 0, 'executed': 0 },
	}
	hchecks = 0; schecks = 0; hchecks_exec = 0; schecks_exec = 0
	nodes = []
	for i in info:
		n = _dict2obj(i)
		nodes.append(n)
		hchecks += n.assigned_hosts
		hchecks_exec += n.host_checks_executed
		schecks += n.assigned_services
		schecks_exec += n.service_checks_executed
		for ctype in checktot.keys():
			checktot[ctype]['assigned'] = getattr(n, 'assigned_%ss' % ctype)
			checktot[ctype]['executed'] = getattr(n, '%s_checks_executed' % ctype)

	state_str = ""
	should = {}
	if not nodes:
		print "UNKNOWN: No hosts found at all"
		sys.exit(nplug.UNKNOWN)

	class check_objs(object):
		pdata = ''
		bad = {}
		state = nplug.OK

		def is_bad(self, actual, exp):
			return actual < exp[0] or actual > exp[1]

		def verify_executed_checks(self, info, exp):
			for ctype, num in exp.items():
				actual = getattr(n, ctype + '_checks_executed')
				ok_str = "%d:%d" % (num[0], num[1])
				self.pdata += (" '%s_%ss'=%d;%s;%s;0;%d" %
					(info.name, ctype, actual, ok_str, ok_str, checktot[ctype]['assigned']))
				if self.is_bad(actual, num):
					self.state = nplug.STATE_CRITICAL
					self.bad[info.name] = {
						'host': info.host_checks_executed,
						'service': info.service_checks_executed
					}

	o = check_objs()
	hc_delta = hchecks - hchecks_exec
	sc_delta = schecks - schecks_exec

	# if either delta is wrong, we just set it to 0.
	# That will cause the check to go CRITICAL, which
	# is just fine.
	if hc_delta < 0:
		print("hchecks=%d; hchecks_exec=%d; delta=%d" %
			(hchecks, hchecks_exec, hcheck_delta))
		hc_delta = 0
	if sc_delta < 0:
		print("schecks=%d; schecks_exec=%d; delta=%d" %
			(schecks, schecks_exec, scheck_delta))
		sc_delta = 0

	for n in nodes:
		should[n.name] = {
			'host': (
				int(n.get('assigned_hosts')) - hc_delta,
				int(n.get('assigned_hosts')) + hc_delta
			),
			'service': (
				int(n.get('assigned_services')) - sc_delta,
				int(n.get('assigned_services')) + sc_delta
			)
		}
		o.verify_executed_checks(n, should[n.name])

	for name, b in o.bad.items():
		if not len(b):
			continue
		state_str += ("%s runs %d / %d checks (should be %s / %s). " %
			(name, int(b['host']), int(b['service']),
			(should[name]['host'][0] == should[name]['host'][1] and should[name]['host'][0]) or ("%s-%s" % (should[name]['host'][0], should[name]['host'][1])),
			 (should[name]['service'][0] == should[name]['service'][1] and should[name]['service'][0]) or ("%s-%s" % (should[name]['service'][0], should[name]['service'][1]))))

	sys.stdout.write("%s: " % nplug.state_name(o.state))
	if not state_str:
		state_str = "All %d nodes run their assigned checks." % len(nodes)
	sys.stdout.write("%s" % state_str.rstrip())
	if print_perfdata:
		print("|%s" % o.pdata.lstrip())
	else:
		sys.stdout.write("\n")
	sys.exit(o.state)


def check_min_avg_max(args, col, defaults=False, filter=False):
	order = ['min', 'avg', 'max']
	thresh = {}
	otype = False

	mst = merlin_status(lsc, qh)
	if filter == False:
		filter = 'Filter: should_be_scheduled = 1\nFilter: active_checks_enabled = 1\nAnd: 2\n'

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
	try:
		values = mst.min_avg_max(otype, col, filter)
	except livestatus.livestatus.MKLivestatusSocketError:
		print "UNKNOWN: Error asking livestatus for info, bailing out"
		sys.exit(nplug.STATE_UNKNOWN)
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
	query = 'GET %ss\nFilter: should_be_scheduled = 1\nFilter: in_check_period = 1\nFilter: next_check < %s\nStats: state != 999'
	try:
		orphans = int(lsc.query_value(query % (otype, now - max_age)))
		if not warning and not critical:
			total = int(lsc.query_value('GET %ss\nFilter: should_be_scheduled = 1\nFilter: in_check_period = 1\nStats: state != 999' % (otype,)))
			critical = total * 0.01
			warning = total * 0.005
	except livestatus.livestatus.MKLivestatusSocketError:
		print "UNKNOWN: Error asking livestatus for info, bailing out"
		sys.exit(nplug.UNKNOWN)
	if not warning and not critical:
		warning = critical = 1

	if orphans > critical:
		state = nplug.CRITICAL
	elif orphans > warning:
		state = nplug.WARNING

	sys.stdout.write("%s: Orphaned %s checks: %d / %d" % (nplug.state_name(state), otype, orphans, total))
	print("|'orphans'=%d;%d;%d;0;%d" % (orphans, warning, critical, total))
	sys.exit(state)

def cmd_status(args=False):
	"""Check that all nodes are connected and run checks (analogous to mon node check)"""
	state = nplug.OK
	sinfo = list(get_merlin_nodeinfo(qh))
	if not sinfo:
		nplug.unknown("Found no checks, is nagios running?")

	host_checks = 0
	service_checks = 0
	for info in sinfo:
		t = info.get('type')
		if t in ('peer', 'local'):
			host_checks += int(info['host_checks_handled'])
			service_checks += int(info['service_checks_handled'])

	num_helpers = sum(mconf.num_nodes[x] for x in mconf.num_nodes.values() if x in ('peer', 'poller'))
	for info in sinfo:
		if info.get('state') != 'STATE_CONNECTED':
			print "Error: %s is not connected." % (info['name'])
			state = nplug.worst_state(state, nplug.CRITICAL)
			continue
		if not info.has_key('latency'):
			print "Error: Can't find latency information for %s - this shouldn't happen." % (info['name'])
			state = nplug.worst_state(state, nplug.CRITICAL)
		elif int(info['latency']) > 5000 or int(info['latency']) < -2000:
			print "Warning: Latency for %s is %ss." % (info['name'], float(info['latency']) / 1000)
			state = nplug.worst_state(state, nplug.WARNING)
		if info['type'] == 'peer' \
				and not (int(info.get('connect_time', 0)) + 30 > int(time.time())) \
				and info.get('self_assigned_peer_id', 0) != info.get('peer_id', 0):
			print "Warning: Peer id mismatch for %s: self-assigned=%d; real=%d." % (
					info['name'],
					info.get('self_assigned_peer_id', 0),
					info.get('peer_id', 0))
			state = nplug.worst_state(state, nplug.WARNING)

		if not info.get('last_action'):
			print "Warning: Unable to determine when %s was last alive." % (info['name'])
			state = nplug.worst_state(state, nplug.UNKNOWN)
		elif not (int(info.get('last_action', 0)) + 30 > time.time()):
			print "Error: %s hasn't checked in recently." % (info['name'])
			state = nplug.worst_state(state, nplug.CRITICAL)

		node = mconf.configured_nodes.get(info['name'], False)
		if int(info.get('instance_id', 0)) and not node:
			print "Warning: Connected node %s is currently not int the configuration file." % (info['name'])
			state = nplug.worst_state(state, nplug.WARNING)
			continue
		if node:
			if node.ntype == 'master':
				if (int(info.get('host_checks_executed', 0)) or int(info.get('service_checks_executed', 0))):
					print "Error: Master %s should not run (visible) checks." % (info['name'])
					state = nplug.worst_state(state, nplug.CRITICAL)
			elif not int(info.get('host_checks_executed', 0)) and not int(info.get('service_checks_executed', 0)):
				print "Error: Node %s runs no checks." % (info['name'])
				state = nplug.worst_state(state, nplug.CRITICAL)
		if not int(info.get('instance_id', 0)) and num_helpers:
			if info.get('host_checks_executed', 0) == host_checks:
				print "Error: There are other nodes, but this node runs all host checks."
				state = nplug.worst_state(state, nplug.CRITICAL)
			if info.get('service_checks_executed', 0) == host_checks:
				print "Error: There are other nodes, but this node runs all service checks."
				state = nplug.worst_state(state, nplug.CRITICAL)
	if state == nplug.OK:
		nodes = []
		types = ['master', 'peer', 'poller', 'local']
		for type in types:
			n = sum(1 for x in sinfo if x['type'] == type)
			if n != 0:
				nodes.append('%s %s' % (n, type if n == 1 else type + 's'))
		print "OK: Total nodes: %s." % (", ".join(nodes))
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
	npcd_config = '/opt/monitor/etc/pnp/npcd.cfg'
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
		if os.access(npcd_config, os.R_OK):
			comp = cconf.parse_conf(npcd_config)
			for k, v in comp.params:
				if k == 'perfdata_spool_dir':
					path = v
					break
			comp = False
		else:
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

	if delete:
		msg = "%d too old files were deleted|%s" % (bad, perfdata)
	else:
		msg = "%d files too old|%s" % (bad, perfdata)

	nplug.die(state, msg)
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
