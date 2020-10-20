import os, sys
import compound_config as cconf

from nagios_qh import nagios_qh
from merlin_apps_utils import *

qh = '/opt/monitor/var/rw/nagios.qh'
single = False

def module_init(args):
	global qh
	global single
	rem_args = []
	comp = cconf.parse_conf(nagios_cfg)
	for v in comp.params:
		if v[0] == 'query_socket':
			qh = v[1]
			break
	for arg in args:
		if arg.startswith('--socket='):
			qh = arg.split('=', 1)[1]
			continue
		if arg == '--single':
			single = True
			continue
		rem_args.append(arg)
	return rem_args


def args_to_query(args):
	if not len(args):
		return False

	# one-shot queries are default
	if args[0][0] != '#' and args[0][0] != '@':
		args[0] = '#' + args[0]

	return ' '.join(args)


def cmd_query(args):
	"""--socket=</path/to/query-socket> --single <query>
	Runs an arbitrary query with the nagios query handler and prints its
	raw output. Queries need not include the trailing nulbyte or leading
	hash- or at-sign.
	"""
	handler = nagios_qh(qh)
	query = args_to_query(args)
	if not query:
		prettyprint_docstring('query', cmd_query.__doc__, 'Not enough arguments')
		return
	for block in handler.query(query):
		print block,
		sys.stdout.flush()
		if single:
			return

def cmd_get(args):
	"""--socket=</path/to/query-socket> <query>
	Runs an arbitrary query with the nagios query handler and pretty-prints
	the output. Queries need not include the trailing nulbyte or leading
	hash- or at-sign.
	"""
	handler = nagios_qh(qh)
	query = args_to_query(args)
	if not query:
		prettyprint_docstring('get', cmd_get.__doc__, 'Not enough arguments')
		return
	resp = handler.query(query)
	try:
		for row in handler.format(resp):
			for pair in row.items():
				print "%-25s %s" % pair
			print ""
	except ValueError, v:
		print "WARNING: Couldn't format response, printing raw response:\n"
		cmd_query(args)
