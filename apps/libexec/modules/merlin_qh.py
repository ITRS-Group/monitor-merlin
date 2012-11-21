import socket, select

query_handler = "/opt/monitor/var/rw/nagios.qh"

def get_merlin_nodeinfo(query_handler):
	read_size = 4096
	qh = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
	qh.connect(query_handler)
	qh.send("#merlin nodeinfo\0")
	resp = ''
	while select.select([qh], [], [], 0.25):
		out = qh.recv(read_size)
		if not out:
			break
		resp += out
	qh.close()
	return [dict([y.split('=') for y in x.split(';')]) for x in resp.split('\n') if x]
