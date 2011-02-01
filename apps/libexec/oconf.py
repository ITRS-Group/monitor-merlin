#!/usr/bin/env python

import os, sys, posix, re, copy, random
import itertools

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from compound_config import *
from object_importer import ObjectImporter
from merlin_apps_utils import *
import merlin_conf as mconf

# when whatever config file we're reading was last changed
last_changed = 0
nagios_cfg = '/opt/monitor/etc/nagios.cfg'
object_cfg_files = {}
object_prefix = ''
object_cache = '/opt/monitor/var/objects.cache'
hostgroups = []
hosts = []
nagios_objects = {}
obj_files = []

# These keeps track of which and how many objects we've
# written and must be wiped between each file we create
written = {}
num_written = 0
blocked_writes = 0
parse_order = [
	'timeperiod', 'command', 'contact', 'contactgroup',
	'host', 'hostgroup', 'service', 'servicegroup',
	'hostextinfo', 'hostescalation', 'hostdependency',
	'serviceextinfo', 'serviceescalation', 'servicedependency',
	]

# grab object configuration files from a cfg_dir directive
def recurse_grab_object_cfg_files(v):
	obj_files = []
	f_ary = os.listdir(v)
	for f in f_ary:
		path = v + '/' + f
		if os.path.isdir(path):
			obj_files += recurse_grab_object_cfg_files(path)
		elif os.path.isfile(path) and f[-4:] == '.cfg':
			obj_files.append(path)

	return obj_files

def grab_object_cfg_files(nagios_cfg_path):
	obj_files = []
	comp = parse_conf(nagios_cfg_path)
	for (k, v) in comp.params:
		if k == 'cfg_file':
			obj_files.append(v)
		elif k == 'cfg_dir':
			obj_files += recurse_grab_object_cfg_files(v)

	return obj_files


class nagios_object:
	otype = ''
	slave_keys = {}
	preserve = {}

	def __init__(self, otype = ''):
		if otype != '':
			self.otype = otype
		self.name = ''
		self.obj = {}
		self.members = {}
		self.groups = {}
		self.slaves = {}

	# string to list conversion for objects (we do this a lot)
	def s2l(self, s):
		if not s:
			return []

		return re.split('[\t ]*,[\t ]*', s)

	def add(self, k, v):
		if self.obj.has_key(k):
			self.obj[k] += ',' + v
		else:
			self.obj[k] = v

	def close(self):
		tname = self.obj.get('name')
		if tname:
			ttype = self.otype + '_template'
			if not nagios_objects.has_key(ttype):
				nagios_objects[ttype] = {}

			nagios_objects[ttype][tname] = self

		if self.obj.has_key(self.otype + '_name'):
			self.name = self.obj[self.otype + '_name']
		elif self.obj.has_key('name'):
			self.name = self.obj['name']
		else:
			self.name = str(len(nagios_objects[self.otype]))

	def raw_write(self, f):
		global written, blocked_writes, num_written

		# write it only once
		if written[self.otype].get(self.name):
			blocked_writes += 1
			return True
		written[self.otype][self.name] = True
		num_written += 1
		f.write("define %s {\n" % self.otype.replace('_template', ''))
		for (k, v) in self.obj.items():
			f.write("%s%-30s %s\n" % (' ' * 4, k, v))
		f.write("}\n")

	def write(self, f):
		pobj = {}
		for (k, l) in self.preserve.items():
			v = self.obj.get(k)
			if not v:
				continue
			pobj[k] = v
			ok_list = set(self.s2l(v)) & interesting[l]
			if not len(ok_list):
				self.obj.pop(k)
			else:
				self.obj[k] = ','.join(ok_list)

		self.raw_write(f)
		for (k, v) in pobj.items():
			self.obj[k] = v

	def raw_write_linked(self, f):
		self.write_template(f)
		self.write(f)
		for (key, ltype) in self.slave_keys.items():
			val = self.obj.get(key)
			if not val:
				continue
			if ltype[0] == 'M':
				ltype = ltype[1:]
				ary = self.s2l(val)
				for tobj in [nagios_objects[ltype][t] for t in ary]:
					tobj.write_linked(f)
			else:
				# commands link to nothing, so might as well use .write(f)
				if ltype == 'command':
					cmd = val.split('!')[0]
					nagios_objects[ltype][cmd].write(f)
				else:
					nagios_objects[ltype][val].write_linked(f)

		for (otype, olist) in self.slaves.items():
			for obj in olist.values():
				obj.write_linked(f)

	def write_linked(self, f):
		self.raw_write_linked(f)

	# splits a comma-separated list into 'include' and 'exclude'
	def incex(self, key):
		s = self.obj.get(key)
		if not s:
			return (set(), set())

		include = set()
		exclude = set()
		ary = self.s2l(s)
		for name in ary:
			if name[0] == '!':
				exclude.add(name[1:])
			else:
				include.add(name)

		return (include, exclude)


	def incex_members(self, ltype, key):
		lgroups = nagios_objects.get(ltype)
		if not lgroups:
			return (set(), set())

		include = set()
		exclude = set()
		(inc, exc) = self.incex(key)
		for oname in inc:
			group_obj = lgroups.get(oname)
			if not group_obj:
				return False
			for m in group_obj.members.values():
				include.add(m.name)
		for oname in exc:
			group_obj = lgroups.get(oname, [])
			if not group_obj:
				return False
			for m in group_obj.members.values():
				exclude.add(m.name)
		return (include, exclude)

	# exclude everything from exc that is also in inc
	# Python's set arithmetic makes this ridiculously easy
	def incex2inc(self, ltype, inc, exc):
		if '*' in inc:
			if '*' in exc:
				return set()

			inc = set(nagios_objects[ltype].keys())

		if '*' in exc:
			exc = set(nagios_objects[ltype].keys())
		return inc - exc


	def list2objs(self, ltype, list):
		olist = nagios_objects.get(ltype)
		if not olist:
			return False

		objs = {}
		for name in list:
			objs[name] = nagios_objects[ltype][name]
		return objs

	# minimal and stupid support for nested templates
	def write_template(self, f):
		tname = self.obj.get('use')
		if not tname:
			return
		if self.otype.endswith('_template'):
			ttype = self.otype
		else:
			ttype = self.otype + '_template'

		tmplt = nagios_objects[ttype].get(tname)
		if tmplt:
			tmplt.write_linked(f)

	def parse_line(self, line):
		ary = re.split('[\t ]*', line, 1)
		return (ary[0], ary[1])

	def parse(self):
		return True


