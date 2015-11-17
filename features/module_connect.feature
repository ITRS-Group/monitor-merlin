@config @daemons @merlin
Feature: Naemon module connects to merlin daemon
	When a module starts, the module should connect to the merlind daemon using
	ipc socket. The module should take initiative to the connection.

Background: Set up naemon configuration
	Given I have naemon host objects
		| use          | host_name | address   |
		| default-host | something | 127.0.0.1 |
		| default-host | gurka     | 127.0.0.2 |
	And I have naemon service objects
		| use             | host_name | description |
		| default-service | something | PING        |
		| default-service | something | PONG        |
		| default-service | gurka     | PING        |
		| default-service | gurka     | PONG        |

Scenario: The module initiates the connetion
	Given I have config file merlin.conf
		"""
		ipc_socket = test_ipc.sock;

		log_level = info;
		use_syslog = 1;

		module {
			log_file = /dev/stdout
		}
		daemon {
			pidfile = merlin.pid;
			log_file = /dev/stdout
			import_program = /bin/false
			port = 7000;
			object_config {
				dump = /bin/false
			}
		}
		"""
	And merlind listens for merlin at socket test_ipc.sock
	And I start naemon
	Then I wait for 10 seconds
	And merlind is connected to merlin
	And merlind received event CTRL_ACTIVE
