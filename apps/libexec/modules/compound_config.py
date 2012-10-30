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

	return cur

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