class nagios_timeperiod(nagios_object):
	otype = 'timeperiod'
	slave_keys = {
		'exclude': 'Mtimeperiod',
    }

	# checks for strings in the format HH:MM-HH:MM
	def is_timedef(self, s):
		times = s.split('-')
		if len(times) != 2:
			return False
		for tdef in times:
			hhmm = tdef.split(':')
			if len(hhmm) != 2:
				return False
			if not hhmm[0].isdigit() or not hhmm[1].isdigit():
				return False
			if int(hhmm[0]) > 24 or int(hhmm[0]) < 0:
				return False
			if int(hhmm[1]) > 59 or int(hhmm[1]) < 0:
				return False

		return True

	# handle weirdo timeperiods smarter here
	def parse_line(self, line):
		ary = re.split('[\t ,]*', line, 1)
		if ary[0] in ['alias', 'timeperiod_name', 'register', 'exclude']:
			return (ary[0], ary[1])

		ary = re.split('[\t ,]*', line)
		if len(ary) == 2:
			return (ary[0], ary[1])

		# tricksy case left. We solve it by locating the first
		# timedef in the string and concatenating the entries
		# before it in the key and the entries after it in the
		# value. It's rather inefficient, but extremely robust
		value = ''
		for i in xrange(0, len(ary)):
			if self.is_timedef(ary[i]):
				key = ' '.join(ary[:i])
				value = ','.join(ary[i:])
				return (key, value)

		return False


class nagios_group(nagios_object):
	def add_member(self, member):
		self.members[member.name] = member

	def write_linked(self, f):
		self.write(f)
		if self.otype == 'contactgroup':
			for obj in self.members.values():
				obj.write_linked(f)

	# TODO: Support groups-in-groups
	def parse(self):
		incex = self.incex('members')
		inc = self.incex2inc(self.otype[:-5], incex[0], incex[1])
		members = self.list2objs(self.otype[:-5], inc)
		if not members:
			return True
		# TODO: what the hell should we do now that we have
		#       the members parsed out?
		self.obj['members'] = ','.join(inc)
		for m in members.values():
			m.add_group(self)
			self.members[m.name] = m

