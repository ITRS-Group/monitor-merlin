import nagios_qh

def nodeinfo_sorter(a, b):
	if a['name'] > b['name']:
		return 1
	if b['name'] > a['name']:
		return -1
	return 0

def get_merlin_nodeinfo(query_handler):
	qh = nagios_qh.nagios_qh(query_handler)
	ninfo = []
	for info in qh.get('#merlin nodeinfo\0'):
		ninfo.append(info)
	ninfo.sort(nodeinfo_sorter)
	return ninfo
