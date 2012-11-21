import nagios_qh

def get_merlin_nodeinfo(query_handler):
	qh = nagios_qh.nagios_qh(query_handler)
	return qh.get('#merlin nodeinfo\0')