class nagios_servicegroup(nagios_group):
	def parse(self):
		members = self.obj.get('members')
		if not members:
			return True
		ary = self.s2l(members)
		if not ary:
			return
		i = 0
		while i < len(ary):
			name = ary[i] + ';' + ary[i + 1]
			i += 2
			service = nagios_objects['service'][name]
			self.members[service.name] = service

class nagios_group_member(nagios_object):
	def add_group(self, group):
		self.groups[group.name] = group

	def add_to_groups(self):
		gtype = self.otype + 'group'
		gvar = gtype + 's'
		groups = self.obj.get(gvar, False)
		if not groups:
			return True
		for gname in self.s2l(groups):
			gobj = nagios_objects[gtype].get(gname)
			# illegal group
			if not gobj:
				print("%s %s not found" % (gtype, gname))
				return False
			gobj.add_member(self)

	def close(self):
		if self.obj.has_key('name'):
			self.otype = self.otype + '_template'
			self.name = self.obj['name']
		else:
			self.name = self.obj[self.otype + '_name']

	def parse(self):
		self.add_to_groups()

class nagios_contact(nagios_group_member):
	otype = 'contact'
	slave_keys = {
#		'contactgroups': 'Mcontactgroup',
		'host_notification_period': 'timeperiod',
		'service_notification_period': 'timeperiod',
		'host_notification_commands': 'command',
		'service_notification_commands': 'command',
		}

class nagios_slave_object(nagios_object):
	def link_to_master(self):
		if nagios_objects[self.master_type].has_key(self.name):
			master = nagios_objects[self.master_type]
			master.slaves[self.otype].append(self)
			return True
		return False

	def member_dict(self, ltype, s):
		members = {}
		ary = re.split('[\t ]*,[\t ]', s)
		group_objs = [nagios_objects[ltype][m] for m in ary]
		for group in group_objs:
			for mobj in group.members.values():
				members[mobj.name] = mobj

		return members

	def obj_dict(self, ltype, s):
		ary = re.split('[\t ]*,[\t ]', s)
		objs = [nagios_objects[ltype][o] for o in ary]
		return objs

class nagios_servicedependency(nagios_object):
	otype = 'servicedependency'


class nagios_host(nagios_group_member):
	otype = 'host'
	slave_keys = {
		'notification_period': 'timeperiod',
		'check_period': 'timeperiod',
		'check_command': 'command',
		# we ignore the parents key here, since all interesting
		# hosts will be printed anyway
		#'parents': 'host',
		'contact_groups': 'Mcontactgroup',
		'contacts': 'Mcontact',
		}
	preserve = {'hostgroups': 'hostgroup', 'parents': 'host'}

	def parse(self):
		self.add_to_groups()


class nagios_service(nagios_slave_object, nagios_group_member):
	otype = 'service'
	slave_keys = {
		'check_command': 'command',
		'event_handler': 'command',
		'check_period': 'timeperiod',
		'notification_period': 'timeperiod',
		'contact_groups': 'Mcontactgroup',
		'contacts': 'Mcontact',
	}

	def close(self):
		if self.obj.has_key('name'):
			self.otype = 'service_template'
			self.name = self.obj['name']
			return True

		if not self.obj.has_key('host_name') or not self.obj.has_key('service_description'):
			return False
		self.name = self.obj['host_name'] + ';' + self.obj['service_description']
		return True

	def parse(self):
		hie = self.incex('host_name')
		gie = self.incex_members('hostgroup', 'hostgroup_name')
		inc = self.incex2inc('host', hie[0] | gie[0], hie[1] | gie[1])
		# remove hostgroup references and replace with host refs
		self.obj.pop('hostgroup_name', False)
		self.obj.pop('host_name', False)
		self.obj['host_name'] = ','.join(inc)

		if not len(inc):
			return False
		hosts = self.list2objs('host', inc)
		for h in hosts.values():
			if not h.slaves.has_key(self.otype):
				h.slaves[self.otype] = {}

			# must use copy.deepcopy() here
			h.slaves[self.otype][self.obj['service_description']] = copy.deepcopy(self)

