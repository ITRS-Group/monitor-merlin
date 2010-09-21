#!/usr/bin/env python

import os, sys, posix, re, copy, random
import itertools

nagios_cfg = '/opt/monitor/etc/nagios.cfg'
object_cfg_files = {}
object_prefix = ''
hostgroups = []
hosts = []
nagios_objects = {}
obj_files = []

# These keeps track of which and how many objects we've
# written and must be wiped between each file we create
written = {}
num_written = 0
blocked_writes = 0

class compound_object:
	name = ''
	parent = False

	def __init__(self, name = '', parent = False):
		self.name = name
		self.parent = parent
		self.params = []
		self.objects = []

	def add(self, key, value):
		# we add values as tuples in a list object so we
		# don't run into collisions with multiple keys
		# with the same name
		self.params.append((key, value))

	def close(self):
		if self.parent:
			return self.parent
		return False

def parse_conf(path):
	cur = compound_object(path)
	pushed_objs = []

	lnum = 0
	f = open(path)
	for line in f:
		lnum += 1
		line = line.strip()
		if len(line) == 0 or line[0] == '#':
			continue

		if line[0] == '}' and cur.parent:
			cur = cur.close()
			continue

		if line[-1] == '{':
			pushed_objs.insert(0, cur)
			n = compound_object(line[:-1].strip(), cur)
			cur.objects.append(n)
			cur = n
			continue

		kv = line.split('=')
		if len(kv) != 2:
			continue
		(key, value) = kv
		key = key.rstrip()
		value = value.lstrip().rstrip(';')
		cur.add(key, value)

	return cur

def write_conf(opt, nesting = 0):
	if nesting:
		print("\n" + (' ' * (nesting - 4)) + opt.name + " {")
	for (key, value) in opt.params:
		sys.stdout.write(' ' * nesting)
		print(key + " = " + value)

	# iterate by recursion
	for obj in opt.objects:
		write_conf(obj, nesting + 4)

	if nesting:
		print((' ' * (nesting - 4)) + "}")


# what's up with the lack of macros and functions to test if something's
# a file or a directory??
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
		if ary[0] in ['alias', 'timeperiod_name', 'register']:
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
	#	for obj in self.members.values():
	#		obj.write_linked(f)

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
		'contactgroups': 'Mcontactgroup',
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

ncfg_path = '/opt/monitor/etc/nagios.cfg'
if __name__ == '__main__':
	for arg in sys.argv[1:]:
		if arg.startswith('--nagios-cfg='):
			ncfg_path = arg[len("--nagios-cfg="):]
			continue

	obj_files = grab_object_cfg_files(ncfg_path)
	for f in obj_files:
		parse_nagios_objects(f)

# Most things can be parsed in any random order, really.
# The respective master objects and their groups must be
# parsed before their slaves though (hosts must be parsed
# before services, and services must be parsed before
# serviceescalations etc)
parse_order = [
	'timeperiod', 'command', 'contact', 'contactgroup',
	'host', 'hostgroup', 'service', 'servicegroup',
	'hostextinfo', 'hostescalation', 'hostdependency',
	'serviceextinfo', 'serviceescalation', 'servicedependency',
	]
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

def create_otype_dict(default):
	global parse_order
	d = {}
	for otype in parse_order:
		d[otype] = default
		d[otype + '_template'] = default
	return d

def write_hg_list(path, hg_list):
	global written, blocked_writes, num_written
	written = create_otype_dict({})
	num_written = 0
	blocked_writes = 0

	f = open(path, "w")
	include_all = ['contact', 'contactgroup']
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
outparams = [
	{'file': 'p1', 'hostgroups': ['p1_hosts']},
	{'file': 'p2', 'hostgroups': ['p2_hosts']},
	{'file': 'p3', 'hostgroups': ['p3_hosts', 'p2_hosts']},
	]
def run_param(param):
	interesting['hostgroup'] = set(param['hostgroups'])
	interesting['host'] = set()
	for shg in interesting['hostgroup']:
		hg = nagios_objects['hostgroup'].get(shg)
		if not hg:
			print("Hostgroup '%s' doesn't exist, you retard" % shg)
			sys.exit(1)

		interesting['host'] |= set(interesting['host']) | set(hg.members.keys())

	write_hg_list(param['file'], interesting['hostgroup'])

#for param in outparams:
#	run_param(param)

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
