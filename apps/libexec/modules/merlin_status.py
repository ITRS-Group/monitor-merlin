import sys, os

mod_path = os.path.dirname(os.path.abspath(__file__))
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

	def status(self, node_name=None):
		nodes = []
		for name, row in self.ni.items():
			if node_name and node_name != row['instance_name']:
				continue
			if row['state'] == 'STATE_NONE':
				continue
			nodes.append(row)
			node = mconf.configured_nodes.get(name, False)
			# we read ourselves, or an unconfigured node
			if not node:
				nodes[-1]['in_config'] = False
		return nodes
