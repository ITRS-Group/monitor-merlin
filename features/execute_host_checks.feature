@config @daemons @merlin @queryhandler @livestatus
Feature: Module should handle execute_host_checks sync with peer
	When a module notices that execute_host_checks has changed value
	the module should notify its peers with the new value

	Scenario: Disabling active checks should propagate to peer
		Given I have naemon config execute_host_checks set to 1
		And I start naemon with merlin nodes connected
			| type | name      | port |
			| peer | the_peer  | 4001 |
			| peer | the_peer2 | 4002 |
		And naemon status execute_host_checks should be set to 1
		When I send naemon command STOP_EXECUTING_HOST_CHECKS
		Then naemon status execute_host_checks should be set to 0

		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And the_peer2 received event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |

	Scenario: Receiving an external command through merlin
		If a command is received through merlin, it shouldn't
		propagate to other nodes, so loops doesn't occur, where
		node A sends to B, sends to C and back to A.

		However, the command should be executed by the node.

		Given I have naemon config execute_host_checks set to 1
		And I start naemon with merlin nodes connected
			| type | name      | port |
			| peer | the_peer  | 4001 |
			| peer | the_peer2 | 4002 |
		And naemon status execute_host_checks should be set to 1
		When the_peer sends event EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		Then the_peer should not receive EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And the_peer2 should not receive EXTERNAL_COMMAND
			| command_type   | 89                         |
			| command_string | STOP_EXECUTING_HOST_CHECKS |
		And naemon status execute_host_checks should be set to 0
