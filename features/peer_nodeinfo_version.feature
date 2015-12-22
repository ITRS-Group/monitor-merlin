@config @daemons @merlin
Feature: Version compatibility between peers
	We don't provide compatibility between versions at the moment. Thus, disconnect from older versions.

	It's up to a future version to handle backward compatibility, so let the future version to disconnect if we're to old.

	Background: I have a local active daemon
		Given I have merlin configured for port 7000
			| type | name | port | connect |
			| peer | peer | 4001 | no      |

		And I start merlin
		And I wait for 1 second

		Given ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| configured_peers | 1 |

		And I wait for 1 second
		Then ipc is connected to merlin

	Scenario: Same version should be accepted
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          | 1 |
			| configured_peers | 1 |

		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| version | 1 |

	Scenario: Lower version should not be accepted
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          | 0 |
			| configured_peers | 1 |
		Then peer is not connected to merlin

	Scenario: Higher version should be accepted, up to other part to validate compatibility
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          | 2 |
			| configured_peers | 1 |
		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| version | 1 |
