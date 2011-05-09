import os.path, glob, sys
try:
	import hashlib
except ImportError:
	import sha as hashlib

modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.insert(0, modpath)

import merlin_conf as mconf
import merlin_db

# We should have a common config file for the mon suite, so this module could
# use the same merlin_dir var that mon.py uses. We don't have that yet.
merlin_dir = '/opt/monitor/op5/merlin'
log_file = '/tmp/merlin-sql-upgrade.log'

def cmd_fixindexes(args):
	print 'Adding indexes...'
	print '(if this takes forever, aborting this and running'
	print '\tmon log import --truncate-db'
	print 'might be quicker, but could permanently remove some old logs)'
	conn = merlin_db.connect(mconf)
	cursor = conn.cursor()
	log = []
	for table in glob.glob(merlin_dir + '/sql/mysql/*-indexes.sql'):
		queries = file(table)
		try:
			cursor.execute(queries.read())
		except BaseException, ex:
			log.append('%s: %s' % (os.path.basename(table), ex[1]))
		queries.close()
	conn.commit()
	conn.close()
	if not log:
		print 'Indexes installed successfully'
	else:
		print '\nThere were problems. This is normal if the indexes already',
		print '(partially) exist,\nbut could also indicate a problem.'
		print '\nMessages:'
		for msg in log:
			print '  %s' % msg

def cmd_cahash(args):
	conn = merlin_db.connect(mconf)
	dbc = conn.cursor()
	hash = hashlib.sha1()
	dbc.execute("SELECT contact, host FROM contact_access "
		"WHERE service IS NULL "
		"ORDER BY contact, host")
	rows = 0
	for row in dbc.fetchall():
		rows += 1
		hash.update("%d %d" % (row[0], row[1]))

	dbc.execute("SELECT contact, service FROM contact_access "
		"WHERE host IS NULL "
		"ORDER BY contact, service")
	for row in dbc.fetchall():
		rows += 1
		hash.update("%d %d" % (row[0], row[1]))

	print("rows: %d; hash: %s" % (rows, hash.hexdigest()))

def module_init(args):
	return args
