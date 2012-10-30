import sys, os

mod_path = '/usr/libexec/merlin/modules/'
if not mod_path in sys.path:
	sys.path.append(mod_path)
import merlin_conf as mconf
from merlin_qh import get_merlin_nodeinfo

class merlin_status:
	lsc = False
	ni = False

	def __init__(self, lsc, qh):
		self.lsc = lsc
		self.ni = dict([(x['name'], x) for x in get_merlin_nodeinfo(qh)])

	def min_avg_max(self, table, col, filter=None):
		"""
		Fetch min, average and max from 'table',
		optionally filtering on instance_id, passed as 'iid'
		"""
		
		query = 'GET %ss\n' % table
		if filter:
			query += filter
		query += 'Stats: min %s\nStats: avg %s\nStats: max %s\n' % ((col,) * 3)
		row = self.lsc.query_row(query)
		ret = {'min': row[0], 'avg': row[1], 'max': row[2]}
		return ret

	def sum_global(self, key):
		return sum([int(x.get(key)) for x in self.ni.values() if x.get('type') in ('peer', 'local')])

	def num_entries(self, table):
		"""
		Fetch the number of entries in 'table', optionally
		filtering on instance_id, passed as 'iid'	
		"""
		return self.sum_global(table+'_checks_handled')

	def global_status(self):
		ret = {
			'checks_run': {
				'host': self.sum_global('host_checks_executed'),
				'service': self.sum_global('service_checks_executed'),
			},
			'latency': self.sum_global('latency')
		}
		return ret

	def assigned_checks(self, node):
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
		attr_name = 'assigned_checks'
		sr = node.get(attr_name)
		if sr:
			return sr

		node[attr_name] = {}

		# masters should never run any checks
		if node['type'] == 'master':
			node['assigned_checks'] = {'host': (0, 0), 'service': (0, 0)}
			return node['assigned_checks']

		host_off = self.lsc.query_value('GET hosts\nFilter: next_check = 0\nStats: state != 999')
		svc_off = self.lsc.query_value('GET services\nFilter: next_check = 0\nStats: state != 999')

		# pollers should always run the number of checks they
		# think they should, since we never know anything about
		# their pollers. The same goes for peers without
		# configured pollers
		if node['type'] == 'poller' or not node['configured_pollers']:

			host = int(node.get('host_checks_handled', 0)) - host_off
			service = int(node.get('service_checks_handled', 0)) - svc_off
			node['assigned_checks'] = {
				'host': (host, host),
				'service': (service, service)
			}
			return node['assigned_checks']

		# for peer nodes with pollers, things get more complicated and
		# we need to query the database to see how many checks there
		# are that aren't handled by any poller and whose alphabetical
		# sorting happens to co-incide with which peer
		poller_hgs = []
		for n in mconf.configured_nodes.values():
			if n.ntype != 'poller':
				continue
			poller_hgs += n.options.get('hostgroup')

		query = 'GET hosts\nColumns: services\n'
		for hg in poller_hgs:
			query += 'Filter: groups >= '+hg+'\n'
		res = self.lsc.query_column(query)
		
		host_max = int(node.get('host_checks_handled', 0))
		service_max = int(node.get('service_checks_handled', 0))
		node['assigned_checks'] = {
			'host': (host_max - len(res), host_max),
			'service': (service_max - sum([len(x) for x in res]), service_max),
		}
		return node['assigned_checks']

	def status(self, node_name=None):
		nodes = {'total': self.global_status(), 'nodes': {}}
		for name, row in self.ni.items():
			if node_name and node_name != row['instance_name']:
				continue
			if row['state'] == 'STATE_NONE':
				continue
#			instance_id = int(row[0])
#			if instance_id == 0:
#				name = '_local.ipc'
#			else:
			nodes['nodes'][name] = {'info': row}
			node = mconf.configured_nodes.get(name, False)
			# we read ourselves, or an unconfigured node
			if not node:
				nodes['nodes'][name]['in_config'] = False
		return nodes
