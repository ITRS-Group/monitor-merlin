@config @daemons @merlin @queryhandler
Feature: Version compatibility between peers
	We don't provide compatibility between versions at the moment.
	Thus, disconnect from older versions.

	It's up to a future version to handle backward compatibility,
	so let the future version to disconnect if we're to old.

	Background: I some default configuration in naemon
		Given I have naemon host objects
			| use          | host_name | address   |
			| default-host | something | 127.0.0.1 |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | something | PING        |
		And I have merlin configured for port 7000
			| type | name | port | connect |
			| peer | peer | 4001 | no      |

		And I start naemon
		And node ipc have info hash config_hash at 1000
		And node ipc have expected hash config_hash at 2000
		And node peer have info hash config_hash at 3000
		And node peer have expected hash config_hash at 4000

	Scenario: Same version should be accepted
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          |           1 |
			| configured_peers |           1 |
			| config_hash      | config_hash |

		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| version | 1 |

	Scenario: Lower version should not be accepted
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          |           0 |
			| configured_peers |           1 |
			| config_hash      | config_hash |
		Then peer is not connected to merlin

	Scenario: Higher version should be accepted, up to other part to validate compatibility
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| version          |           2 |
			| configured_peers |           1 |
			| config_hash      | config_hash |
		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| version | 1 |
