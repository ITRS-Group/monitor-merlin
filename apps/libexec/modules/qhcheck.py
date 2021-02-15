#!/usr/bin/env python2
import socket, sys
import multiprocessing

class QhError(Exception):
	def __init__(self, errno, msg):
		self.errno, self.message = errno, msg

	def __str__(self):
		return '%d: %s' % (self.errno, self.message)

class QhClientError(QhError): pass
class QhServerError(QhError): pass
class ChannelClosed(Exception): pass
class QueryTimeout(Exception): pass

class QhCheck(object):
	def __init__(self):
		self.channels = {}
		self.pipes = {}

	def __enter__(self):
		return self

	def __exit__(self, exc_type, exc_value, traceback):
		self.drop_all()
		return exc_type is None

	def run_channel(self, channel, pipe):
		#in the child, close our copy of the parent's end of the pipe
		self.pipes[channel.address].close()
		generator = None
		while True:
			#if we're subscribing already, we want to check if there's
			#any new queries to submit before reading, lest we hang
			if not generator or pipe.poll():
				try:
					query = pipe.recv()
					generator = channel.query(query, timeout=0.1)
				except (EOFError, KeyboardInterrupt):
					pipe.close()
					break

			if generator:
				try:
					for response in generator:
						#response could be None, if the timeout expired
						if response != None:
							pipe.send(response)
						else:
							raise QueryTimeout
				except (IOError, KeyboardInterrupt, StopIteration):
					#that's all folks!
					#TODO: communicate errors (4xx-5xx) if the pipe isn't broken as well
					pipe.close()
					break
				except QueryTimeout:
					pass


	def query(self, channel, query):
		self.pipes[channel].send(query)

	def add_channel(self, channel):
		if channel.address in self.channels:
			raise ValueError('%s already registered' % channel.address)

		our_end, their_end = multiprocessing.Pipe()
		self.pipes[channel.address] = our_end
		self.channels[channel.address] = multiprocessing.Process(name=channel.address, target=self.run_channel, args=(channel, their_end))
		self.channels[channel.address].start()
		#close the child end
		their_end.close()


	def get_response(self, channel):
		try:
			return self.pipes[channel].recv()
		except EOFError:
			self.pipes[channel].close()
			raise ChannelClosed

	def drain_responses(self, channel):
		responses = []
		while self.pipes[channel].poll():
			responses.append(self.get_response(channel))
		return responses

	def get_responses(self, channel, num_responses=0):
		if num_responses <= 0:
			self.drain_responses(channel)

		responses = []
		for _ in range(0, num_responses):
			responses.append(self.get_response(channel))
		return responses

	def drop_channel(self, channel):
		self.pipes[channel].close()
		self.channels[channel].join()

	def drop_all(self):
		for c in self.channels.keys():
			self.drop_channel(c)

class ChannelOptions(object):
	def __init__(self, linesep='\n', pairsep=';', kvsep='='):
		self.linesep = linesep
		self.pairsep = pairsep
		self.kvsep = kvsep

class QhChannel(object):
	def __init__(self, address, path='/opt/monitor/var/rw/nagios.qh', options=None, subscribe=True):
		if not options:
			options = ChannelOptions()

		self.options = options
		self.address = address.strip()
		self.bufsize = 4096
		self.query_handler = path
		self.subscribe = subscribe
		self.QUERY_FORMAT = '@%s %s\0' if self.subscribe else '#%s %s\0'

	def query(self, query, timeout=None):
		self.qh = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
		self.qh.connect(self.query_handler)
		self._isclosed = False
		if self._isclosed:
			raise Exception('Cannot query closed channel %s' % self.address)

		return self._query(query, timeout)

	def error(self, response):
		tokens = response.raw.strip().split(':')
		if len(tokens) == 2:
			try:
				error_code = int(tokens[0])
			except ValueError:
				#not an error code, abort
				return
			message = tokens[1].strip()
			if error_code in range(400, 415):
				#Client error
				raise QhClientError(error_code, message)
			elif error_code in range(500, 506):
				#Server error
				raise QhServerError(error_code, message)

	def _query(self, query, timeout):
		query = self.QUERY_FORMAT % (self.address, query)
		self.qh.send(query)
		self.qh.settimeout(timeout)
		responses = []
		remainder = ''
		while True:
			try:
				read = self.qh.recv(self.bufsize)
			except socket.timeout:
				yield
				continue

			if not read:
				try:
					if len(responses) > 0:
						self.error(responses[-1]) # could be that the last response was an error
				except QhError:
					sys.stderr.write("%s: Error occured after query `%s'\n" % (self.address, query))
					raise
				raise StopIteration

			responses, remainder = self.parse_responses(read, remainder)
			for response in responses:
				yield response

	def parse_responses(self, buf, initial_buffer=''):
		responses = []
		if self.options.linesep in (None, ''):
			responses.append(Response(initial_buffer + buf, self.options.pairsep, self.options.kvsep))
			buf = ''
		else:
			while self.options.linesep in buf:
				#one Response/newline
				response_str, buf = buf.split(self.options.linesep, 1)
				if len(response_str) > 0:
					responses.append(Response(initial_buffer + response_str,
							   self.options.pairsep, self.options.kvsep))
				initial_buffer = ''

		return (responses, buf)


	def close(self):
		if self.qh:
			self.qh.close()
			self.qh = None
			self._isclosed = True

