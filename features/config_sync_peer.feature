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

		And I start naemon
		And node ipc have info hash ipc_info_hash at 1000
		And node ipc have expected hash ipc_expected_hash at 2000
		And node peer have info hash peer_info_hash at 3000
		And node peer have expected hash peer_expected_hash at 4000

	Scenario: Same config and same timestamp should be accepted
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |                  1 |
			| configured_pollers |                  0 |
			| config_hash        | peer_expected_hash |
			| last_cfg_change    |               4000 |
		And I wait for 1 second
		Then peer is connected to merlin
		And peer received event CTRL_ACTIVE
			| configured_peers   |             1 |
			| configured_pollers |             0 |
			| config_hash        | ipc_info_hash |
			| last_cfg_change    |          1000 |
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
		And file merlin.log matches CSYNC: peer peer: Checking. Time delta: -100$
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
		And file merlin.log matches CSYNC: peer peer: Checking. Time delta: 100$
		And file config_sync.log does not match ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and same timestamp should be denied, lower config hash, treat as earlier
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             a_error |
			| last_cfg_change    |                4000 |
		Then peer is not connected to merlin
		And file merlin.log matches CSYNC: peer peer: Checking. Time delta: -1$
		And file config_sync.log matches ^push peer$
		And file config_sync.log does not match ^fetch peer$

	Scenario: Different config and same timestamp should be denied, higher config hash, treat as later
		Given peer connect to merlin at port 7000 from port 11001
		And peer sends event CTRL_ACTIVE
			| configured_peers   |                   1 |
			| configured_pollers |                   0 |
			| config_hash        |             z_error |
			| last_cfg_change    |                4000 |
		Then peer is not connected to merlin
		And file merlin.log matches CSYNC: peer peer: Checking. Time delta: 1$
		And file config_sync.log does not match ^push peer$
		And file config_sync.log does not match ^fetch peer$

# TODO:
# - verify no connection if "connect=no"