import sys, os, time, errno

class ansi_color:
	_color_names = "grey red green yellow blue magenta cyan white"
	_attr_names = "none bold faint italic underline blink fast reverse concealed"
	esc = '%s[' % chr(27)
	def __init__(self, f=sys.stdout.fileno()):
		if not os.isatty(f):
			self.bright = ''
			self.reset = ''
			for attr in self._color_names.split():
				setattr(self, attr, '')
			for attr in self._attr_names.split():
				setattr(self, attr, '')
		else:
			self.bright = '\033m\033[1m'
			self.reset = '%s0m' % self.esc
			i = 0
			for name in self._color_names.split():
				setattr(self, name, '%s0;3%dm' % (self.esc, i))
				i += 1
			i = 0
			for name in self._attr_names.split():
				setattr(self, name, '%s0;4%dm' % (self.esc, i))
				i += 1


color = ansi_color()

def time_delta(then, now=time.time()):
	dvals = [('w', 604800), ('d', 86400), ('h', 3600), ('m', 60)]
	ret = ''
	seconds = now - then
	if (seconds < 0):
		seconds = then - now

	for unit, div in dvals:
		if seconds > div:
			ret += "%d%s " % ((seconds / div), unit)
			seconds %= div

	ret += '%ds' % seconds
	return ret


def strtobool(str):
	str = str.lower()
	if str == 'yes' or str == 'true' or str == 'on':
		return True

	if str.isdigit():
		return int(str) != 0

	return False


def mkdir_p(dirname, mode=0777):
	try:
		os.makedirs(dirname, mode)
	except OSError, exc:
		if exc.errno == errno.EEXIST:
			pass
		else:
			raise

def str_to_seconds(str):
	multipliers = {'m': 60, 'h': 3600, 'd': 86400, 'w': 86400 * 7}
	if str.isdigit():
		return int(str)
	suffix = str[-1].tolower()
	multiplier = multipliers.get(suffix.tolower(), None)
	if not str[:-1].isdigit() or multiplier == None:
		return None
	return int(str[:-1]) * multiplier

def find_in_path(basename):
	path_ary = os.getenv(PATH).split(':')
	for p in path_ary:
		path = "%s/%s" % (p, basename)
		if os.access(path, os.X_OK) and os.isfile(path):
			return path

	return False
