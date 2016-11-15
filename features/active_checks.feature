@config @daemons @merlin @queryhandler @livestatus
Feature: Active checks
	Each active check should only be executed on a single machine; either one
	of several peers, or by a node in a poller group

	Background: Set up naemon configuration
		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		And I have naemon host objects
			| use          | host_name | address   | max_check_attempts | hostgroups  | active_checks_enabled | check_interval |
			| default-host | hostA     | 127.0.0.1 | 2                  | pollergroup |                     1 |              1 |
			| default-host | hostB     | 127.0.0.1 | 2                  | pollergroup |                     1 |              1 |
		And I have naemon service objects
			| use             | host_name | description | active_checks_enabled | check_interval |
			| default-service | hostA     | PONG        |                     1 |              1 |
			| default-service | hostB     | PONG        |                     1 |              1 |
		And I have naemon config interval_length set to 1
		And I have naemon config cached_host_check_horizon set to 0
		And I have naemon config cached_service_check_horizon set to 0

	Scenario: A non-distributed machine should execute all host and service checks
		Given I start naemon with merlin nodes connected
			| type   | name       | port |

		When I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$

	Scenario: A peered environment, the other node is started prior to this, and thus, handles first checks
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |
		# Peer id:s is allocated in falling start time, thus my_peer is peer 0, local is peer 1

		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log does not match ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log does not match ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$

	Scenario: In a poller environment, master doesn't execute poller checks
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups  |
			| poller | my_poller  | 4001 | pollergroup |

		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$


	Scenario: In a poller environment, master execute checks not owned by poller
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |
			| poller | my_poller  | 4001 | emptygroup |

		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
