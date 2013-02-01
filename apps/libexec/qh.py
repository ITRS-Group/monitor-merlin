import os, sys
import compound_config as cconf

modpath = os.path.dirname(os.path.abspath(__file__)) + '/modules'
if not modpath in sys.path:
	sys.path.append(modpath)
from nagios_qh import nagios_qh

qh = '/opt/monitor/var/rw/nagios.qh'

def module_init(args):
	global qh
	comp = cconf.parse_conf(nagios_cfg)
	for v in comp.params:
		if v[0] == 'query_socket':
			qh = v[1]
			break


def args_to_query(args):
	if not len(args):
		return False

	# one-shot queries are default
	if args[0][0] != '#' and args[0][0] != '@':
		args[0] = '#' + args[0]

	return ' '.join(args)


def cmd_query(args):
	"""Run an arbitrary query with the nagios query handler.
Print raw output.
Commands need not include trailing nullbyte."""
	handler = nagios_qh(qh)
	query = args_to_query(args)
	if not query:
		print """Expected more arguments, try something like 'mon qh help' or
'mon qh query help'"""
		return
	for block in handler.query(query):
		print block,
		sys.stdout.flush()

def cmd_get(args):
	"""Run an arbitrary query with the nagios query handler.
Print pretty-printed output.
Commands need not include trailing nullbyte."""
	handler = nagios_qh(qh)
	query = args_to_query(args)
	if not query:
		print """Expected more arguments, try something like 'mon qh help' or
'mon qh get help'"""
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
