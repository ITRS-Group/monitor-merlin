import os, sys, time, copy
from pprint import pformat, pprint
import traceback

class pytap:
	"""Python tap-ish class for testing blackbox systems where it's
	nifty to reset the test-suite every once in a while (such as when
	a particular condition has been met, and the only way to find out
	if it has been met is to run the tests)"""
	TODO = 1
	CRITICAL = 2
	SKIP = 4
	OK = 'ok'
	WARNING = 'warning'
	FAIL = 'fail'

	def __init__(self, name='Anonymous test-suite', master=False):
		self.name = False
		self.tap_compat = False
		self.section_name = ''
		self.num_tests = 0
		self.planned_tests = False
		self.skip = False
		self.skip_msg = False
		self.skipped = 0
		self.flags = 0
		self.todo_msg = False
		self.summarized = False
		self.ansi_colors = False
		self.color_reset = "\033[0m"
		self.is_done = False
		self.colors = None
		self.ind_lvl = 0
		self.parent = False
		self.use_colors = True
		self.suite = ''
		self.show_diag = False
		self.have_header = False
		self.verbose = 1; # print buggy, todo, skip etc by default
		self.failures = []
		self.faildiag = []
		self.store_fails = True
		self.show_fails = True
		self.init(name, os.isatty(sys.stdout.fileno()))


	def _print_one(self, what, msg):
		if self.parent and not self.verbose:
			return

		self.print_header(True);
		self.show_diag = True;

		if self.tap_compat and what == 'fail':
			what = 'not ok'

		self._indent()
		if self.tap_compat:
			print("%s %d # %s" % (what, self.num_tests, msg))
		else:
			if type(what) == type('foo'):
				sys.stdout.write(' ' * (4 - len(what)))
			print("%-4s: %s" % (self._colorize(what), msg))


	def get_status(self):
		"""Returns the test-suite's overall status as an integer
		If all tests passed, 0 will be returned.
		If all any test failed, 2 will be returned.
		If any tests are buggy, skipped or in todo state, 1 will
		be returned.
		"""
		if self.tcount[self.FAIL]:
			return 2

		if self.tcount['ok'] + self.tcount['fixed'] == self.num_tests:
			return 0
		# some tests are buggy, todo or broken
		return 1


	def _colorize(self, what, color=False):
		if type(color) == type(False) and color == False:
			color = what

		if not self.use_colors or not self.colors:
			return what

		if not self.colors.get(color, False):
			print("No color found to match %s" % color)
			return what

		return "%s%s%s" % (self.colors[color], what, self.color_reset)


	def _indent(self, ind_lvl=0):
		if not self.ind_lvl + ind_lvl:
			return

		buf = '  ' * (self.ind_lvl + ind_lvl)
		sys.stdout.write(buf)


	def _summarize(self):
		if not self.have_header:
			return

		status = self.get_status()

		self.stop_time = time.time()
		self.runtime = self.stop_time - self.start_time
		if self.parent:
			summary = self._colorize("### @%d %s. " % (self.stop_time, self.suite), status)
		else:
			summary = self._colorize('### @%d ' % (self.stop_time), status)

		if self.tcount['ok'] and len(self.tcount) == 1:
			summary += self._colorize("All self.num_tests tests passed in %.2fs" % self.runtime, 'brightgreen')
		else:
			summary += "%.2fs total: %d" % (self.runtime, self.num_tests)
			for (cat, num) in self.tcount.items():
				if not num:
					continue
				summary += ", " + self._colorize("%s: %s" % (cat, num), cat)

		self._indent()
		print("%s" % summary)


	#####################################
	#
	# public functions
	#
	def num_failed(self):
		return self.tcount[self.FAIL]

	def num_passed(self):
		return self.tcount['ok'] + self.tcount['fixed']

	def reset(self):
		"""Resets the state of the tap object (test-counts etc)"""
		self.skip_end()
		self.todo_end()
		self.status = 'AOK'
		self.tcount = {
			'ok': 0, 'fail': 0, 'not ok': 0,
			'todo': 0, 'fixed': 0, 'buggy': 0,
		}
		self.num_tests = 0

	def init(self, suite=False, colors_too=False, **kwargs):
		"""
		Initialize the tap-object
		@param suite Test-suite name/description
		@param colors_too Reset colors too
		"""
		if suite:
			self.suite = suite
		self.start_time = time.time()
		self.reset()
		self.have_header = False
		if self.parent:
			self.colors = self.parent.colors
		elif (colors_too):
			self.colors = {
				'red': "\033[31m",
				'brightred': "\033[31m\033[1m",
				'green': "\033[32m",
				'gray': "\033[30m\033[1m", # "bright" black
				'brightgreen': "\033[32m\033[1m",
				'brown': "\033[33m",
				'yellow': "\033[33m\033[1m",
				'blue': "\033[34m",
				'brightblue': "\033[34m\033[1m",
				'pink': "\033[35m",
				'brightpink': "\033[35m\033[1m",
				'cyan': "\033[36m",
				'brightcyan': "\033[36m\033[1m",
			}

			self.set_color('buggy', 'brightpink')
			self.set_color('ok', 'green')
			self.set_color('fixed', 'brightgreen')
			self.set_color('todo', 'yellow')
			self.set_color('broken', 'todo')
			self.set_color('still broken', 'todo')
			self.set_color('skip', 'pink')
			self.set_color('fail', 'red')
			self.set_color('not ok', 'red')
			self.set_color('description', 'brightblue')
			self.set_color('trace', 'yellow')
			self.set_color('warning', 'yellow')
			self.set_color('critical', 'red')
			self.set_color(0, 'green')
			self.set_color(1, 'yellow')
			self.set_color(2, 'red')
			self.set_color('0', 'green')
			self.set_color('1', 'yellow')
			self.set_color('2', 'red')

		if suite:
			self.print_header(suite)


	def sub_init(self, desc=False, verbose=None):
		"""
		Initialize a sub-suite
		@param desc Description for the sub-suite
		@return A new TAP object, to be used for the sub-suite's tests
	 	"""
		sub = copy.copy(self)
		if verbose:
			sub.verbose = verbose
		sub.parent = self
		sub.ind_lvl = self.ind_lvl + 1
		sub.init(desc, self.use_colors)
		return sub


	def show_colors(self):
		"""List which colors are set"""
		color_reset = "\033[0m"
		for (name, color) in self.colors.items():
			print("%s%s%s" % (color, name, color_reset))


	def set_color(self, name=False, color=False):
		"""Set a color for a certain description
		@param str The description (ie, 'fail')
		@param color The color to use
	 	"""
		#if (not name or not color or not self.colors.get(color)):
		#	return
		self.colors[name] = self.colors[color]


	def plan(self, num=False):
		"""Plan a number of tests (default plan is to have no plan)
		@param num The number of tests planned
		"""
		if not num:
			return
		self.planned_tests = num


	def todo_start(self, msg=False, num=True):
		"""Mark a number of tests as 'todo'
		@param msg A message to print for the failing tests
		@param num If given, mark a pre-determined number of tests as todo
		"""
		self.todo = num
		self.todo_msg = msg


	def todo_end(self, output = True):
		"""Stop marking tests as todo
		@param output Currently unused
		"""
		self.flags &= ~self.TODO
		self.todo_msg = False
		self.todo_ents = 0


	def skip_start(self, msg=False, num=0):
		"""Mark a number of tests as 'skipped'
		@param msg A message to print for the skipped tests
		@param num If given, mark a pre-determined number of tests as skipped
		"""
		self.flags |= self.SKIP
		self.skip = num
		self.skip_msg = msg


	def skip_end(self, output=True):
		"""Skip no more tests
		@param output If True (default), print a message saying
		how many tests were skipped
		"""
		if output and self.skipped and not self.verbose:
			msg = "Skipped %d test%s" % (self.skipped, ('', 's')[self.skipped > 1])
			if self.skip_msg:
				msg = "%s: %s" % (msg, self.skip_msg)
			self._print_one('skip', msg)

		self.flags &= ~self.SKIP
		self.skip_msg = False
		self.skipped = 0


	def print_header(self, suite=False):
		"""Print a test-suite header (description)
		@param suite The name/description to print
		"""
		if self.parent and not self.verbose:
			return

		# don't print header twice
		if (self.have_header):
			return

		if type(suite) == type('foo'):
			self.suite = suite

		# print all parent headers recursively
		if suite == True and self.parent:
			self.parent.print_header(True)

		# skip subsuites unless forced or verbose
		if self.parent and not self.verbose and suite != True:
			return

		# we're actually going to print this one
		self.have_header = True

		suite = ('', 'subsuite: ')[self.parent != False]
		suite += self.suite

		self._indent()
		print(self._colorize("### @%d %s ###" % (self.start_time, suite), 'description'))


	def ok(self, result, msg, flags=0):
		"""The workhorse. All test_* functions end up here
		@param result The test-result. Must be 'True' for test to pass
		@param msg A description of this particular test
		@param flags OR'ed bitflags for this test (self.TODO etc)
		@return True if test passed. False otherwise.
		"""
		self.show_diag = False
		should_print = False

		# we add global flags once
		flags |= self.flags

		self.num_tests += 1

		if (self.planned_tests and self.num_tests > self.planned_tests):
			sys.stdout.write("Test #%d out of self.planned_tests. What voodoo is this?!\n", self.num_tests)

		if flags & self.SKIP:
			what = 'skip'
			if self.skip:
				self.skipped += 1
				if self.skipped == self.skip:
					self.skip_end()
		elif type(result) != type(True):
			what = 'buggy'
		elif result == True:
			what = 'ok'
			if flags & self.TODO != 0:
				what = 'fixed'
		else:
			what = ('todo', 'fail')[flags & self.TODO == 0]

		self.tcount[what] = self.tcount.get(what, 0) + 1

		if what == 'ok' or what == 'skip':
			should_print = (self.verbose > 1)
		elif what == 'fail' or what == 'not ok' or what == 'buggy':
			should_print = True
		else:
			should_print = (self.verbose != 0)

		if not should_print:
			return result == True

		self._print_one(what, msg)
		if what == 'ok' or what == 'fixed':
			return True

		if what == 'buggy':
			self.diag('pytap::ok() not passed a boolean value')
		stack = traceback.extract_stack()
		me = os.path.basename(__file__)
		if me[-1] == 'c' or me[-1] == 'o':
			# pyc and pyo files
			me = me[:-1]
		diag = []
		skip = 0
		indent = 0
		i = len(stack)
		while i >= 0:
			i -= 1
			(filename, lineno, funcname, text) = stack[i]
			if filename == me or filename.endswith('/' + me):
				skip += 1
				continue
			filename = os.path.basename(filename)
			if funcname == '<module>':
				funcname = '(main)'
			msg = "%s%s:%d:%s(): %s" % (' ' * indent, filename, lineno, funcname, text)
			indent += 1
			self.diag(msg)
			break

		return False


	def ok_gt(a, b, msg, flags=False):
		"""
		Verify that the first variable is greater than the second
		@param a The first variable (which should be the greater)
		@param b The second variable (which should be the lesser)
		@param msg A description of this particular test
		@param flags OR'ed bitflags for this test (self.TODO etc)
		@return True if test passed. False otherwise.
		"""
		ret = self.ok(a > b, msg, flags)
		if not ret:
			self.diag(a, '<=', b)
		return ret


	def ok_eq(self, a, b, msg, flags=False):
		"""
		Verify that two variables are equal
		@param a The first variable
		@param b The second variable
		@param msg A description of this particular test
		@param flags OR'ed bitflags for this test (self.TODO etc)
		@return True if test passed. False otherwise.
		"""
		ret = self.ok(a == b, msg, flags)
		if not ret:
			self.diag([a, '!=', b])
		return ret


	def ok_type(self, a, b, msg, flags=False):
		ret = self.ok(type(a) == type(b), msg, flags)
		if not ret:
			self.diag([type(a), '!=', type(b)])
		return ret


	def ok_empty(self, ary, msg, flags=False):
		"""Verify that the argument is an empty array
		@param ary The array to test
		@param msg A description of this particular test
		@param flags OR'ed bitflags for this test (self.TODO etc)
		@return True if test passed. False otherwise.
		"""
		ret = self.ok(len(ary) == 0, msg, flags)
		if not ret:
			self.diag(ary)
		return ret


	def diag(self, msg, diag_depth=0):
		"""Print diagnostic message (part of semi-official TAP API)
		@param msg The message to print (can be arrays)
		@param diag_depth Diagnostic depth (used internally)
		"""
		if not self.show_diag or not self.verbose:
			return
		if type(msg) != type([]):
			msg = [msg]
		for v in msg:
			self._indent(1)
			if type(v) == type(str('lala')):
				v = str(v)
			elif type(v) != type('foo'):
				v = pformat(v)
			print("%s# %s" % ('  ' * diag_depth, v))


	def is_array_subset(self, a, b, msg, flags=0):
		"""Test if one array is a subset of another
		@param a The full array
		@param b The subset array
		@param msg A description of this particular test
		@param flags OR'ed bitflags for this test (self.TODO etc)
		@return True if $b is a subset of $a. False otherwise
		"""
		for k in a:
			if b.get(k, None) == None:
				return self.test_fail(msg)
		return self.test_pass(msg, flags)


	def done(self, do_exit=False):
		"""Finish a test-suite and all its sub-suits
		@param do_exit If True, exit with the current test-suite status
		@return 2 if there are tests with 'failed' status.
	            1 if not all tests passed.
	            0 if all tests passed.
		"""
		if do_exit:
			sys.exit((int)(self.done(False) > 1))

		self._summarize()

		if self.parent:
			self.parent.num_tests += self.num_tests
			for (cat, num) in self.tcount.items():
				if not self.parent.tcount.get(cat):
					self.parent.tcount[cat] = 0
				self.parent.tcount[cat] += num

		status = self.get_status()
		self.init()
		return status


	def test(self, a, b, msg, flags=0):
		return self.ok_eq(a, b, msg, flags)


	def _pass(self, msg, flags=0):
		return self.ok(True, msg, flags)


	def _fail(self, msg, flags=0):
		return self.ok(False, msg, flags)


	def fail(self, msg, flags=0):
		return self._fail(msg, flags)
