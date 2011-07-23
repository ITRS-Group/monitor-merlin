import os, sys, re, time, subprocess
from merlin_apps_utils import *

class coredump:
	"""
	Class for crudely examining (most) coredumps and finely examining
	coredumps from monitor, merlin, ocimp and other core parts of op5
	Monitor.
	"""
	path = ''
	command_line = ''
	executable = ''
	bt = ''

	# non-zero if core for some reason appears to be invalid
	invalid = 0

	# codes for 'invalid' above
	INV_EMPTY = 1
	INV_EXEC_IS_NEWER = 2
	INV_FILE_CMD = 3
	INV_ESRCH_EXEC = 4

	def __init__(self, path):
		self.path = path

	def get_executable(self):
		"""
		Tries to determine the executable name of the program producing
		the coredump. We'll be majorly fucked if it's not one we know of
		or if it's not in the path.
		"""
		stuff = subprocess.Popen(['/usr/bin/file', '-b', self.path], stdout=subprocess.PIPE)
		self.file_cmd_output = stuff.communicate()[0].strip()
		ary = re.split(".*, from '", self.file_cmd_output, 1)
		if len(ary) == 1:
			# not a "real" coredump, so just ignore it
			self.invalid = self.INV_FILE_CMD
			return False

		# we get rid of whitespace, remaining single-quotes and
		# leading dots and slashes as well
		cmd_line = ary[1].strip()
		if cmd_line[-1] == "'":
			cmd_line = cmd_line[:-1]
		self.cmd_line = cmd_line
		self.executable = os.path.basename(cmd_line.split(' ', 1)[0])

		# relative paths require some extra work, so we take care
		# of the ones we know of
		if self.executable[0] != '/':
			basename = os.path.basename(self.executable).strip('./\\')
			if basename == 'merlind':
				self.executable = '/opt/monitor/op5/merlin/merlind'
			elif basename == 'monitor':
				self.executable = '/opt/monitor/bin/monitor'
			elif basename == 'ocimp':
				self.executable = '/opt/monitor/op5/merlin/ocimp'
			elif baseneme == 'check_nt':
				self.executable = '/opt/plugins/check_nt'
			else:
				self.invalid = self.INV_ESRCH_EXEC

		print(self.executable)


	def get_backtrace(self):
		if not self.executable or not self.path:
			return False
		gdb = ['/usr/bin/gdb', '-batch', '-ex', 'bt full',
			'-c', self.path, '--exec=%s' % self.executable]
		stuff = subprocess.Popen(gdb, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
		(self.gdb_stdout, self.gdb_stderr) = stuff.communicate()
		self.gdb_stdout = self.gdb_stdout.strip()
		self.gdb_stderr = self.gdb_stderr.strip()
		print("GDB STDERR START")
		print(self.gdb_stderr)
		print("GDB STDERR END")
		print("GDB STDOUT START")
		print(self.gdb_stdout)
		print("GDB STDOUT END")
		for l in self.gdb_stderr.split('\n'):
			if not len(l):
				continue
			# cores produced by older versions of the program can't
			# sanely be debugged, so just ignore them and mark them
			# as invalid so the caller can determine what to do with
			# them.
			if l == 'warning: exec file is newer than core file.':
				self.invalid = self.INV_EXEC_IS_NEWER
				return False


	def examine(self):
		"""
		Examines a corefile and attempts to determine executable,
		backtrace and error location in the offending executable.
		If no executable can be found (or determined), or if the
		executable or any of its on-demand loaded modules lack
		debugging symbols, the backtrace will be sparse.
		"""
		print("Examining %s" % self.path)
		self.get_executable()
		self.get_backtrace()
