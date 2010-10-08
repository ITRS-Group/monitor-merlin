import os, sys, re, time

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from compound_config import *
from merlin_apps_utils import *

class merlin_node:
	valid_types = ['poller', 'master', 'peer']

	def __init__(self, name, ntype = 'poller'):
		self.options = {'type': 'poller', 'port': 15551}
		self.set('name', name)
		self.set('type', ntype)
		self.set("address", name)
		self.hostgroup = []
		self.pushed_logs_dir = ''
		self.ssh_key = ''
		self.ssh_user = 'monitor'
		self.port = '15551'

		# the compound object has info we will need, so
		# prepare a storage area for it
		self.comp = False

	def verify(self):
		if not self.ntype in self.valid_types:
			print("'%s' is not a valid node type" % self.ntype)
			print("Any of %s would work" % "', '".join(self.valid_types))
			return False
		if not self.name:
			print("We lack a name. How the fuck is that possible?")
			return False
		if not self.address:
			print("No address given to node '%s'" % self.name)
			return False
		return True

	def write(self, f):
		oconf_vars = {}
		f.write("%s %s {\n" % (self.ntype, self.name))
		valid_vars = ['address', 'port']
		if self.ntype != 'master':
			valid_vars.append('hostgroup')

		for (k, v) in self.options.items():
			# we handle object_config variables first
			if k.startswith('oconf_'):
				short_var = k.split('_', 1)[1]
				oconf_vars[short_var] = v
				continue
			if not k in valid_vars:
				continue
			if type(v) == type([]):
				v = ','.join(v)

			# we store and print oconf variables specially
			f.write("\t%s = %s\n" % (k, v))

		if len(oconf_vars):
			f.write("\n\tobject_config {\n")
			for (k, v) in oconf_vars.items():
				f.write("\t\t%s = %s\n" % (k, v))
			f.write("\t}\n")

		f.write("}\n")


	def save(self):
		if not self.verify():
			print("Refusing to save a node that doesn't verify")
			return False
		self.write(sys.stdout)
		f = open(self.path, "a")
		self.write(f)
		# If we unconditionally close f, it gets a lot
		# harder to test and debug this thing.
		if f != sys.stdout:
			f.close()
		return True


	# set a variable for the object. Return True on success
	# and false on errors.
	def set_arg(self, arg, verbose=True):
		arg = arg.strip()
		if not '=' in arg:
			print("Arguments should be in the form key=value. %s doesn't work" % arg)
			return False
		(k, v) = arg.split('=', 1)
		self.set(k, v)

	def set(self, k, v):
		k = k.lower()
		if k == 'name':
			self.name = v
		elif k == 'type':
			self.ntype = v
		elif k == 'address':
			self.address = v
		elif k == 'port':
			self.port = v
		elif k == 'hostgroup':
			v = re.split("[\t ]*,[\t ]", v)
		elif not k.startswith('oconf_'):
			print("Unknown key in key=value pair: %s=%s" % (k, v))
			raise hell
			return False

		if k == 'hostgroup' and self.options.has_key(k):
			print(self.name, k, self.options[k], v)
			self.options[k] += v
		else:
			self.options[k] = v


	def rename(self, arg):
		name = self.name
		self.name = arg
		return os.rename(node_conf_dir + '/' + name, node_conf_dir + '/' + arg)


	def show(self):
		for (k, v) in self.options.items():
			k = k.upper()

			if type(v) == type([]):
				v = ','.join(v)

			print("%s=%s" % (k, v))


	def ctrl(self, args):
		if not self.ssh_user:
			self.ssh_user = 'root'
		prefix_args = ["ssh", self.ssh_user + "@" + self.address]
		if self.ssh_key:
			prefix_args += ['-i', self.ssh_key]
		all_args = prefix_args + [args]
		print("Connecting to '%s' with the following command:\n  %s"
			  % (self.name, ' '.join(all_args)))
		ret = os.spawnvp(os.P_WAIT, "ssh", all_args)
		if ret < 0:
			print("ssh was killed by signal %d" % ret)
			return False
		if ret != 0:
			print("ssh exited with return code %d" % ret)
			return False
		return True

node_conf_dir = "/etc/op5/distributed/nodes"
num_nodes = {'poller': 0, 'peer': 0, 'master': 0}
configured_nodes = {}
merlin_conf = '/opt/monitor/op5/merlin/merlin.conf'
wanted_types = merlin_node.valid_types
wanted_names = []
ntype = 'poller'
dbopt = {}

