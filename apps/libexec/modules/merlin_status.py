import sys, os

mod_path = '/usr/libexec/merlin/modules/'
if not mod_path in sys.path:
	sys.path.append(mod_path)
import merlin_conf as mconf

class merlin_status:
	dbc = False

	def __init__(self, dbc):
		self.dbc = dbc

	def min_avg_max(self, table, col, filter=None, iid=None):
		"""
		Fetch min, average and max from 'table',
		optionally filtering on instance_id, passed as 'iid'
		"""
		where = 'WHERE'
		query = 'SELECT min(%s), avg(%s), max(%s) FROM %s' % \
			(col, col, col, table)

		if iid != None:
			query += ' WHERE instance_id = %d' % iid
			where = 'AND'

		if filter and len(filter):
			query = "%s %s %s" % (query, where, filter)

		self.dbc.execute(query)
		row = self.dbc.fetchone()
		ret = {'min': row[0], 'avg': row[1], 'max': row[2]}
		return ret


	def num_entries(self, table, filter=None, iid=None):
		"""
		Fetch the number of entries in 'table', optionally
		filtering on instance_id, passed as 'iid'	
		"""
		where = 'WHERE'
		query = 'SELECT count(1) FROM %s' % table

		if iid != None:
			query += ' WHERE instance_id = %d' % iid
			where = 'AND'

		if filter:
			query = "%s %s %s" % (query, where, filter)

		self.dbc.execute(query)
		return self.dbc.fetchone()[0]


	def node_status(self, iid=False):
		ret = {
			'checks_run': {
				'host': self.num_entries('host', '', iid),
				'service': self.num_entries('service', '', iid),
			},
			'latency': {
				'host': self.min_avg_max('host', 'latency', '', iid),
				'service': self.min_avg_max('service', 'latency', '', iid),
			}
		}
		return ret


	def iid(self, name = False):
		if not name:
			return 0

		query = "SELECT instance_id FROM program_status " \
			"WHERE instance_name = '%s'" % name
		self.dbc.execute(query)
		row = self.dbc.fetchone()
		return row[0]


	def hostgroup_hostchecks_query(self, hg_str, what="COUNT(1)", where=False):
		"""
		Creates a query searching for 'what' among all hosts in
		the listed hostgroups. This handles hosts sitting in multiple
		hostgroups.
		"""
		if where != False:
			where = " AND %s" % where
		else:
			where = ""
		query = """
			SELECT %s FROM host h WHERE h.id IN(
				SELECT DISTINCT host
					FROM host_hostgroup hhg, hostgroup hg
					WHERE hg.hostgroup_name IN(%s) AND hhg.hostgroup = hg.id
					%s
			)""" % (what, hg_str, where)
		return query

	def hostgroup_servicechecks_query(self, hg_str, what="COUNT(1)", where=False):
		"""
		Creates a query searching for 'what' among all services on
		all hosts in the listed hostgroups. This handles hosts
		sitting in multiple hostgroups.
		"""
		if where != False:
			where = " AND %s" % where
		else:
			where = ''
		query = """
			SELECT %s FROM service s WHERE s.host_name IN(
				SELECT DISTINCT host_name
					FROM host h, hostgroup hg, host_hostgroup AS hhg
					WHERE h.id = hhg.host AND hhg.hostgroup = hg.id
						AND hg.hostgroup_name IN(%s) %s
			)""" % (what, hg_str, where)
		return query

	def hostgroup_checks(self, hglist):
		ret = {'host': 0, 'service': 0}
		if not hglist:
			return {'host': 0, 'service': 0}

		hg_str = "'%s'" % "', '".join(hglist)

		query = self.hostgroup_hostchecks_query(hg_str)
		self.dbc.execute(query)
		row = self.dbc.fetchone()
		ret['host'] = row[0]

		query = self.hostgroup_servicechecks_query(hg_str)
		self.dbc.execute(query)
		row = self.dbc.fetchone()
		ret['service'] = row[0]
		return ret


	def assigned_checks(self, node, info):
		"""
		Calculates how many checks of each type 'node' is supposed
		to run, with a min/max value for both.
		Note that in order to calculate this properly, it's necessary
		to figure out how many checks each of its pollers should do.
		If it's a master node, we return 0/0:0/0, since masters should
		never run any checks for us.
		If the node has no peers or pollers, we always return the total
		amount of checks.
		If the node has peers, the numbers returned are the lowest
		number of checks it should do. The caller will have to add 1
		to get the max amount of checks it's OK for this node to run.
		"""

		# use cached values if available
		sr = getattr(node, 'assigned_checks', False)
		if sr:
			return sr

		sr = {}
		attr_name = 'assigned_checks'
		setattr(node, attr_name, {})
		if node.ntype == 'master':
			node.assigned_checks = {'host': 0, 'service': 0}
			return node.assigned_checks
		if node.ntype == 'poller':
			sr = self.hostgroup_checks(node.options.get('hostgroup'))
			node.assigned_checks = {
				'host': sr['host'] / (node.num_peers + 1),
				'service': sr['service'] / (node.num_peers + 1)
			}
			# cache the 'assigned_checks' info for all peers
			for n in node.peer_nodes.values():
				setattr(n, attr_name, node.assigned_checks)
			return node.assigned_checks

		# This is a peer. That means it should have the same pollers
		# as we do. If there are no pollers, we just divide up the
		# total checks between the total number of peers
		if not mconf.num_nodes['poller']:
			node.assigned_checks = {
				'host': self.num_entries('host') / (node.num_peers + 1),
				'service': self.num_entries('service') / (node.num_peers + 1),
			}
			return node.assigned_checks

		# otherwise things get more complicated and we need to query
		# the database to see how many checks there are that aren't
		# handled by any poller and whose alphabetical sorting happens
		# to co-incide with which peer
		poller_hgs = []
		for n in mconf.configured_nodes.values():
			if n.ntype != 'poller':
				continue
			poller_hgs += n.options.get('hostgroup')

		sa_peer_id = info['basic']['self_assigned_peer_id']
		poller_hgs = set(poller_hgs)
		poller_hgstr = "'%s'" % ("', '".join(poller_hgs))

		# we must decrement database id's with 1, since the
		# indexes in merlin are zero based but the ones in
		# the database start at 1
		query_suffix = "AND (id - 1) %% %d = %d""" % (
			mconf.num_nodes['peer'] + 1, sa_peer_id
		)

		# first hosts
		query = "SELECT COUNT(1) FROM host WHERE id NOT IN(%s) %s" % (
			self.hostgroup_hostchecks_query(poller_hgstr, "id"),
			query_suffix
		)
		self.dbc.execute(query)
		host_row = self.dbc.fetchone()

		# and then services
		query = "SELECT COUNT(1) FROM service WHERE id NOT IN(%s) %s" % (
			self.hostgroup_servicechecks_query(poller_hgstr, 'id'),
			query_suffix
		)
		self.dbc.execute(query)
		service_row = self.dbc.fetchone()
		node.assigned_checks = {
			'host': host_row[0],
			'service': service_row[0]
		}

		return node.assigned_checks


	def status(self, node_name=None):
		ret = {'total': self.node_status()}
		query = """SELECT instance_id, instance_name, last_alive,
			is_running, self_assigned_peer_id,
			configured_masters,	active_masters,
			configured_peers, active_peers,
			configured_pollers, active_pollers
			FROM program_status"""

		if node_name:
			query += " WHERE instance_name = '%s'" % node_name

		self.dbc.execute(query)

		nodes = {}
		for row in self.dbc.fetchall():
			name = row[1]
			if name == 'Local Nagios/Merlin instance':
				name = '_local.ipc'
			nodes[name] = {}
			res = {'iid': row[0], 'name': name, 'last_alive': row[2], 'active': row[3]}
			res['self_assigned_peer_id'] = row[4]
			res['configured_masters'] = row[5]
			res['active_masters'] = row[6]
			res['configured_peers'] = row[7]
			res['active_peers'] = row[8]
			res['configured_pollers'] = row[9]
			res['active_pollers'] = row[10]
			nodes[name]['basic'] = res
			node = mconf.configured_nodes.get(name, False)
			# we read ourselves, or an unconfigured node
			if not node:
				node = mconf.merlin_node(name)
				node.in_config = False
				node.ntype = 'peer'
				node.num_peers = mconf.num_nodes['peer']

			node.in_db = True
			nodes[name]['node'] = node

		return nodes
