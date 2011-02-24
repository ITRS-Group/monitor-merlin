conn = False

def connect(mconf):
	""" Connects to the merlin database configured by the 'mconf'
	object passed as its only argument and returns a cursor suitable
	for running queries against that database."""
	global conn

	db_host = mconf.dbopt.get('host', 'localhost')
	db_name = mconf.dbopt.get('name', 'merlin')
	db_user = mconf.dbopt.get('user', 'merlin')
	db_pass = mconf.dbopt.get('pass', 'merlin')
	db_type = mconf.dbopt.get('type', 'mysql')
	db_port = mconf.dbopt.get('port', False)

	# now we load whatever database driver is appropriate
	if db_type == 'mysql':
		try:
			import MySQLdb as db
		except:
			print("Failed to import MySQLdb")
			print("Install mysqldb-python or MySQLdb-python to make this command work")
			sys.exit(1)
	elif db_type in ['postgres', 'psql', 'pgsql']:
		try:
			import pgdb as db
		except:
			print("Failed to import pgdb")
			print("Install postgresql-python to make this command work")
			sys.exit(1)
	elif db_type in ['oci', 'oracle', 'ocilib']:
		try:
			import cx_Oracle as db
		except:
			print("Failed to import cx_Oracle")
			print("Install oracle-python to make this command work")
			sys.exit(1)
		if db_port:
			dsn = "//%s:%s" % (db_host, db_port)
		else:
			dsn = "//%s" % (db_host)
		dsn += "/" + db_name
		db_user = db_user.encode('latin1')
		db_pass = db_pass.encode('latin1')
		dsn = dsn.encode('latin1')
		conn = db.connect(db_user, db_pass, dsn)

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
	return conn.close()