def parse_nagios_objects(path):
	global last_changed

	st = os.stat(path)
	if st and st.st_mtime > last_changed:
		last_changed = st.st_mtime

	f = open(path)
	obj = False
	otype = ''
	for line in f:
		line = line.strip()
		if line == '' or line[0] == '#':
			continue

		if line[-1] == '{':
			otype = line[:-1].strip().split(" ")[-1]
			if not otype in nagios_objects.keys():
				nagios_objects[otype] = {}
			if otype == 'host':
				obj = nagios_host()
			elif otype == 'service':
				obj = nagios_service()
			elif otype == 'contact':
				obj = nagios_contact(otype)
			elif otype == 'servicegroup':
				obj = nagios_servicegroup(otype)
			elif otype.endswith('group'):
				obj = nagios_group(otype)
			elif otype.startswith('host'):
				obj = nagios_slave_object(otype)
			elif otype.startswith('service'):
				obj = nagios_slave_object(otype)
			elif otype == 'timeperiod':
				obj = nagios_timeperiod(otype)
			else:
				# command and contact objects end up here
				obj = nagios_object(otype)
			continue

		if line[0] == '}':
			if obj == False:
				# this should never happen
				continue
			obj.close()
			# this is for template objects
			if not obj.otype in nagios_objects.keys():
				nagios_objects[obj.otype] = {}
			nagios_objects[obj.otype][obj.name] = obj
			continue

		(key, value) = obj.parse_line(line)
		obj.add(key, value)

# Most things can be parsed in any random order, really.
# The respective master objects and their groups must be
# parsed before their slaves though (hosts must be parsed
# before services, and services must be parsed before
# serviceescalations etc)
def post_parse():
	for otype in parse_order:
		olist = nagios_objects.get(otype)
		if not olist:
			continue
		for (oname, obj) in olist.items():
			obj.parse()

def write_hostgroup(f, hg_name):
	hg = nagios_objects['hostgroup'].get(hg_name)
	if not hg:
		print("Hostgroup '%s' doesn't exist" % hg_name)
		return False

	hg.write(f)
	for host in hg.members.values():
		host.write_linked(f)

def create_otype_dict():
	global parse_order

	d = {}
	for otype in parse_order:
		d[otype] = {}
		d[otype + '_template'] = {}
	return d

def write_hg_list(path, hg_list):
	global written, blocked_writes, num_written
	written = create_otype_dict()
	num_written = 0
	blocked_writes = 0

	f = open(path, "w")
	include_all = []
	for otype in include_all:
		olist = nagios_objects.get(otype)
		if not olist:
			continue
		for o in olist.values():
			o.write_linked(f)

	for hg_name in hg_list:
		write_hostgroup(f, hg_name)
	print("%s created with %d objects for hostgroup list '%s'" %
		(path, num_written, ','.join(hg_list)))
	f.close()
	os.utime(path, (last_changed, last_changed))

def hg_permute(li):
	for i in range(len(li)):
		for p in itertools.combinations(li, len(li) - i):
			yield(p)

# since we'll be doing 2**x runs through the hostgroups and their
# associated objects, we really don't want to run every combination
# of all hostgroups. For those who have 200-ish of the little
# buggers, we'd be looking at a runtime counted in years. Instead,
# we grab a random selection of 16 hostgroups to calculate for
# We do it rather inefficiently, but we really don't care since
# it's a one-off only used for testing
def hg_pregen(li):
	perm_const = 11

	if len(li) < perm_const:
		return li
	psel = {}
	while len(psel) < perm_const:
		r = random.choice(li)
		psel[r] = nagios_objects['hostgroup'][r]
	return psel.keys()

interesting = {}
def run_param(param):
	global nagios_objects, interesting
	interesting['hostgroup'] = set(param['hostgroups'])
	interesting['host'] = set()
	for shg in interesting['hostgroup']:
		hg = nagios_objects['hostgroup'].get(shg)
		if not hg:
			print("Hostgroup '%s' doesn't exist" % shg)
			sys.exit(1)

		interesting['host'] |= set(interesting['host']) | set(hg.members.keys())

	write_hg_list(param['file'], interesting['hostgroup'])

