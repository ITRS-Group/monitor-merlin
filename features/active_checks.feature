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
		And I have naemon config execute_host_checks set to 1
		And I have naemon config execute_service_checks set to 1

	# Non distributed system

	Scenario: A non-distributed machine should execute all host and service checks
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$


	Scenario: Disabling checks in a non-distributed machine runtime should
		not execute any more checks.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1

		When I send naemon command STOP_EXECUTING_HOST_CHECKS
		And I send naemon command STOP_EXECUTING_SVC_CHECKS
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$
		And naemon status execute_host_checks should be set to 0
		And naemon status execute_service_checks should be set to 0


	Scenario: A non-distributed machine should execute NO checks if they are
		disabled in the naemon configuration.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |

		When I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$


	Scenario: Enabling checks in a non-distributed machine runtime should
		start executing checks.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |

		When I send naemon command START_EXECUTING_HOST_CHECKS
		And I send naemon command START_EXECUTING_SVC_CHECKS
		And I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1


	# Peered system

	Scenario: In a peered environment, the other node is started prior to this,
		and thus, handles first checks.

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


	Scenario: Disabling checks in a peered system during runtime should
		not execute any more checks.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1

		When I send naemon command STOP_EXECUTING_HOST_CHECKS
		And I send naemon command STOP_EXECUTING_SVC_CHECKS
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$
		And my_peer received event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And my_peer received event EXTERNAL_COMMAND
			| command_type   | 36                        |
			| command_string | STOP_EXECUTING_SVC_CHECKS |
		And naemon status execute_host_checks should be set to 0
		And naemon status execute_service_checks should be set to 0


	Scenario: A peered machine should execute NO checks if they are
		disabled in the naemon configuration.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |

		When I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$


	Scenario: Enabling checks in a peered machine during runtime should
		start executing checks.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |
		# Peer id:s is allocated in falling start time, thus my_peer is peer 0, local is peer 1

		When I send naemon command START_EXECUTING_HOST_CHECKS
		And I send naemon command START_EXECUTING_SVC_CHECKS
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log does not match ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log does not match ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
		And my_peer received event EXTERNAL_COMMAND
			| command_type   | 88                          |
			| command_string | START_EXECUTING_HOST_CHECKS |
		And my_peer received event EXTERNAL_COMMAND
			| command_type   | 35                         |
			| command_string | START_EXECUTING_SVC_CHECKS |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1


	Scenario: In a peered environment, receiving a start checks command
		should start checking.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |

		When my_peer sends event EXTERNAL_COMMAND
			| command_type   | 88                          |
			| command_string | START_EXECUTING_HOST_CHECKS |
		And my_peer sends event EXTERNAL_COMMAND
			| command_type   | 35                         |
			| command_string | START_EXECUTING_SVC_CHECKS |
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log does not match ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log does not match ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1


	Scenario: In a peered environment, receiving a stop checks commmands
		should stop checking.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | my_peer    | 4001 |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1

		When my_peer sends event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And my_peer sends event EXTERNAL_COMMAND
			| command_type   | 36                        |
			| command_string | STOP_EXECUTING_SVC_CHECKS |
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$
		And naemon status execute_host_checks should be set to 0
		And naemon status execute_service_checks should be set to 0

	# Poller system

	Scenario: In a poller environment, master doesn't execute poller checks

		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups  |
			| poller | my_poller  | 4001 | pollergroup |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1
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


	Scenario: As a poller I should execute checks if I own the object

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |
		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$


	# Questionable behaviour, should a poller be able to disable active checks?
	Scenario: Disabling checks in a poller during runtime should
		not execute any more checks.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1

		When I send naemon command STOP_EXECUTING_HOST_CHECKS
		And I send naemon command STOP_EXECUTING_SVC_CHECKS
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$
		And naemon status execute_host_checks should be set to 0
		And naemon status execute_service_checks should be set to 0


	# Questionable behaviour, should a poller be able to disable active checks?
	Scenario: A poller should execute NO checks if they are
		disabled in the naemon configuration.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |
		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$


	Scenario: Enabling checks in a poller during runtime should
		start executing checks.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |

		When I send naemon command START_EXECUTING_HOST_CHECKS
		And I send naemon command START_EXECUTING_SVC_CHECKS
		And I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1


	Scenario: In a poller, receiving a start checks command
		should start checking.

		Given I have naemon config execute_service_checks set to 0
		And I have naemon config execute_host_checks set to 0
		And I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |

		When my_master sends event EXTERNAL_COMMAND
			| command_type   | 88                          |
			| command_string | START_EXECUTING_HOST_CHECKS |
		And my_master sends event EXTERNAL_COMMAND
			| command_type   | 35                         |
			| command_string | START_EXECUTING_SVC_CHECKS |
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log matches ^check host hostA$
		And file checks.log matches ^check host hostB$
		And file checks.log matches ^check service hostA PONG$
		And file checks.log matches ^check service hostB PONG$
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1


	Scenario: In a poller, receiving a stop checks commmands
		should stop checking.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |
		And naemon status execute_host_checks should be set to 1
		And naemon status execute_service_checks should be set to 1

		When my_master sends event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And my_master sends event EXTERNAL_COMMAND
			| command_type   | 36                        |
			| command_string | STOP_EXECUTING_SVC_CHECKS |
		And I have an empty file checks.log
		And I wait for 5 seconds

		Then file checks.log has 0 lines matching ^check .*$
		And naemon status execute_host_checks should be set to 0
		And naemon status execute_service_checks should be set to 0


	# Command propagation
	# Enabling/Disabling active checks in runtime testing below

	Scenario: Start check commands should propagate to peers and poller.

		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |
			| peer   | my_peer    | 4001 | ignore     |
			| poller | my_poller  | 4002 | emptygroup |

		When I send naemon command START_EXECUTING_HOST_CHECKS
		And I send naemon command START_EXECUTING_SVC_CHECKS

		Then my_peer received event EXTERNAL_COMMAND
			| command_type   | 88                          |
			| command_string | START_EXECUTING_HOST_CHECKS |
		And my_peer received event EXTERNAL_COMMAND
			| command_type   | 35                         |
			| command_string | START_EXECUTING_SVC_CHECKS |
		And my_poller received event EXTERNAL_COMMAND
			| command_type   | 88                          |
			| command_string | START_EXECUTING_HOST_CHECKS |
		And my_poller received event EXTERNAL_COMMAND
			| command_type   | 35                         |
			| command_string | START_EXECUTING_SVC_CHECKS |

	# Should commands propagate from poller to master?
	# If so, we should test it here.

	Scenario: A non-distrubuted node shouldn't execute checks for
		any objects in ipc_blocked_hostgroups.
		Given I have merlin config ipc_blocked_hostgroups set to pollergroup
		And I start naemon with merlin nodes connected
			| type   | name       | port |
		And I have an empty file checks.log

		When I wait for 5 seconds

		Then file checks.log does not match ^check host hostA$
		And file checks.log does not match ^check host hostB$
		And file checks.log does not match ^check service hostA PONG$
		And file checks.log does not match ^check service hostB PONG$
		And file merlin.log matches Blocking check execution of hostA
		And file merlin.log matches Blocking check execution of hostA;PONG
		And file merlin.log matches Blocking check execution of hostB
		And file merlin.log matches Blocking check execution of hostB;PONG
