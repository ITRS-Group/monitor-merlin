@config @daemons @merlin @queryhandler
Feature: Module should handle conf sync with peer
	When a module realizes that another node has a config out of sync, the
	module should start configuration sync, according to the config sync
	directive in the merlin.conf

	Background: I some default configuration in naemon
		Given I have naemon host objects
			| use          | host_name | address   |
			| default-host | something | 127.0.0.1 |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | something | PING        |
		And I have merlin configured for port 7000
			| type | name | port |
			| peer | peer | 4001 |

		# This sets up the normal case, where two peers have the
		# same configuration hash. Tests will alter the hash they
		# send as they see fit to test what they're designed to
		# test.
		And I start naemon
		And node ipc have info hash config_hash at 1000
		And node ipc have expected hash config_hash at 2000
		And node peer have info hash config_hash at 3000
		And node peer have expected hash config_hash at 4000

	Scenario: Same config should be accepted, regardless of timestamp
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| config_hash        | config_hash |
			| last_cfg_change    |        4000 |
		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| config_hash        | config_hash |
			| last_cfg_change    |        1000 |
		And file config_sync.log does not match ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and lower timestamp should be denied, and synced
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             a_error |
			| last_cfg_change    |                3900 |
		Then peer is not connected to merlin
		And file config_sync.log matches ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and higher timestamp should be denied, but not synced
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             a_error |
			| last_cfg_change    |                4100 |
		Then peer is not connected to merlin
		And file config_sync.log does not match ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and same timestamp should be denied, peer started later, so push
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| start              |        2203553100.0 |
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             a_error |
			| last_cfg_change    |                4000 |
		Then peer is not connected to merlin
		And file config_sync.log matches ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and same timestamp should be denied, peer started earlier, so don't push
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| start              |                 0.0 |
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             z_error |
			| last_cfg_change    |                4000 |
		Then peer is not connected to merlin
		And file config_sync.log does not match ^push peer$
		And file config_sync.log does not match ^fetch peer$

# TODO:
# - verify no connection if "connect=no"
