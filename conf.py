#!/usr/bin/env python

import os, sys, posix, re, copy

nagios_cfg = '/opt/monitor/etc/nagios.cfg'
object_cfg_files = {}
object_prefix = ''
hostgroups = []
hosts = []
nagios_objects = {}
obj_files = []
written = 0
allowed_writes = 1
interesting_objects = {}
interesting_hosts = {}

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

	def __init__(self, otype = ''):
		if otype != '':
			self.otype = otype
		self.name = ''
		self.obj = {}
		self.members = {}
		self.slaves = {}
		self.written = 0

	# string to list conversion for objects (we do this a lot)
	def s2l(self, s):
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

	def write(self, f):
		global written

		# write it only once
		if self.written >= allowed_writes:
			return True
		self.written += 1
		written += 1
		f.write("define %s {\n" % self.otype.replace('_template', ''))
		for (k, v) in self.obj.items():
			f.write("%s%-30s %s\n" % (' ' * 4, k, v))
		f.write("}\n")

	def write_linked(self, f):
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
		ttype = self.otype if '_' in self.otype else self.otype + '_template'
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
		print(members)
		# TODO: what the hell should we do now that we have
		#       the members parsed out?

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
	def add_to_groups(self):
		gtype = self.otype + 'group'
		gvar = gtype + 's'
		groups = self.obj.pop(gvar, False)
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

	def parse(self):
		self.add_to_groups()

class nagios_service(nagios_slave_object, nagios_group_member):
	otype = 'service'

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
		if not len(inc):
			return False
		hosts = self.list2objs('host', inc)
		for h in hosts.values():
			if not h.slaves.has_key(self.otype):
				h.slaves[self.otype] = {}

			h.slaves[self.otype][self.obj['service_description']] = copy.deepcopy(self)

	def write_linked(self, f):
		self.write_template(f)
		self.write(f)

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

outparams = [
#	{'file': 'p1', 'hostgroups': ['p1_hosts']},
#	{'file': 'p2', 'hostgroups': ['p2_hosts']},
	{'file': 'p3', 'hostgroups': ['p3_hosts']},
	]

fail = False
for hg in nagios_objects['hostgroup'].values():
	f = open("output/" + hg.name, "w")
	hg.write(f)
	for host in hg.members.values():
		host.write_linked(f)
	allowed_writes += 1
	f.close()

sys.exit(0)

for param in outparams:
	interesting_hgs = param['hostgroups']
	for shg in param['hostgroups']:
		hg = nagios_objects['hostgroup'].get(shg)
		if not hg:
			print("Hostgroup '%s' doesn't exist, you retard" % shg)
			fail = True
			continue
		f = open(param['file'], "w")
		hg.write(f)
		for host in hg.members.values():
			host.write_linked(f)
		allowed_writes += 1

if fail:
	print("Hostgroups: ")
	for hg_name in nagios_objects['hostgroup']:
		print(hg_name)