class Response(object):
	def __init__(self, response_str, pairsep=None, kvsep=None):
		self.raw = response_str
		self._response_dict = {}
		if not any([x in (None, '') for x in (pairsep, kvsep)]):
			self._response_dict = dict([y.split(kvsep, 1) for y in response_str.strip().split(pairsep) if len(y) > 0])
		else:
			self._response_dict = {'value': response_str}

	def __str__(self):
		return str(self._response_dict)

	def __repr__(self):
		s = ''
		for key, val in self._response_dict.iteritems():
			s += '%s: %s\n' % (key, val)

		return s

	def __getattr__(self, name):
		if name == '_response_dict': raise AttributeError

		if self._response_dict and name in self._response_dict:
			return self._response_dict[name]
		raise AttributeError


def run_tests():
	#ECHO
	echo_opt = ChannelOptions('', '', '')
	qc_echo = QhChannel('echo', echo_opt, subscribe=False)
	echostr = 'abcdefghijklmnopqrstuvwxyz'
	responses = []
	for response in qc_echo.query(echostr):
		responses.append(response)
		assert echostr == responses[0].value, 'response "%s" doesn\'t equal original message "%s"' % (str(responses[0].value), echostr)

	qc_echo.close()

	#MERLIN - oneshot
	qc_merlin = QhChannel('merlin', subscribe=False)
	responses = []
	responses.append(qc_merlin.query('cbstats').next())
	assert responses != [], 'expected a response but got none'
	qc_merlin.close()

	#MERLIN - subscribed
	qc_merlin = QhChannel('merlin')
	responses = []
	responses.append(qc_merlin.query('nodeinfo').next())
	assert len(responses) == 1, 'expected one response but got %d' % len(responses)
	qc_merlin.close()

	#NERD
	qc_nerd = QhChannel('nerd', ChannelOptions('\n', '', ''))

	responses = []
	generator = qc_nerd.query('subscribe hostchecks')
	responses.append(generator.next())
	responses.append(generator.next())
	assert len(responses) == 2
	qc_nerd.close()

	#Multi-channel
	with QhCheck() as qhcheck:
		qhcheck.add_channel(QhChannel('echo', echo_opt))
		qhcheck.query('echo', echostr)
		response = qhcheck.get_response('echo')
		assert echostr == response.raw
		assert echostr == response.value

		#send 3 one shot
		qhcheck.query('echo', echostr + '1')
		qhcheck.query('echo', echostr + '2')
		qhcheck.query('echo', echostr + '3')
		#receive 3
		responses = qhcheck.get_responses('echo', 3)
		assert len(responses) == 3

		qhcheck.add_channel(QhChannel('nerd', ChannelOptions('\n', '', '')))
		qhcheck.query('nerd', 'subscribe servicechecks')
		responses = []
		while len(responses) < 3:
			responses.append(qhcheck.get_response('nerd'))

		assert len(responses) == 3

	print '='*40
	print '%s: All tests passed!' % __file__
	print '='*40

if __name__ == '__main__':
	if len(sys.argv) > 1:
		if sys.argv[1] == 'test':
			run_tests()

	else:
		with QhCheck() as qhcheck:
			user_query = raw_input('Query: ')
			address, query = user_query.split(' ', 1)
			if address[0] not in ('#', '@'):
				raise Exception('Prefix channel with # or @ for oneshot or subscription mode')

			subscription = address[0] == '@'
			address = address[1:]
			channel = QhChannel(address, subscribe=subscription)
			qhcheck.add_channel(channel)
			qhcheck.query(address, query)
			try:
				if subscription:
					response = ''
					while response != None:
						response = qhcheck.get_response(address)
						print response
				else:
					print qhcheck.get_response(address)
			except KeyboardInterrupt:
				print 'Goodbye.'



