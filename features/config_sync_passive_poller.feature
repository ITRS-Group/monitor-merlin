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
			| type   | name   | port | fetch                  |
			| master | master | 4001 | mon oconf fetch master |

		When I start naemon
		Then node ipc have info hash ipc_info_hash at 1000
		And node ipc have expected hash ipc_expected_hash at 2000
		And node master have info hash poller_info_hash at 3000
		And node master have expected hash poller_expected_hash at 4000

	Scenario: Same config and same timestamp should be accepted
		Given master connect to merlin at port 7000 from port 11001
		And master sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| last_cfg_change    |                 4000 |
		When I wait for 1 second
		Then master is connected to merlin
		And master received event CTRL_ACTIVE
			| last_cfg_change    |                 1000 |
		And file config_sync.log does not match ^push
		And file merlin.log does not match fetch triggered

	Scenario: Different config and lower timestamp should be denied, poller should fetch if configured
		Given master connect to merlin at port 7000 from port 11001
		And master sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| last_cfg_change    |                 3900 |
		When I wait for 1 second
		Then master is not connected to merlin
		And file merlin.log matches fetch triggered
		And file config_sync.log does not match ^push

	Scenario: Different config and higher timestamp should be denied, poller should fetch if configured
		Given master connect to merlin at port 7000 from port 11001
		And master sends event CTRL_ACTIVE
			| configured_masters |                    1 |
			| last_cfg_change    |                 4100 |
		When I wait for 1 second
		Then master is not connected to merlin
		And file merlin.log matches fetch triggered
		And file config_sync.log does not match ^push

	Scenario: poller should fetch if master sends CTRL_FETCH
		Given master connect to merlin at port 7000 from port 11001
		And master sends event CTRL_FETCH
			| configured_masters |                    1 |
		When I wait for 1 second
		Then file merlin.log matches fetch triggered
