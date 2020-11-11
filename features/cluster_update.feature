@config @daemons @merlin @queryhandler
Feature: The cluster_update functionality allows to configure a specific script
	to be run when a node has an incompatible cluster configuration.

	Background: I some default configuration in naemon
		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
		And I have naemon host objects
			| use          | host_name | address   | hostgroups  |
			| default-host | something | 127.0.0.1 | pollergroup |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | something | PING        |

	Scenario: Master sends CTRL_INVALID_CLUSTER then the poller should
		execute the cluster_update command
		Given I start naemon with merlin nodes connected
			| type   | name    | port |
			| master | master1 | 4001 |
		And master1 sends event CTRL_INVALID_CLUSTER
			| version | 1 |

		And I wait for 3 second
		Then file merlin.log matches Cluster update finished successfully

	Scenario: If poller connects with wrong number of peers it should
		disconnect.
		Given I start naemon with merlin nodes connected
			| type   | name    | port | hostgroups  |
			| poller | poller1 | 4001 | pollergroup |
		And poller1 sends event CTRL_ACTIVE
			| configured_peers    | 1 |
			| configured_masters  | 2 |

		And I wait for 1 second
		Then poller1 should appear disconnected
