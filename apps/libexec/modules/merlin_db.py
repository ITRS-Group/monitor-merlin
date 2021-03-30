import sys

conn = False

def connect(mconf, reuse_conn=True):
	"""
	Connects to the merlin database configured by the 'mconf'
	object passed as its only argument and returns a connection handle
	suitable for running queries against that database.
	"""
	global conn

	if conn and reuse_conn:
		return conn

	dbopts = getattr(mconf, 'dbopt', False)
	if dbopts == False:
		dbopts = mconf

	db_host = mconf.dbopt.get('host', 'localhost')
	db_name = mconf.dbopt.get('name', 'merlin')
	db_user = mconf.dbopt.get('user', 'merlin')
	db_pass = mconf.dbopt.get('pass', 'merlin')
	db_type = mconf.dbopt.get('type', 'mysql')
	db_port = mconf.dbopt.get('port', False)
	db_conn_str = mconf.dbopt.get('conn_str', False)

	# now we load whatever database driver is appropriate
	if db_type == 'mysql':
		try:
			import MySQLdb as db
		except ImportError:
			try:
				import pymysql as db
			except ImportError:
				print("Failed to import MySQLdb or PyMySQL")
				print("Install MySQL-python or python2-PyMySQL to make this command work")
				sys.exit(1)
		if db_pass == False:
			conn = db.connect(host=db_host, user=db_user, db=db_name)
		else:
			conn = db.connect(host=db_host, user=db_user, passwd=db_pass, db=db_name)

	elif db_type in ['postgres', 'psql', 'pgsql']:
		try:
			import pgdb as db
		except:
			print("Failed to import pgdb")
			print("Install postgresql-python to make this command work")
			sys.exit(1)
		conn = db.connect(user=db_user, password=db_pass, host=db_host, database=db_name)
	else:
		print("Invalid database type selected: %s" % db_type)
		print("Cannot continue")
		sys.exit(1)

	#print("Connecting to %s on %s with %s:%s as user:pass" %
	#	(db_name, db_host, db_user, db_pass))
	if not conn:
		conn = db.connect(host=db_host, user=db_user, passwd=db_pass, db=db_name)

	return conn

def disconnect():
	global conn
	ret = conn.close()
	conn = False
	return ret
