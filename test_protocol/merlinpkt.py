import struct

CTRL_PACKET   = 0xffff
ACK_PACKET    = 0xfffe
NAK_PACKET    = 0xfffd
RUNCMD_PACKET = 0xfffc

CTRL_GENERIC  = 0
CTRL_PULSE    = 1
CTRL_INACTIVE = 2
CTRL_ACTIVE   = 3
CTRL_PATHS    = 4
CTRL_STALL    = 5
CTRL_RESUME   = 6
CTRL_STOP     = 7
MAGIC_NONET   = 0xffff


NEBCALLBACK_PROCESS_DATA = 0
NEBCALLBACK_TIMED_EVENT_DATA = 1
NEBCALLBACK_LOG_DATA = 2
NEBCALLBACK_SYSTEM_COMMAND_DATA = 3
NEBCALLBACK_EVENT_HANDLER_DATA = 4
NEBCALLBACK_NOTIFICATION_DATA = 5
NEBCALLBACK_SERVICE_CHECK_DATA = 6
NEBCALLBACK_HOST_CHECK_DATA = 7
NEBCALLBACK_COMMENT_DATA = 8
NEBCALLBACK_DOWNTIME_DATA = 9
NEBCALLBACK_FLAPPING_DATA = 10
NEBCALLBACK_PROGRAM_STATUS_DATA = 11
NEBCALLBACK_HOST_STATUS_DATA = 12
NEBCALLBACK_SERVICE_STATUS_DATA = 13
NEBCALLBACK_ADAPTIVE_PROGRAM_DATA = 14
NEBCALLBACK_ADAPTIVE_HOST_DATA = 15
NEBCALLBACK_ADAPTIVE_SERVICE_DATA = 16
NEBCALLBACK_EXTERNAL_COMMAND_DATA = 17
NEBCALLBACK_AGGREGATED_STATUS_DATA = 18
NEBCALLBACK_RETENTION_DATA = 19
NEBCALLBACK_CONTACT_NOTIFICATION_DATA = 20
NEBCALLBACK_CONTACT_NOTIFICATION_METHOD_DATA = 21
NEBCALLBACK_ACKNOWLEDGEMENT_DATA = 22
NEBCALLBACK_STATE_CHANGE_DATA = 23
NEBCALLBACK_CONTACT_STATUS_DATA = 24
NEBCALLBACK_ADAPTIVE_CONTACT_DATA = 25

class MerlinPkt:
	_fmt_header = [
		("Q", "hdr.id", 0x005456454e4c524d), # "MRLNEVT\0"
		("H", "hdr.protocol", 0),
		("H", "hdr.type", CTRL_PACKET),
		("H", "hdr.code", CTRL_ACTIVE),
		("H", "hdr.selection", 0),
		("L", "hdr.len", 0),
		("44s", None, "") # timeval sent + padding to 64 byte
		]
	_fmt_data = []
	_strings = []

	def __init__(self):
		self._values = {}
		self._value_order = []

		header_fmt = ""
		for (pd, name, value) in self._fmt_header:
			header_fmt += pd
			if name != None:
				self._values[name] = value
			self._value_order.append(name)

		data_fmt = ""
		for (pd, name, value) in self._fmt_data:
			data_fmt += pd
			if name != None:
				self._values[name] = value
			self._value_order.append(name)

		self._fmt = "<" + header_fmt + data_fmt

		self._size = struct.calcsize(self._fmt)
		self._size_header = struct.calcsize("<" + header_fmt)
		self._size_data = struct.calcsize("<" + data_fmt)

	def size(self):
		return struct.calcsize(self._fmt)

	def pack(self, values):
		myvalues = self._values.copy()
		myvalues.update(values)

		string_baseoffset = self._size_data

		stringbindata = ""
		for stringname in self._strings:
			stringdata = myvalues[stringname]
			offset = string_baseoffset + len(stringbindata)
			myvalues[stringname] = offset
			stringbindata += stringdata + "\0";

		myvalues["hdr.len"] = self._size_data + len(stringbindata)

		args = []
		for argname in self._value_order:
			if argname == None:
				args.append("")
			else:
				args.append(myvalues[argname])

		return struct.pack(self._fmt, *args) + stringbindata

	def unpack(self, buffer):
		args = struct.unpack(self._fmt, buffer[0:self._size])
		values = {}
		for value, argname in zip(args, self._value_order):
			if argname != None:
				values[argname] = value
		return values

