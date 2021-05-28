import os

class compound_object:
	def __init__(self, name = '', parent = False):
		# we stash start and end line so our caller can remove
		# compounds using sed without affecting config comments
		self.line_start = 0
		self.line_end = 0

		self.name = name
		self.parent = parent
		self.params = []
		self.objects = []

		# optionally set by caller for toplevel compounds (files)
		self.path = False

	def add(self, key, value):
		# we add values as tuples in a list object so we
		# don't run into collisions with multiple keys
		# with the same name
		self.params.append((key, value))

	def close(self):
		if self.parent:
			return self.parent
		return False

	def __getitem__(self, key):
		for p in self.params:
			if p[0] == key:
				return p[1]


def count_compound_types(path):
	"""Counts object types in compound config files"""
	if not path:
		return False

	ocount = dict()

	f = open(path, "r")
	for line in f:
		name = False
		line = line.strip()
		if not len(line) or line[-1] != '{':
			continue
		if line.startswith('define'):
			name = line.split(' ', 2)[1]
		else:
			name = line.split(' ', 1)[0]

		if ocount.get(name, False) == False:
			ocount[name] = 0
		ocount[name] += 1

	f.close()

	return ocount


def parse_conf(path, splitchar='='):
	cur = compound_object(path)
	cur.path = path
	pushed_objs = []

	lnum = 0
	f = open(path)
	for line in f:
		lnum += 1
		line = line.strip()
		# this barfs on latin1 characters, but those aren't handled properly by
		# merlin anyway.
		line = line.decode('latin1')
		if not line or line[0] == '#':
			continue

		if line[0] == '}' and cur.parent:
			cur.line_end = lnum
			cur = cur.close()
			continue

		if line[-1] == '{':
			pushed_objs.insert(0, cur)
			n = compound_object(line[:-1].strip(), cur)
			cur.objects.append(n)
			cur = n
			cur.line_start = lnum
			continue

		kv = line.split(splitchar, 1)
		if len(kv) != 2:
			cur.add(line, True)
			continue
		key, value = kv
		key = key.rstrip()
		value = value.lstrip().rstrip(';')
		cur.add(key, value)

	f.close()

	return cur

def parse_nagios_cfg(path):
	comp = parse_conf(path)
	main_config_dir = os.path.dirname(os.path.abspath(path))
	temp_path_i = False
	temp_path = False
	temp_file_i = False
	temp_file = False
	comp.command_file = False
	comp.query_socket = False
	i = -1
	for k, v in comp.params:
		i += 1
		if not '_' in k:
			continue
		if k == 'broker_module':
			ary = v.split(' ')
			modpath = ary[0]
			if modpath[0] == '/':
				continue
			else:
				ary[0] = os.path.abspath(main_config_dir + '/' + modpath)
				comp.params[i] = ('broker_module', ' '.join(ary))
		if k != 'query_socket':
			last = k.split('_')[-1]
			if last != 'file' and last != 'dir' and last != 'path':
				continue
		if k == 'temp_path':
			temp_path_i = i
			temp_path = v
		if k == 'temp_file':
			temp_file_i = i
			temp_file = v
		if v[0] != '/':
			v = os.path.abspath(main_config_dir + '/' + v)
		comp.params[i] = (k, v)
		if k == 'command_file':
			comp.command_file = v
		elif k == 'query_socket':
			comp.query_socket = v

	# This is how Nagios does it
	if not temp_path_i:
		temp_path = os.getenv("TMPDIR")
		if not temp_path:
			temp_path = os.getenv("TMP")
		if not temp_path:
			temp_path = '/tmp'
		comp.params.append(('temp_path', temp_path))

	if not temp_file_i:
		temp_file = '%s/nagios.tmp' % temp_path
		comp.params.append(('temp_file', '%s/nagios.tmp' % temp_path))
	elif not temp_file[0] == '/':
		if not '/' in temp_file:
			temp_file = '%s/%s' % (temp_path, temp_file)
		else:
			temp_file = '%s/%s' % (temp_path, os.path.abspath(temp_file))
		comp.params[temp_file_i] = ('temp_file', '%s' % temp_file)

	comp.temp_path = temp_path
	comp.temp_file = temp_file
	# we're basically guessing if these aren't set, but it follows the
	# Nagios defaults more or less, so that's probably ok.
	if not comp.command_file:
		comp.command_file = '%s/rw/naemon.cmd' % (comp.temp_path)
	if not comp.query_socket:
		comp.query_socket = "%s/naemon.qh" % (os.path.dirname(comp.command_file))

	return comp

def write_conf(f, opt, nesting = 0):
	if nesting:
		f.write("\n" + (' ' * (nesting - 4)) + opt.name + " {\n")
	for (key, value) in opt.params:
		f.write(' ' * nesting)
		f.write(key + " = " + value + "\n")

	# iterate by recursion
	for obj in opt.objects:
		write_conf(f, obj, nesting + 4)

	if nesting:
		f.write((' ' * (nesting - 4)) + "}\n")
