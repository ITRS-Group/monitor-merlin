
import os, sys, re, time

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from merlin_apps_utils import *
import merlin_conf as mconf

wanted_types = mconf.merlin_node.valid_types
wanted_names = []
have_type_arg = False
have_name_arg = False

def module_init(args):
	global wanted_types, wanted_names
	global have_type_arg, have_name_arg

	rem_args = []
	for arg in args:
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
 add <name> type=[peer|poller|master] [var1=value] [varN=value]
   Adds a node with the designated type and variables

 remove <name1> [name2] [nameN]
   Removes a set of pollers

 list [--type=poller,peer,master]
   Lists all nodes of the (optionally) specified type

 show [--type=poller,peer,master]]
   Show all nodes of the selected type(s) in a shell-friendly fashion.

 show <name1> [name2] [nameN...]
   Show named nodes. eval'able from shell if only one node is chosen.

 ctrl <name1> <name2> [--all|--type=<peer|poller|master>] -- <command>
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

 status
   Show status of all nodes configured in the running Merlin daemon
   Red text points to problem areas, such as high latency or the node
   being inactive, not handling any checks,  or not sending regular
   enough program_status updates.
""")

def get_min_avg_max(table, column, iid=False):
	query = 'SELECT min(%s), avg(%s), max(%s) FROM %s' % \
		(column, column, column, table)

	if iid != False:
		query += ' WHERE instance_id = %d' % iid

	dbc.execute(query)
	row = dbc.fetchone()
	ret = {'min': row[0], 'avg': row[1], 'max': row[2]}
	return ret

def get_num_checks(table, iid=None):
	query = 'SELECT count(*) FROM %s' % table
	if iid != None:
		query += ' WHERE instance_id = %d' % iid

	dbc.execute(query)
	row = dbc.fetchone()
	return row[0]

def get_node_status():
	global dbc

	cols = {
		'instance_name': False, 'instance_id': False,
		'is_running': False, 'last_alive': False,
	}

	dbc.execute("select %s "
		"FROM program_status ORDER BY instance_name" % ', '.join(cols.keys()))

	result_set = {}
	for row in dbc.fetchall():
		res = {}
		node_name = row[0]
		col_num = 1
		for col in cols.keys()[1:]:
			res[col] = row[col_num]
			col_num += 1

		result_set[node_name] = res

	for (name, info) in result_set.items():
		iid = info['instance_id']
		for otype in ['host', 'service']:
			info['%s_checks' % otype] = get_num_checks(otype, iid)
			info['%s_latency' % otype] = get_min_avg_max(otype, 'latency', iid)
			info['%s_exectime' % otype] = get_min_avg_max(otype, 'execution_time', iid)

	return result_set

def fmt_min_avg_max(lat, thresh={}):
	values = []
	keys = []
	for (k, v) in lat.items():
		if v == None:
			continue
		v_color = ''
		max_v = thresh.get(k, -1)
		if max_v >= 0 and max_v < v:
			v_color = color.red
		keys.insert(0, k)
		values.insert(0, "%s%.3f%s" % (v_color, v, color.reset))

	if not values and not keys:
		return ": %s%s%s" % (color.red, "Unable to retrieve data", color.reset)
	
	ret = "(%s): %s" % (' / '.join(keys), ' / '.join(values))
	return ret

dbc = False
def cmd_status(args):
	global dbc
	db_host = mconf.dbopt.get('host', 'localhost')
	db_name = mconf.dbopt.get('name', 'merlin')
	db_user = mconf.dbopt.get('user', 'merlin')
	db_pass = mconf.dbopt.get('pass', 'merlin')
	db_type = mconf.dbopt.get('type', 'mysql')

	high_latency = {}
	inactive = {}
	mentioned = {}

	# now we load whatever database driver is appropriate
	if db_type == 'mysql':
		try:
			import MySQLdb as db
		except:
			print("Failed to import MySQLdb")
			print("Install mysqldb-python or MySQLdb-python to make this command work")
			sys.exit(1)
	elif db_type in ['postgres', 'psql', 'pgsql']:
		try:
			import pgdb as db
		except:
			print("Failed to import pgdb")
			print("Install postgresql-python to make this command work")
			sys.exit(1)

	#print("Connecting to %s on %s with %s:%s as user:pass" %
	#	(db_name, db_host, db_user, db_pass))
	conn = db.connect(host=db_host, user=db_user, passwd=db_pass, db=db_name)
	dbc = conn.cursor()
	status = get_node_status()
	latency_thresholds = {'min': -1.0, 'avg': 100.0, 'max': -1.0}
	sinfo = sorted(status.items())
	host_checks = get_num_checks('host')
	service_checks = get_num_checks('service')
	print("Total checks (host / service): %d / %d" % (host_checks, service_checks))

	num_peers = mconf.num_nodes['peer']
	num_pollers = mconf.num_nodes['poller']
	num_masters = mconf.num_nodes['master']
	num_helpers = num_peers + num_pollers
	for (name, info) in sinfo:
		print("")
		iid = info.pop('instance_id', 0)
		node = mconf.configured_nodes.get(name, False)
		is_running = info.pop('is_running')
		name = "#%02d: %s" % (iid, name)
		name_len = len(name) + 9
		if is_running:
			name += " (%sACTIVE%s)" % (color.green, color.reset)
		else:
			name += " (%sINACTIVE%s)" % (color.red, color.reset)
			name_len += 2

		print("%s\n%s" % (name, '-' * name_len))
		if iid and not node:
			print("%sThis node is currently not in the configuration file%s" %
				(color.yellow, color.reset))

		last_alive = info.pop('last_alive')
		if not last_alive:
			print("%sUnable to determine when this node was last alive%s" %
				(color.red, color.reset))
		else:
			if last_alive + 20 >= time.time():
				la_color = color.green
			else:
				la_color = color.red

			delta = time_delta(last_alive)
			dtime = time.strftime("%F %H:%m:%d", time.localtime(last_alive))
			print("Last alive: %s (%d) (%s%s ago%s)" %
				(dtime,	last_alive, la_color, delta, color.reset))

		hchecks = info.pop('host_checks')
		schecks = info.pop('service_checks')
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
				hc_color = color.red

		print("Checks (host/service): %s%d%s / %s%d%s  (%s%.2f%%%s / %s%.2f%%%s)" %
			(hc_color, hchecks, color.reset, sc_color, schecks, color.reset,
			hc_color, float(hchecks) / float(host_checks) * 100, color.reset,
			sc_color, float(schecks) / float(service_checks) * 100, color.reset))

		# if this node has never reported any checks, we can't
		# very well print out its latency or execution time values
		if not hchecks and not schecks:
			continue
		host_lat = fmt_min_avg_max(info.pop('host_latency'), latency_thresholds)
		service_lat = fmt_min_avg_max(info.pop('service_latency'), latency_thresholds)
		host_exectime = fmt_min_avg_max(info.pop('host_exectime'))
		service_exectime = fmt_min_avg_max(info.pop('service_exectime'))
		print("Host latency    %s" % host_lat)
		print("Service latency %s" % service_lat)
		#print("Host check execution time    %s" % host_exectime)
		#print("Service check execution time %s" % service_exectime)

		# should be empty by now
		for (k, v) in info.items():
			print("%s = %s" % (k, v))

	conn.close()

## node commands ##
# list configured nodes, capable of filtering by type
def cmd_list(args):
	global wanted_types
	for node in mconf.configured_nodes.values():
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


# display all variables for all nodes, or for one node in a fashion
# suitable for being used as "eval $(mon node show nodename)" from
# shell scripts
def cmd_show(args):
	if len(mconf.configured_nodes) == 0:
		print("No nodes configured")
		return
	if len(args) == 0:
		for (name, node) in mconf.configured_nodes.items():
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
	global wanted_names

	if len(args) == 0:
		print("Control without commands seems quite pointless. I'm going home")
		print("Try 'mon node help' for some assistance")
		sys.exit(1)
	nodes = {}
	cmd_args = []
	i = -1
	for arg in args:
		i += 1
		if arg == '--all':
			wanted_names = mconf.configured_nodes.keys()
			continue
		elif arg == '--':
			# double dashes means "here's the command"
			cmd_args = args[i + 1:]
			# if it's the first argument we got, the user wants
			# to run the command on all configured nodes
			if not i:
				wanted_names = mconf.configured_nodes.keys()
			break

		node = mconf.configured_nodes.get(arg, False)
		if node == False:
			cmd_args = args[i:]
			break
		wanted_names += [node.name]

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

	if not len(nodes) or not len(cmd_args):
		print("No nodes, or no commands to send. Aborting")
		sys.exit(1)

	cmd = ' '.join(cmd_args)
	for name, node in nodes.items():
		node.ctrl(cmd)

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