class MerlinPktNodeInfo(MerlinPkt):
	_fmt_data = [
		("L", "version", 1),
		("L", "word_size", 64), # bits per register (sizeof(void *) * 8)
		("L", "byte_order", 1234), # 1234 = little, 4321 = big, ...
		("L", "object_structure_version", 402),
		#struct timeval start; # module (or daemon) start time
		("16s", None, ""),
		("Q", "last_cfg_change", 0), # when config was last changed
		("20s", "config_hash", "a cool config hash"), # SHA1 hash of object config hash
		("L", "peer_id", 0), # self-assigned peer-id
		("L", "active_peers", 0),
		("L", "configured_peers", 0),
		("L", "active_pollers", 0),
		("L", "configured_pollers", 0),
		("L", "active_masters", 0),
		("L", "configured_masters", 0),
		("L", "host_checks_handled", 0),
		("L", "service_checks_handled", 0),
		("L", "monitored_object_state_size", 0)
		]

	def __init__(self):
		MerlinPkt.__init__(self)
		self._values["hdr.type"] = CTRL_PACKET
		self._values["hdr.code"] = CTRL_ACTIVE

class MerlinPktCheckData(MerlinPkt):
	_fmt_data = [
		("i", "nebattr", 0),
		("4s", None, ""), # padding

# struct monitored_object_state {
		("i", "state.initial_state", 0),
		("i", "state.flap_detection_enabled", 0),
		("d", "state.low_flap_threshold", 0.0),
		("d", "state.high_flap_threshold", 0.0),
		("i", "state.check_freshness", 1),
		("i", "state.freshness_threshold", 60),
		("i", "state.process_performance_data", 0),
		("i", "state.checks_enabled", 1),
		("i", "state.accept_passive_checks", 1),
		("i", "state.event_handler_enabled", 1),
		("i", "state.obsess", 0),
		("i", "state.problem_has_been_acknowledged", 1),
		("i", "state.acknowledgement_type", 0),
		("i", "state.check_type", 0),
		("i", "state.current_state", 0),
		("i", "state.last_state", 0),
		("i", "state.last_hard_state", 0),
		("i", "state.state_type", 0),
		("i", "state.current_attempt", 0),
		("4s", None, ""), # padding
		("Q", "state.hourly_value", 0),
		("Q", "state.current_event_id", 0),
		("Q", "state.last_event_id", 0),
		("Q", "state.current_problem_id", 0),
		("Q", "state.last_problem_id", 0),
		("d", "state.latency", 0.023),
		("d", "state.execution_time", 0.054),
		("i", "state.notifications_enabled", 0),
		("4s", None, ""), # padding
		("q", "state.last_notification", 0),
		("q", "state.next_notification", 0),
		("q", "state.next_check", 0),
		("i", "state.should_be_scheduled", 1),
		("4s", None, ""), # padding
		("q", "state.last_check", 0),
		("q", "state.last_state_change", 0),
		("q", "state.last_hard_state_change", 0),
		("q", "state.last_time_up", 0),
		("q", "state.last_time_down", 0),
		("q", "state.last_time_unreachable", 0),
		("i", "state.has_been_checked", 1),
		("i", "state.current_notification_number", 0),
		("Q", "state.current_notification_id", 0),
		("i", "state.check_flapping_recovery_notification", 0),
		("i", "state.scheduled_downtime_depth", 0),
		("i", "state.pending_flex_downtime", 0),
		# ("21i", "state.state_history", 0), # flap detection
		("84s", "state_history", ""),
		("i", "state.state_history_index", 0),
		("i", "state.is_flapping", 0),
		("Q", "state.flapping_comment_id", 0),
		("d", "state.percent_state_change", 0.0),
		("Q", "state.modified_attributes", 0),
		("i", "state.notified_on", 0),
		("4s", None, ""), # padding
		("Q", "state.plugin_output", ""), # char *
		("Q", "state.long_plugin_output", ""), # char *
		("Q", "state.perf_data", ""), # char *
		]


class MerlinPktHostCheckData(MerlinPktCheckData):
	_fmt_data = MerlinPktCheckData._fmt_data + [
		("Q", "name", "") # char *
		]

	_strings = [
			"name",
			"state.plugin_output",
			"state.long_plugin_output",
			"state.perf_data"
			]

	def __init__(self):
		MerlinPkt.__init__(self)
		self._values["hdr.type"] = NEBCALLBACK_HOST_CHECK_DATA



class MerlinPktServiceCheckData(MerlinPktCheckData):
	_fmt_data = MerlinPktCheckData._fmt_data + [
		("Q", "host_name", ""), # char *
		("Q", "service_description", "") # char *
		]

	_strings = [
			"host_name",
			"service_description",
			"state.plugin_output",
			"state.long_plugin_output",
			"state.perf_data"
			]

	def __init__(self):
		MerlinPkt.__init__(self)
		self._values["hdr.type"] = NEBCALLBACK_SERVICE_CHECK_DATA
