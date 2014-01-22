#!/usr/bin/python
# hashbang so editors will recognize it as python

"""
This is a WAF build script (http://code.google.com/p/waf/).

Requires WAF 1.7.1 and Python 2.4 (or later).
"""

import sys
import os
import tempfile
from waflib import Logs, Options, Scripting, Utils
from waflib.Configure import ConfigurationContext
from waflib.Errors import WafError
from waflib.TaskGen import feature

APPNAME = 'merlin'
VERSION = '2.0.9'

top = '.'
out = 'WAFBUILD'


db_sources = 'sql.c db_wrap.c'.split()
oconf_sources = 'sha1.c misc.c'
common_sources = 'cfgfile.c shared.c version.c logging.c'.split()
shared_sources = 'ipc.c io.c node.c binlog.c codec.c'.split()
daemon_sources = '''
	status.c daemonize.c daemon.c net.c db_updater.c state.c
'''.strip().split()
ocimp_sources =	'ocimp.c sha1.c slist.c'.split()

def options(opt):
	opt.load('compiler_c')
	opt.load('gnu_dirs')

	#sys.exit(1)
	# Features
	opt.add_option('--libdbi', action='store', default='dflt',
		help='controller for libdbi support [default: No]', dest='libdbi')

	# Paths
	opt.add_option('--mandir', type='string', default='',
		help='man documentation', dest='mandir')
	opt.add_option('--docdir', type='string', default='',
		help='documentation root', dest='docdir')
	opt.add_option('--libdir', type='string', default='',
		help='object code libraries', dest='libdir')


def configure(conf):
	conf.check_waf_version(mini='1.7.1')
	conf.load('compiler_c')

	flags = '-O2 -ggdb3 -pipe'
	flags += ' -Wall -Wextra -Wstrict-prototypes'
	flags += ' -Wdeclaration-after-statement -Wshadow'
	flags += ' -Wformat=2 -Winit-self -Wnonnull'
	# these we ignore (if possible)
	flags += ' -Wno-unused-result -Wno-unused-parameter'
	flags += ' -Wno-format-zero-length'
	for flag in flags.split():
		if conf.check_cc(cflags=flag, mandatory=False) == True:
			conf.env.append_value('CFLAGS', flag)
	# reset line lengths after the long gcc clags
	conf.line_just = 40
	conf.check_cc(header_name='fcntl.h', mandatory=False)
	conf.check_cc(header_name='sys/time.h', mandatory=False)
	conf.check_cc(header_name='sys/types.h', mandatory=False)
	conf.check_cc(header_name='sys/stat.h', mandatory=False)
	conf.check_cc(header_name='nagios/lib/libnagios.h', mandatory=True)
	conf.check_cc(lib='nagios', mandatory=False)
	libdbi_required = conf.options.libdbi.startswith('enable')

	if conf.options.libdbi.startswith('dis'):
		have_libdbi = False
	else:
		have_libdbi = conf.check_cc(
			function_name='dbi_conn_new',
			lib='dbi',
			header_name='dbi/dbi.h',
			mandatory=conf.options.libdbi.startswith('enable'),
		)

	# check sunOS socket support
	if Options.platform == 'sunos':
		conf.check_cc(
			function_name='socket',
			lib='socket',
			header_name='sys/socket.h',
			uselib_store='OS_NET',
			mandatory=True
		)
		conf.check_cc(
			function_name='nsl',
			lib='nsl',
			header_name='sys/socket.h',
			uselib_store='OS_NET',
			mandatory=True
		)

	# we build 'merlin.so', not 'libmerlin.so'
	conf.env['cshlib_PATTERN'] = '%s.so'

	conf.define('PACKAGE', APPNAME, quote=True)
	conf.define('VERSION', VERSION, quote=True)

	gitver = _get_git_ver(conf)
	if gitver:
		conf.define('GIT_VERSION', gitver, quote=True)

	# summary
	Logs.pprint('BLUE', 'Summary:')
	conf.msg('Install ' + APPNAME + ' ' + VERSION + ' in', conf.env['PREFIX'])
	if gitver is not None:
		conf.msg('Building git version', gitver)

	if have_libdbi:
		conf.define('DB_WRAP_CONFIG_ENABLE_LIBDBI', 1)
		conf.env.append_value('dblibs', 'dbi')
	if not have_libdbi:
		conf.define('HAVE_DB', 0)
	else:
		conf.define('HAVE_DB', 1)

	conf.env.append_value('dblibs', 'sqlite')

	# some more compiler flags
	conf.write_config_header('waf-config.h', remove=False)



def build(bld):
	if bld.cmd in ('install', 'uninstall'):
		bld.add_post_fun(_post_install)

	bld.objects(
		source = common_sources,
		target = 'common-objects',
		install_path = None)

	bld.objects(
		source = shared_sources,
		target = 'shared-objects',
		install_path = None)

	bld.objects(
		source = db_sources,
		target = 'db-objects',
		install_path = None)

	# demandloaded code has to be compiled with -fPIC.
	# waf handles that automagically, but for optimization
	# reasons we need to use separate object files and can't
	# "use" objects at this stage
	module_sources = common_sources + shared_sources + '''
		module.c hooks.c queries.c pgroup.c misc.c
	'''.strip().split()
	# merlin.so
	bld.shlib(
		source = module_sources,
		target = 'merlin',
		install_path = None)  # do not install this library

	bld.program(
		source = 'oconf.c ' + oconf_sources,
		target = 'oconf',
		lib = 'nagios',
		install_path = None)

	# merlind
	bld.program(
		source = daemon_sources,
		use = ['common-objects', 'shared-objects', 'db-objects'],
		lib = ['nagios'] + bld.env.dblibs,
		target = 'merlind',
		install_path = None)  # do not install this library

	# ocimp
	bld.program(
		source = ocimp_sources,
		use = ['common-objects', 'shared-objects', 'db-objects'],
		lib = ['nagios'] + bld.env.dblibs,
		target = 'ocimp',
		install_path = None)  # do not install this library

	bld.objects(
		source = 'logutils.c lparse.c test_utils.c state.c',
		target = 'lparse-objects',
		install_path = None)

	# import
	bld.program(
		source = 'import.c',
		lib = ['nagios'] + bld.env.dblibs,
		use = ['common-objects', 'db-objects', 'lparse-objects'],
		target = 'import',
	)

	# showlog
	bld.program(
		source = 'showlog.c auth.c',
		lib = 'nagios',
		use = ['common-objects', 'lparse-objects'],
		target = 'showlog',
	)

	###
	# Install files
	###
	# Headers
	bld.install_files('${PREFIX}', '''example.conf''')


def distclean(ctx):
	Scripting.distclean(ctx)


def _post_install(ctx):
	Logs.pprint('RED', "Running _post_install()")


def apidoc(ctx):
	"""generate API reference documentation"""
	basedir = ctx.path.abspath()
	doxygen = _find_program(ctx, 'doxygen')
	doxyfile = 'doxy.conf'
	Logs.pprint('CYAN', 'Generating API documentation')
	ret = ctx.exec_command('%s doxy.conf' % doxygen)
	if ret != 0:
		raise WafError('Generating API documentation failed')


def _find_program(ctx, cmd, **kw):
	def noop(*args):
		pass

	ctx = ConfigurationContext()
	ctx.to_log = noop
	ctx.msg = noop
	return ctx.find_program(cmd, **kw)


def _get_git_ver(conf):
	if not os.path.isdir('.git'):
		return

	try:
		cmd = 'git describe --dirty --always'
		ver = conf.cmd_and_log(cmd).strip()
	except WafError:
		return None
	else:
		return ver
