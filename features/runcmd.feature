@config @daemons @merlin @queryhandler @livestatus
Feature: Test merlins runcmd feature.

	Background: Set up naemon configuration

		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		And I have naemon host objects
			| use          | host_name | address   | contacts  | max_check_attempts | hostgroups  |
			| default-host | hostA     | 127.0.0.1 | myContact | 2                  | pollergroup |
			| default-host | hostB     | 127.0.0.2 | myContact | 2                  | pollergroup |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | hostA     | PONG        |
			| default-service | hostB     | PONG        |
		And I have naemon contact objects
			| use             | contact_name |
			| default-contact | myContact    |


	Scenario: The peer sends a request to "ipc" to execute a RUNCMD command.
		The peer should recieve a response back.

		Given I start naemon with merlin nodes connected
			| type   | name        | port |
			| peer   | my_peer     | 4123 |
		And I wait for 1 second

		Then my_peer is connected to merlin
		When my_peer sends event RUNCMD_PACKET
			| sd      | 29		 							       |
			| content | command.name=test_command;command.command_line=echo OK;runcmd=test_command |
		And I wait for 4 second

		Then my_peer received event RUNCMD_PACKET contains
			| sd      | 29	      |
			| content | outstd=OK |
