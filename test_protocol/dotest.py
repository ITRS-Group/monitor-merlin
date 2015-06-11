
import socket
import struct
import time
from merlinpkt import *
from merlinnet import *

sock = socket.socket(socket.AF_UNIX)
sock.connect("/tmp/merlintest/ipc.sock")
merl = MerlinNet(sock)
merl.send(MerlinPktNodeInfo(), {
	"hdr.code": CTRL_ACTIVE
	})
merl.send(MerlinPktHostCheckData(), {
		"nebattr": 3,
		"name": "hostmonitor",
		"state.current_state": 1,
		"state.state_type": 1,
		"state.last_check": (int)(time.time()),
		"state.plugin_output": "Some output",
		"state.long_plugin_output": "Something long",
		"state.perf_data": "measure=131"
		})
merl.send(MerlinPktServiceCheckData(), {
		"nebattr": 3,
		"host_name": "servicemonitor",
		"service_description": "PING or something",
		"state.current_state": 1,
		"state.state_type": 1,
		"state.last_check": (int)(time.time()),
		"state.plugin_output": "Some output",
		"state.long_plugin_output": "Something long",
		"state.perf_data": "measure=131"
		})
