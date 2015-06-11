class MerlinNet:
	def __init__(self, sock):
		self._sock = sock

	def read(self, pkt):
		size = pkt.size()
		buffer = self._sock.recv(size)
		return pkt.unpack(buffer)

	def send(self, pkt, values):
		buf = pkt.pack(values)
		self._sock.sendall(buf)