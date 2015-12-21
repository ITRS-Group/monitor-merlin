@config @daemons @merlin
Feature: Version compatibility over IPC
	If merlin daemon and merlin module is of different versions, they shouldn't
	be able to talk to each other. The module and daemon comes in pairs, and not
	even backward compatibility should be accepted there.

	Background: I have a local active daemon
		Given I have merlin configured for port 7000
			| type | name | port | connect |

		And I start merlin
		And I wait for 1 second

	Scenario: Same version should be accepted
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 1 |

		And I wait for 1 second
		Then ipc is connected to merlin

	Scenario: Lower version should not be accepted
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 0 |
		Then ipc is not connected to merlin

	Scenario: Higher version should not be accepted
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 2 |
		Then ipc is not connected to merlin
