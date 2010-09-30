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