def usage(msg = False):
	if msg:
		print(msg)

	print("""
usage: mon oconf <command> [options]

Command overview
----------------
 split <outfile:hostgroup1,hostgroup2,hostgroupN>
   write config for hostgroup1,hostgroup2 and hostgroupN into outfile

 nodesplit
   same as above, but use merlin's config to split config

 hash
   print sha1 hash of running configuration

 changed
   print last modification time of all files

 files
   print the configuration files in alphabetical order

 hglist
   print a sorted list of all configured hostgroups

 push
   Split configuration based on merlin's peer and poller configuration
   and send object configuration to all peers and pollers, restarting
   those that receive a configuration update. ssh keys need to be set
   up for this to be usable without admin supervision.
""")
	sys.exit(1)

def cmd_help(args):
	usage()

def oconf_helper(args):
	app = os.path.dirname(__file__) + '/-oconf'
	ret = os.spawnv(os.P_WAIT, app, [app] + args)
	if ret < 0:
		print("Helper %s was killed by signal %d" (app, ret))

def cmd_hash(args):
	oconf_helper(['hash'] + args)

def cmd_changed(args):
	oconf_helper(['last-changed'] + args)

def cmd_files(args):
	sob_files = sorted(grab_object_cfg_files(nagios_cfg))
	for cfile in sob_files:
		print(cfile)

def cmd_split(args):
	if len(args) == 0:
		usage("'split' requires arguments")

	argparams = []

	for arg in args:
		# Ignore global arguments
		if arg.startswith('--'):
			continue

		# default case. outfile:hg1,hg2,hgN... argument
		ary = arg.split(':')
		if len(ary) != 2:
			usage("Unknown argument: %s" % arg)

		hgs = re.split('[\t ]*,[\t ]*', ary[1])
		argparams.append({'file': ary[0], 'hostgroups': hgs})

	if not len(argparams):
		return

	parse_object_config([object_cache])

	for param in argparams:
		run_param(param)

cache_dir = '/var/cache/merlin'
config_dir = cache_dir + '/config'
def cmd_nodesplit(args):
	global cache_dir, config_dir

	wanted_nodes = {}
	force = False
	for arg in args:
		if arg == '--force':
			force = True
		if arg.startswith('--cache-dir='):
			config_dir = arg.split('=', 1)[1]
		# check if it's a poller node
		node = mconf.configured_nodes.get(arg)
		if node and node.ntype == 'poller':
			wanted_nodes[node.name] = node

	if not mconf.num_nodes['poller']:
		print("No pollers configured. No need to split config.")
		return True

	if not wanted_nodes:
		wanted_nodes = mconf.configured_nodes

	config_dir = cache_dir + '/config'
	mkdir_p(config_dir)

	# now that the potentially failing calls have been made, we
	# parse the object configuration from the objects.cache file
	# or, if that file doesn't exist, from the regular object
	# config. Either way, this must come before the loop as we
	# need the last_changed variable from here in order to know
	# which nodes we can avoid pushing to
	if os.path.isfile(object_cache):
		parse_object_config([object_cache])
	else:
		parse_object_config()

	params = []
	for name, node in mconf.configured_nodes.items():
		if node.ntype != 'poller':
			continue
		hostgroups = node.options.get('hostgroup', False)
		if not hostgroups:
			print("%s is a poller without hostgroups assigned to it." % name)
			print("Fix your config, please")
			sys.exit(1)

		node.oconf_file = '%s/%s.cfg' % (config_dir, name)
		# if there is a cached config file which is the same age
		# as the object config and we're not being forced, there's
		# no need to re-create it
		if os.access(node.oconf_file, os.R_OK):
			st = os.stat(node.oconf_file)
			if not force and st.st_mtime == last_changed:
				print("%s is cached" % (node.oconf_file))
				continue

		params.append({'file': node.oconf_file, 'hostgroups': hostgroups})

	# If there are no pollers with hostgroups, we might as well
	# go home.
	if not len(params):
		return

	# make sure files are created with the proper mode
	# for the target system
	old_umask = os.umask(002)
	map(run_param, params)
	os.umask(old_umask)


