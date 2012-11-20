#!/usr/bin/env python
import socket

class nagios_qh:
	__slots__ = ['query_handler', 'socket', 'read_size']
	read_size = 4096
	socket = False
	query_handler = False

	def __init__(self, query_handler):
		self.query_handler = query_handler

	def query(self, query):
		"""Ask a raw query to nagios' query handler socket, return the raw
		response as a lazily generated sequence."""
		try:
			self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
			self.socket.connect(self.query_handler)

			self.socket.send(query + '\0')
			while True:
				out = self.socket.recv(self.read_size)
				if not out:
					break
				yield out
			self.socket.close()
		except socket.error, e:
			print "Couldn't connect to nagios socket: " + str(e)

	def format(self, data, rowsep='\n', pairsep=';', kvsep='='):
		"""Lazily format a response into a sequence of dicts"""
		def todict(row):
			return dict(x.split(kvsep) for x in row.split(pairsep))
		rest = ''
		for block in data:
			block = rest + block
			rows = block.split(rowsep)
			for row in rows[:-1]:
				yield todict(row)
			rest = rows[-1]
		if rest:
			yield todict(rest)

	def get(self, query, rowsep='\n', pairsep=';', kvsep='='):
		"""Ask a query to nagios' query handler socket, and return an object
		representing a "standard" nagios qh response"""
		resp = self.query(query)
		return self.format(resp, rowsep, pairsep, kvsep)
