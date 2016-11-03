@config @daemons @merlin @queryhandler
Feature: Merlin should put hosts in specified hostgroups on correct pollers
	In the merlin configuration, which host is available on which pollers is
	specified by which hostgroups the poller handles.
	
	Each poller is paired with a set of hostgroups. Usally only one is
	specified, but more is allowed as a comma separated list.
	
	Those scenarios relies on passive host check result commands is sent to the
	correct poller, thus, the poller should only receive commands regarding the
	poller.
	
	Background: Set up naemon configuration
		And I have naemon hostgroup objects
			| hostgroup_name | alias |
			| hgA            | hgA   |
			| hgB            | hgB   |
			| hgC            | hgC   |
		Given I have naemon host objects
			| use          | host_name | address   | hostgroups  |
			| default-host | hA        | 127.0.0.1 | hgA         |
			| default-host | hB        | 127.0.0.1 | hgB         |
			| default-host | hC        | 127.0.0.1 | hgC         |
	
	Scenario: A poller has only one group, no other commands should be received
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |
			| poller | the_poller | 4001 | hgA        |
		
		When I send naemon command PROCESS_HOST_CHECK_RESULT;hA;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hB;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hC;0;Yay
		
		Then the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hA;0;Yay                  |
		Then the_poller should not receive EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hB;0;Yay                  |
	
	Scenario: A poller has only two groups, without spaces
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |
			| poller | the_poller | 4001 | hgA,hgB    |
		
		When I send naemon command PROCESS_HOST_CHECK_RESULT;hA;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hB;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hC;0;Yay
		
		Then the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hA;0;Yay                  |
		Then the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hB;0;Yay                  |
		Then the_poller should not receive EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hC;0;Yay                  |
	
	Scenario: A poller has only two groups, with spaces in hostgroup string
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroups |
			| poller | the_poller | 4001 | hgA, hgB   |
		
		When I send naemon command PROCESS_HOST_CHECK_RESULT;hA;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hB;0;Yay
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hC;0;Yay
		
		Then the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hA;0;Yay                  |
		Then the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hB;0;Yay                  |
		Then the_poller should not receive EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hC;0;Yay                  |