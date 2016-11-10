@config @daemons @merlin @queryhandler @livestatus
Feature: Passive check result
	Passive check results should be propagated as an external command, to all
	nodes, but only be handled by the node owning that particular object.

	Doing so, we can guarantee the integrity of the information to the same
	level as active checks, where one node owns the object, and that node is the
	node handling all updates. All other nodes just holds a replica of the
	information.

	For each of hosts and services, there are two scenarios. First, a node gets
	the a passive check result from query handler. If having one peer (two peers
	in a network), and having two hosts/service, only one of the hosts/services
	should be evaluated on that node, but both commands should be forwarded to
	the other peer.

	On the other peer, when receiving the commands, only the object handled by
	the current peer should have its command executed. No commands should be
	forwarded/passed back, but the check result generated as result of the
	command should be send out on the merlin network


	Background: Set up naemon configuration
		Given I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		And I have naemon host objects
			| use          | host_name | address   | max_check_attempts | hostgroups  |
			| default-host | hostA     | 127.0.0.1 | 2                  | pollergroup |
			| default-host | hostB     | 127.0.0.1 | 2                  | pollergroup |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | hostA     | PONG        |
			| default-service | hostB     | PONG        |

	Scenario: A node gets a passive host check result through naemon's query handler
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | the_peer   | 4001 |

		And I should have 0 hosts objects matching plugin_output = Fromtest

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Fromtest
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostB;1;Fromtest

		Then I should have 1 hosts object matching plugin_output = Fromtest

		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Fromtest          |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;1;Fromtest          |

		And the_peer received event HOST_STATUS
			| state.plugin_output | Fromtest |
			| state.current_state | 1        |

	Scenario: A node gets a passive host check result from a peer
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | the_peer   | 4001 |

		And I should have 0 hosts objects matching plugin_output = Fromtest

		When the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Fromtest          |
		And the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;1;Fromtest          |

		And I wait for 1 second

		Then I should have 1 hosts object matching plugin_output = Fromtest

		And the_peer received event HOST_STATUS
			| state.plugin_output | Fromtest |
			| state.current_state | 1        |
	
		And the_peer should not receive EXTERNAL_COMMAND

	Scenario: A node gets a passive service check result through naemon's query handler
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | the_peer   | 4001 |

		And I should have 0 services objects matching plugin_output = Fromtest

		When I send naemon command PROCESS_SERVICE_CHECK_RESULT;hostA;PONG;1;Fromtest
		And I send naemon command PROCESS_SERVICE_CHECK_RESULT;hostB;PONG;1;Fromtest

		Then I should have 1 services object matching plugin_output = Fromtest

		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 30                           |
			| command_string | PROCESS_SERVICE_CHECK_RESULT |
			| command_args   | hostA;PONG;1;Fromtest        |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 30                           |
			| command_string | PROCESS_SERVICE_CHECK_RESULT |
			| command_args   | hostB;PONG;1;Fromtest        |

		And the_peer received event SERVICE_STATUS
			| state.plugin_output | Fromtest |
			| state.current_state | 1        |
	
	Scenario: A node gets a passive service check result from a peer
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| peer   | the_peer   | 4001 |

		And I should have 0 services objects matching plugin_output = Fromtest

		When the_peer sends event EXTERNAL_COMMAND
			| command_type   | 30                           |
			| command_string | PROCESS_SERVICE_CHECK_RESULT |
			| command_args   | hostA;PONG;1;Fromtest        |
		And the_peer sends event EXTERNAL_COMMAND
			| command_type   | 30                           |
			| command_string | PROCESS_SERVICE_CHECK_RESULT |
			| command_args   | hostB;PONG;1;Fromtest        |

		And I wait for 1 second

		Then I should have 1 services object matching plugin_output = Fromtest

		And the_peer received event SERVICE_STATUS
			| state.plugin_output | Fromtest |
			| state.current_state | 1        |
	
		And the_peer should not receive EXTERNAL_COMMAND
