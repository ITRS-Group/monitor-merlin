import os.path, glob, sys
modpath = os.path.dirname(__file__) + '/modules'
if not modpath in sys.path:
	sys.path.insert(0, modpath)

import merlin_conf as mconf
from merlin_db import connect, disconnect

# yeah, yeah, we are multi-DB aware. But this module only does something on
# mysql
import MySQLdb

# We should have a common config file for the mon suite, so this module could
# use the same merlin_dir var that mon.py uses. We don't have that yet.
merlin_dir = '/opt/monitor/op5/merlin'
log_file = '/tmp/merlin-sql-upgrade.log'

def cmd_fixindexes(args):
	print 'Adding indexes...'
	print '(if this takes forever, aborting this and running'
	print '\tmon log import --truncate-db'
	print 'might be quicker, but could permanently remove some old logs)'
	conn = connect(mconf)
	cursor = conn.cursor()
	log = []
	for table in glob.glob(merlin_dir + '/sql/mysql/*-indexes.sql'):
		queries = file(table)
		try:
			cursor.execute(queries.read())
		except MySQLdb.OperationalError, ex:
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
			print '\t%s' % msg

def module_init(args):
	return args