def get_ssh_key(node):
	ssh_key = node.options.get('oconf_ssh_key', False)
	if ssh_key and os.path.isfile(ssh_key):
		return ssh_key
	home = os.getenv('HOME', False)
	if not home:
		return False
	sshdir = home + "/.ssh"
	if not os.path.isdir(sshdir):
		return False

	# Try various keyfiles in the preferred order.
	# If we find one, we simply return 'true', since
	# ssh will look for the keys there too and may
	# choose one with better encryption (or whatever).
	for keyfile in ['id_rsa', 'id_dsa', 'identity']:
		if os.path.isfile(sshdir + '/' + keyfile):
			return True

	# no key seems to exist
	return False


def cmd_push(args):
	errors = 0
	cmd_nodesplit(args)
	restart_nodes = {}
	restart = True

	for arg in args:
		if arg == '--no-restart':
			restart = False

	for name, node in mconf.configured_nodes.items():
		# we don't push to master nodes
		if node.ntype == 'master':
			continue

		# Copy recursively in 'archive' mode
		rsync_args = ['rsync', '-aotz', '--delete']
		rsync_args += ['-b', '--backup-dir=/var/cache/merlin/backups']

		# Use compression by default
		ssh_cmd = 'ssh -C'

		if not node.oconf_file and node.ntype == 'poller':
			continue

		# now we set up source and destination. Pollers and peers
		# have different demands for this, and peers can be
		# configured to either transport only object configuration
		# (put it in a directory on its own and ship only objects)
		# or everything (ship /opt/monitor/etc to /opt/monitor)
		if node.ntype == 'poller':
			# pollers without an oconf_file are ignored
			if not node.oconf_file:
				continue
			source = node.oconf_file
			default_dest = '/opt/monitor/etc/oconf/from-master.cfg'
		else:
			source = node.options.get('oconf_source', '/opt/monitor/etc')
			default_dest = '/opt/monitor'

		oconf_dest = node.options.get('oconf_dest', default_dest)
		ssh_user = node.options.get('oconf_ssh_user', 'root')
		ssh_key = get_ssh_key(node)
		if ssh_key and ssh_key != True:
			if not os.path.isfile(ssh_key):
				print("ssh key '%s' for node '%s' not found" % (ssh_key, name))
				print("We can't push config without keys being properly set up")
				continue
			ssh_cmd += ' -i ' + ssh_key

		# if we're not running from console, we need to disable
		# keyboard-interactive authentication to avoid hanging
		if not os.isatty(sys.stdout.fileno()):
			ssh_cmd += ' -o KbdInteractiveAuthentication=no'

		ssh_cmd += ' -l ' + ssh_user
		user_address_dest = "%s@%s:%s" % (ssh_user, node.address, oconf_dest)
		address_dest = node.address + ':' + oconf_dest
		rsync_args += [source, '-e', ssh_cmd, address_dest]
#		scp_args += [source, user_address_dest]
		ret = os.spawnvp(os.P_WAIT, 'rsync', rsync_args)
		if ret != 0:
			print("rsync returned %d. Breakage?" % ret)
			print("Won't restart monitor and merlin on node '%s'" % name)
			errors += 1
			continue

		restart_nodes[name] = node

	# we restart all nodes after having pushed configuration to all
	# of them, or we might trigger an avalanche of config pushes
	# that trigger and re-trigger each other.
	for name, node in restart_nodes.items():
		if restart and not node.ctrl("mon restart"):
			print("Restart failed for node '%s'" % name)
			errors += 1

	# splitting, pushing and restarting is done. If there were
	# errors, we exit with non-zero exit status
	if errors:
		sys.exit(1)

def cmd_hglist(args):
	parse_object_config([object_cache])
	for k in sorted(nagios_objects['hostgroup'].keys()):
		print("  %s" % k)

def import_usage():
	print """
Usage: mon oconf import <options>

--db-name    name of database to import to
--db-user    database username
--db-host    database host
--db-pass    database password
--db-type    database type (mysql is the only supported for now)
--cache      path to the objects.cache file to import
--status-log path to the status.log file to import
--nagios-cfg path to nagios' main configuration file
--help       print this text and exit
"""

