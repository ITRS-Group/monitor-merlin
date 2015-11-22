@merlin @daemons @config
Feature: Merlind connects to peer
	Verify that merlind connects to a peer

	Also verify that the hash from module passed though ipc is forwarded
	correctly

	Background: Set up merlind connecting to peer01, mock ipc
		Given I have merlin configured for port 7000
			| type | name   | port |
			| peer | peer01 | 4001 |

		And I start merlin
		And I wait for 1 second

		And ipc connect to merlin at socket test_ipc.sock
		And ipc sends event CTRL_ACTIVE
			| configured_peers   | 1 |
			| configured_pollers | 0 |

			# Make sure it knows about the config hash
		And I wait for 1 second
		Then ipc is connected to merlin

	Scenario: Start listen to peer01, and get a connection
		Given peer01 listens for merlin at port 4001
		# Merlin reconnects every 5 seconds, have some margin.
		And I wait for 6 seconds
		Then peer01 is connected to merlin
		Then peer01 received event CTRL_ACTIVE
			| last_cfg_change             | 17                |
			| config_hash                 | my_hash           |