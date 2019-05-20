import os, sys
import time
import subprocess
import merlin_db

LS_SOCK='/opt/monitor/var/rw/live_tmp'

def cmd_check_consistency(args):
	"""
	Check for consistency errors in report_data.

	This command outputs any consistency errors if the latest entry for a
	host/service in the report_data table does not match the current
	livestatus state.
	"""

	dbc = merlin_db.connect(mconf).cursor()

	dbc.execute("SELECT A.ID, A.timestamp, A.host_name, A.service_description, state \
			FROM report_data A\
			INNER JOIN (SELECT max(timestamp) TS, host_name, service_description\
				FROM report_data\
				GROUP BY host_name, service_description) B\
			ON A.timestamp = B.TS\
			AND A.host_name = B.host_name\
			AND A.service_description = B.service_description")

	rows = dbc.fetchall()

	errors=0
	for row in rows:
		id=row[0]
		timestamp=row[1]
		host_name=row[2]
		service_description=row[3]
		state=row[4]
		QRES=""
		try:
			if service_description:
				ls_query="GET services\nColumns: last_hard_state\nFilter: host_name = " + host_name + "\nFilter: description = " + service_description
				ls_state=subprocess.Popen("echo \"" + ls_query + "\" | unixcat " + LS_SOCK, shell=True, stdout=subprocess.PIPE).stdout.read()
				if (int(state) != int(ls_state)):
					print "ERROR: " + host_name + ";" + service_description + " : db_state=" + str(state) + " ls_state: " + ls_state.rstrip()
					errors=errors+1
			elif host_name:
				ls_query="GET hosts\nColumns: last_hard_state\nFilter: host_name = " + host_name
				ls_state=subprocess.Popen("echo \"" + ls_query + "\" | unixcat " + LS_SOCK, shell=True, stdout=subprocess.PIPE).stdout.read()
				if (int(state) != int(ls_state)):
					print "ERROR: " + host_name + " : db_state=" + str(state) + " ls_state: " + ls_state.rstrip()
					errors=errors+1
		# probably means state or ls_state is not an int, so we just continue
		except ValueError as e:
			continue

	merlin_db.disconnect()
	if (errors == 0):
		print "OK"
		return True
	else:
		print "Errors: {}".format(errors)
		return False