def module_init():
	global merlin_conf, wanted_types, wanted_names, configured_nodes
	global dbopt

	i = 2
	for arg in sys.argv[i:]:
		if arg.startswith('--merlin-cfg=') or arg.startswith('--config='):
			merlin_conf = arg.split('=', 1)[1]
		elif arg.startswith('--type='):
			wanted_types = arg.split('=')[1].split(',')
		elif arg.startswith('--name='):
			wanted_names = arg.split('=')[1].split(',')
		else:
			# not an argument we recognize, so ignore it
			i += 1
			continue

		popped = sys.argv.pop(i)
		i += 1

	# load the configured nodes
	conf = parse_conf(merlin_conf)
	for comp in conf.objects:
		comp.name = comp.name.strip()
		# grab the database settings. fugly, but quick
		if comp.name == 'daemon':
			for dobj in comp.objects:
				dobj.name.strip()
				if dobj.name != 'database':
					continue
				for k, v in dobj.params:
					dbopt[k] = v
			continue
		

		ary = re.split("[\t ]+", comp.name, 1)
		if len(ary) != 2 or not ary[0] in merlin_node.valid_types:
			continue
		node = merlin_node(ary[1], ary[0])
		node.comp = comp
		node.path = merlin_conf
		configured_nodes[node.name] = node
		for (k, v) in comp.params:
			node.set(k, v)
		for sc in comp.objects:
			if sc.name != 'object_config':
				continue
			for (sk, sv) in sc.params:
				node.set('oconf_' + sk, sv)


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
	global dbc, dbopt
	db_host = dbopt.get('host', 'localhost')
	db_name = dbopt.get('name', 'merlin')
	db_user = dbopt.get('user', 'merlin')
	db_pass = dbopt.get('pass', 'merlin')
	db_type = dbopt.get('type', 'mysql')

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

	for (name, info) in sinfo:
		print("")
		iid = info.pop('instance_id', 0)
		is_running = info.pop('is_running')
		name = "#%02d: %s" % (iid, name)
		name_len = len(name) + 9
		if is_running:
			name += " (%sACTIVE%s)" % (color.green, color.reset)
		else:
			name += " (%sINACTIVE%s)" % (color.red, color.reset)
			name_len += 2

		print("%s\n%s" % (name, '-' * name_len))
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
		if hchecks == 0 or (hchecks == host_checks and num_nodes > 1):
			hc_color = color.red
		if schecks == 0 or (schecks == service_checks and num_nodes > 1):
			sc_color = color.red
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
	for node in configured_nodes.values():
		if not node.ntype in wanted_types:
			continue
		print("  %s" % node.name)


# display all variables for all nodes, or for one node in a fashion
# suitable for being used as "eval $(mon node show nodename)" from
# shell scripts
def cmd_show(args):
	if len(configured_nodes) == 0:
		print("No nodes configured")
		return
	if len(args) == 0:
		for (name, node) in configured_nodes.items():
			if not node.ntype in wanted_types:
				continue
			print("\n# %s" % name)
			node.show()

	for arg in args:
		if not arg in configured_nodes.keys():
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			# scripts will list one node at a time. If the command
			# fails, they don't have to check the output to see if
			# the got anything sensible or not
			if len(args) == 1:
				sys.exit(1)
			continue
		if not configured_nodes[arg].ntype in wanted_types:
			continue
		if len(args) > 1:
			print("\n# %s" % arg)
		configured_nodes[arg].show()


def cmd_add(args):
	if len(args) < 1:
		return False
	name = args[0]
	if name in configured_nodes.keys():
		print("%s is already configured. Aborting" % name)
		return False

	node = merlin_node(name)
	node.path = merlin_conf

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
	if not name in configured_nodes.keys():
		print("%s isn't configured yet. Use 'add' to add it" % name)
		return False

	node = merlin_node(args[0])
	for arg in args[1:]:
		node.set(arg)

	return node.save()


def cmd_remove(args):
	if len(args) == 0:
		print("Which node do you want to remove? Try the 'list' command")
		return

	for arg in args:
		if not arg in configured_nodes:
			print("'%s' is not a configured node. Try the 'list' command" % arg)
			continue

		node = configured_nodes.pop(arg)
		sed_range = str(node.comp.line_start) + ',' + str(node.comp.line_end)
		cmd_args = ['sed', '-i', sed_range + 'd', merlin_conf]
		os.spawnvp(os.P_WAIT, 'sed', cmd_args)


def _cmd_rename(args):
	if len(args) != 2 or not args[0] in configured_nodes.keys():
		print("Which node do you want to rename? Try the 'list' command")
		return False

	node = configured_nodes[args[0]]
	dest = args[1]
	if dest in configured_nodes.keys():
		print("A node named '%s' already exists. Remove it first" % dest)
		return False
	configured_nodes.pop(args[0])
	configured_nodes[dest] = node
	node.rename(dest)