#def cmd_import(args):
#	db_type = 'mysql'
#	db_host = 'localhost'
#	db_user = 'merlin'
#	db_pass = 'merlin'
#	db_name = 'merlin'
#	cache = None
#	status_log = None
#	nagios_cfg = None
#	dry_run = False
#	verbose = False
#
#	i = 0;
#	nargs = len(args)
#	try:
#		while i < nargs:
#			if args[i].startswith('--db-name'):
#				db_name = args[i][len('--db-name='):]
#			elif args[i].startswith('--db-user'):
#				db_user = args[i][len('--db-user='):]
#			elif args[i].startswith('--db-host'):
#				db_host = args[i][len('--db-host='):]
#			elif args[i].startswith('--db-pass'):
#				db_pass = args[i][len('--db-pass='):]
#			elif args[i].startswith('--db-type'):
#				db_type = args[i][len('--db-type='):]
#			elif args[i].startswith('--cache'):
#				cache = args[i][len('--cache='):]
#			elif args[i].startswith('--status-log'):
#				status_log = args[i][len('--status-log='):]
#			elif args[i].startswith('--nagios-cfg'):
#				nagios_cfg = args[i][len('--nagios-cfg='):]
#			elif args[i] == '--dry-run':
#				dry_run = True
#			elif args[i] == '--verbose':
#				verbose = True
#			elif args[i] == '--help':
#				import_usage()
#				return
#			else:
#				print "Error: Unknown argument '%s'" % args[i]
#				import_usage()
#				return
#			i += 1
#	except IndexError:
#		print "Error: option '%s' requires an argument" % args[i][2:]
#		import_usage()
#		return
#
#	if not (nagios_cfg or cache or status_log):
#		nagios_cfg = '/opt/monitor/etc/nagios.cfg'
#
#	oi = ObjectImporter(db_type, db_host, db_name, db_user, db_pass)
#
#	if nagios_cfg:
#		try:
#			config = parse_conf(nagios_cfg)
#			if config:
#				if not cache and config['object_cache_file']:
#					cache = config['object_cache_file']
#				if not status_log:
#					if config['status_file']:
#						status_log = config['status_file']
#					elif config['xsddefault_status_file']:
#						status_log = config['xsddefault_status_file']
#		except IOError:
#			print "Error: Couldn't open file '%s'." % (nagios_cfg),
#			if not cache or not status_log:
#				print "Not using config to find cache or status_log."
#			else:
#				print '\n'
#	
#	oi.prepare_import()
#
#	if not cache and not status_log:
#		print 'Neither --cache nor --status-log given.\nImporting nothing'
#		import_usage()
#		return
#
#	
#	if not dry_run:
#		print 'Disabling indexes'
#		oi.disable_indexes()
#
#	print 'Importing objects to database ' + db_name
#	if cache:
#		print 'Importing objects from ' + cache
#		if not dry_run:
#			oi.import_objects_from_cache(cache, True, verbose)
#	
#	if status_log:
#		print 'Importing status from ' + status_log
#		if not dry_run:
#			oi.import_objects_from_cache(status_log, False, verbose)
#
#	if not dry_run:
#		oi.commit_objects()
#	
#	if not dry_run:
#		print 'Enabling indexes'
#		oi.enable_indexes()
#		oi.finalize_import()

def _cmd_t_randomize(args):
	global nagios_objects, interesting
	parse_object_config()
	i = 0
	hostgroup_list = hg_pregen(nagios_objects['hostgroup'].keys())
	num_hgs = len(hostgroup_list)
	gen_confs = (2 ** num_hgs - 1)
	print("Generating %d configurations" % gen_confs)
	if gen_confs > 100:
		print("This will take a while")
	for p in hg_permute(hostgroup_list):
		param = {'file': 'output/%d' % i, 'hostgroups': p}
		run_param(param)
		i += 1

def parse_object_config(files = False):
	if not files:
		files = grab_object_cfg_files(nagios_cfg)

	map(parse_nagios_objects, files)
	post_parse()

def module_init(args):
	global nagios_cfg, object_cache

	# arguments viable for all our commands are parsed here
	for arg in args:
		if arg.startswith('--nagios-cfg='):
			nagios_cfg = arg.split('=', 1)[1]
		elif arg.startswith('--merlin-cfg='):
			mconf.config_file = arg.split('=', 1)[1]
		elif arg.startswith('--object-cache='):
			object_cache = arg.split('=', 1)[1]
			if not len(object_cache):
				object_cache = False

	mconf.parse()
