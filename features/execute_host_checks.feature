@config @daemons @merlin @queryhandler @livestatus
Feature: Module should handle execute_host_checks sync with peer
	When a module notices that execute_host_checks has changed value
	the module should notify its peers with the new value

	Scenario: Disabling active checks should propagate to peer
		Given I start naemon with merlin nodes connected
			| type | name      | port |
			| peer | the_peer  | 4001 |
			| peer | the_peer2 | 4002 |
		When I submit the following livestatus query
			| GET status                   |
			| Columns: execute_host_checks |
		Then I should see the following livestatus response
			| execute_host_checks |
			|                   1 |

		When I send naemon command STOP_EXECUTING_HOST_CHECKS

		When I submit the following livestatus query
			| GET status                   |
			| Columns: execute_host_checks |
		Then I should see the following livestatus response
			| execute_host_checks |
			|                   0 |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And the_peer2 received event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
