@config @daemons @merlin
Feature: Version compatibility over IPC
	If merlin daemon and merlin module is of different versions, they shouldn't be able to talk to each other. The module and daemon comes in pairs.

	It's up to a future version to handle backward compatibility, so let the future version to disconnect if we're to old.

	Background: I have a local active daemon
		Given I have merlin configured for port 7000
			| type | name | port | connect |

		And I start merlin
		And I wait for 1 second

	@unreliable_el8
	Scenario: Same version should be accepted
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 1 |
		And I wait for 1 second
		Then ipc is connected to merlin

	@unreliable_el8
	Scenario: Lower version should not be accepted
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 0 |
		Then ipc is not connected to merlin

	@unreliable_el8
	Scenario: Higher version should be accepted, up to other version to handle backward compatibility
		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| version | 2 |
		And I wait for 1 second
		Then ipc is connected to merlin
