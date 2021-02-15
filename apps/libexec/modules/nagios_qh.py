#!/usr/bin/env python2
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
		if not query:
			print "Missing query argument"
			return
		try:
			self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
			self.socket.connect(self.query_handler)

			self.socket.send(query + '\0')
			while True:
				try:
					out = self.socket.recv(self.read_size)
					if not out:
						break
					yield out
				except KeyboardInterrupt:
					print "Good bye."
					break
			self.socket.close()
		except socket.error, e:
			print "Couldn't connect to nagios socket %s: %s" % (self.query_handler, str(e))

	def format(self, data, rowsep='\n', pairsep=';', kvsep='='):
		"""Lazily format a response into a sequence of dicts"""
		for row in filter(None, ''.join(data).split(rowsep)):
			if row == "404: merlin: No such handler":
				print("ERROR: Could not get nodeinfo. See /var/log/op5/merlin/neb.log for more information")
				yield -1
				break
			else:
				yield dict(x.split(kvsep, 1) for x in row.split(pairsep))

	def get(self, query, rowsep='\n', pairsep=';', kvsep='='):
		"""Ask a query to nagios' query handler socket, and return an object
		representing a "standard" nagios qh response"""
		resp = self.query(query)
		return self.format(resp, rowsep, pairsep, kvsep)
