@config @daemons @merlin @queryhandler
Feature: Module should handle conf sync with poller
	When a module realizes that another node has a config out of sync, the
	module should start configuration sync, according to the config sync
	directive in the merlin.conf

	Background: I some default configuration in naemon
		Given I have naemon host objects
			| use          | host_name   | address   |
			| default-host | something   | 127.0.0.1 |
			| default-host | pollerthing | 127.0.0.1 |
		And I have naemon hostgroup objects
			| hostgroup_name | alias | members     |
			| poller-group   | PG    | pollerthing |
		And I have naemon service objects
			| use             | host_name   | description |
			| default-service | something   | PING        |
			| default-service | pollerthing | PING        |
		And I have merlin configured for port 7000
			| type   | name   | port | hostgroup    |
			| poller | poller | 4001 | poller-group |

		And I start naemon
		And node ipc have info hash ipc_info_hash at 1000
		And node ipc have expected hash ipc_expected_hash at 2000
		And node poller have info hash poller_info_hash at 3000
		And node poller have expected hash poller_expected_hash at 4000

	Scenario: Same config and same timestamp should be accepted
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| config_hash        | poller_expected_hash |
			| last_cfg_change    |                 4000 |
		And I wait for 1 second
		Then poller is connected to merlin
		And poller received event CTRL_ACTIVE
			| config_hash        | poller_expected_hash |
			| last_cfg_change    |                 4000 |
		And file config_sync.log does not match ^push
		And file config_sync.log does not match ^fetch

	Scenario: Different config and lower timestamp should be denied, master pushes
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| config_hash        |              a_error |
			| last_cfg_change    |                 3900 |
		Then poller is not connected to merlin
		And file merlin.log matches CSYNC: poller poller: Checking. Time delta: -100$
		And file config_sync.log matches ^push poller$
		And file config_sync.log does not match ^fetch

	Scenario: Different config and higher timestamp should be denied, master pushes
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| config_hash        |              a_error |
			| last_cfg_change    |                 4100 |
		Then poller is not connected to merlin
		And file merlin.log matches CSYNC: poller poller: Checking. Time delta: 100$
		And file config_sync.log matches ^push poller$
		And file config_sync.log does not match ^fetch

	Scenario: Different config and same timestamp should be denied, lower config hash, treat as earlier
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| config_hash        |              a_error |
			| last_cfg_change    |                 4000 |
		Then poller is not connected to merlin
		And file merlin.log matches CSYNC: poller poller: Checking. Time delta: -1$
		And file config_sync.log matches ^push poller$
		And file config_sync.log does not match ^fetch

	Scenario: Different config and same timestamp should be denied, higher config hash, treat as later
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| config_hash        |              z_error |
			| last_cfg_change    |                 4000 |
		Then poller is not connected to merlin
		And file merlin.log matches CSYNC: poller poller: Checking. Time delta: -1$
		And file config_sync.log matches ^push poller$
		And file config_sync.log does not match ^fetch

# TODO:
# - verify no connection if "connect=no"
