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
	Given I have merlin configured for port 7000
		| type | name | port |
	And merlind listens for merlin at socket test_ipc.sock
	And I start naemon
	Then I wait for 3 seconds
	And merlind is connected to merlin
	And merlind received event CTRL_ACTIVE
