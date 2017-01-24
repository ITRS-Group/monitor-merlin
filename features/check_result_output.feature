@config @daemons @merlin @queryhandler @livestatus
Feature: Running active checks locally which returns multi-line output
	should store the output within the object in naemon. Meaning that that first
	line of the output should be stored in the plugin_output parameter and the
	remaining lines in the long_plugin_output parameter.

	The same goes for when receiving a check result over merlin. Multi-line
	output in the received check result object should be stored in the same
	way it would be done locally which is first line in plugin_output and the
	rest in long_plugin_output.

	Background: Set up naemon configuration
		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		And I have naemon host objects
			| use          | host_name | address   | contacts  | max_check_attempts | hostgroups  | active_checks_enabled | check_interval | check_command     |
			| default-host | hostA     | 127.0.0.1 | myContact | 2                  | pollergroup |                     1 |              1 | check_long_output |
			| default-host | hostB     | 127.0.0.2 | myContact | 2                  | pollergroup |                     1 |              1 | check_long_output |
		And I have naemon service objects
			| use             | host_name | description | active_checks_enabled | check_interval | check_command     |
			| default-service | hostA     | PING        |                     1 |              1 | check_long_output |
			| default-service | hostB     | PONG        |                     1 |              1 | check_long_output |
		And I have naemon contact objects
			| use             | contact_name |
			| default-contact | myContact    |
		And I have naemon command objects
			| command_name | command_line |
			| echo_this    | asdf         |
		And I have naemon config interval_length set to 3
		And I have naemon config cached_host_check_horizon set to 0
		And I have naemon config cached_service_check_horizon set to 0

	Scenario: Processing an active service check result locally with a
		multi-line output should set plugin_output of the service to the first
		line and long_plugin_output to the remaining lines.

		Given I have naemon config execute_host_checks set to 1
		And I have naemon config execute_service_checks set to 1
		And I start naemon with merlin nodes connected
			| type   | name       | port |

		When I wait for 1 second

		Then plugin_output of service PONG on host hostB should be O K
		And long_plugin_output of service PONG on host hostB should be L\nO K
		And perf_data of service PONG on host hostB should be Perf. O K
		And plugin_output of service PING on host hostA should be O K
		And long_plugin_output of service PING on host hostA should be L\nO K
		And perf_data of service PING on host hostA should be Perf. O K

	Scenario: Receiving a service check result with a multi-line output should
		set plugin_output of the service to the first line and
		long_plugin_output to the remaining lines.

		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | the_peer   | 4001 |

		When the_peer sends event SERVICE_CHECK
			| state.current_state      | 0                      |
			| state.plugin_output      | O K\nL\nO K\|Perf. O K |
			| host_name                | hostA                  |
			| service_description      | PING                   |

		Then plugin_output of service PING on host hostA should be O K
		And long_plugin_output of service PING on host hostA should be L\nO K
		And perf_data of service PING on host hostA should be Perf. O K