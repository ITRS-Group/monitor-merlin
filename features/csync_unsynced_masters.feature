@config @daemons @merlin @queryhandler
Feature: Handle config sync to pollers from unsynced masters gracefully
	If a master disagrees with the poller's configuration, it
	should only push its own generated config to the poller if
	the poller's configuration is older than what we have in
	order to prevent ping-pong pushing from several masters
	when the reload-cascade starts, or when master nodes get
	their object config out of sync.

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
			| peer   | peer1  | 4002 | unused       |
			| poller | poller | 4001 | poller-group |

		And I start naemon
		And node ipc have info hash ipc_info_hash at 1000
		And node ipc have expected hash ipc_expected_hash at 2000
		And node poller have info hash poller_info_hash at 3000
		And node poller have expected hash poller_expected_hash at 4000

	Scenario: Same config and same timestamp should be accepted
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    2 |
			| config_hash        | poller_expected_hash |
			| last_cfg_change    |                 4000 |
		And I wait for 1 second
		Then poller is connected to merlin
		And poller received event CTRL_ACTIVE
			| config_hash        |        ipc_info_hash |
			| last_cfg_change    |                 1000 |
		And file config_sync.log does not match ^push
		And file config_sync.log does not match ^fetch

	Scenario: Different config and lower timestamp should be denied, master pushes
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    2 |
			| config_hash        |              a_error |
			| last_cfg_change    |                 3900 |
		Then poller is not connected to merlin
		And file config_sync.log matches ^push poller$
		And file config_sync.log does not match ^fetch

	Scenario: Different config and higher timestamp should be denied, no push
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| configured_masters |                    2 |
			| config_hash        |              a_error |
			| last_cfg_change    |                 4100 |
		Then poller is not connected to merlin
		And file config_sync.log does not match ^push poller$
		And file config_sync.log does not match ^fetch

	Scenario: Different config and same timestamp should be denied, no push
		Given poller connect to merlin at port 7000 from port 11001
		And poller sends event CTRL_ACTIVE
			| start              |                  0.0 |
			| configured_masters |                    2 |
			| config_hash        |              z_error |
			| last_cfg_change    |                 4000 |
		Then poller is not connected to merlin
		And file config_sync.log does not match ^push poller$
		And file config_sync.log does not match ^fetch

# TODO:
# - verify no connection if "connect=no"
