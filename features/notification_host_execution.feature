@config @daemons @merlin @queryhandler
Feature: Notification execution for host notificaitons
	Notification scripts should only be executed on one node; the node that
	identifies the notification.

	In a peered environment, it should be the peer that is responsible for
	executing the script.

	In a poller environment, it is the node peered poller that is responsible
	for executing the plugin, if the pollers is configured in for notification.
	Otherwise, it is the master to the peer that has responsibility for the
	service/host given that the pollers wouldn't be there.

	In any case, the merlin packets for notifications is just informative, that
	should only be treated as information for logging to database. The only
	packets that can affect nofification commands to be executed is the packets
	regarding check results, which triggers the check result handling of naemon
	itself.

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

	Scenario: One master notifies if poller doesn't notify, given merlin HOST_STATUS events is received
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | no       |
			| peer   | the_peer   | 4002 | ignore      | ignore   |

		When the_poller sends event HOST_STATUS
			| name                  | hostA |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_STATUS
			| name                  | hostB |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_STATUS
			| name                  | hostA |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 2     |

		And the_poller sends event HOST_STATUS
			| name                  | hostB |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 2     |

		And I wait for 1 second

		Then file checks.log has 1 line matching ^notif host (hostA|hostB)$

	Scenario: No masters notifies if poller notifies, given merlin HOST_STATUS events is received
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | yes      |
			| peer   | the_peer   | 4002 | ignore      | ignore   |

		When the_poller sends event HOST_STATUS
			| name                  | hostA |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_STATUS
			| name                  | hostB |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_STATUS
			| name                  | hostA |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 2     |

		And the_poller sends event HOST_STATUS
			| name                  | hostB |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 2     |

		And I wait for 1 second

		Then file checks.log has 0 line matching ^notif host (hostA|hostB)$

	Scenario: Poller should notify if poller is configured to notify
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;0;First OK
		# Passive checks goes hard directly
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Not OK

		And I wait for 1 second
		Then file checks.log matches ^notif host hostA$
		And my_master received event CONTACT_NOTIFICATION_METHOD
			| host_name    | hostA     |
			| contact_name | myContact |

	Scenario: Passive check result should only be executed on machine handling the check, when getting from QH/command pipe
		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  |
			| peer   | the_peer    | 4001 | ignore     |
			| poller | the_poller  | 4002 | emptygroup |

		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;0;First OK
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostB;0;First OK
		# Passive checks goes hard directly
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Not OK
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostB;1;Not OK

		When the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;0;First OK          |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;0;First OK          |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Not OK            |
		And the_peer received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;1;Not OK            |

		And the_poller should not receive EXTERNAL_COMMAND

		Then file checks.log has 1 line matching ^notif host (hostA|hostB)$


	Scenario: Passive check result should only be executed on machine handling the check, when getting from merlin
		Given I start naemon with merlin nodes connected
			| type   | name        | port | hostgroup  |
			| peer   | the_peer    | 4001 | ignore     |
			| poller | the_poller  | 4002 | emptygroup |

		When the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;0;First OK          |
		And the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;0;First OK          |
		And the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Not OK            |
		And the_peer sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;1;Not OK            |

		# Next line waits
		Then the_peer should not receive EXTERNAL_COMMAND
		And the_poller should not receive EXTERNAL_COMMAND

		And file checks.log has 1 line matching ^notif host (hostA|hostB)$
		
	Scenario: Passive checks should be executed on poller if poller handling the check
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   |
			| poller | the_poller | 4001 | pollergroup |
			
		
		When I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;0;First OK
		# Passive checks goes hard directly
		And I send naemon command PROCESS_HOST_CHECK_RESULT;hostA;1;Not OK

		And I wait for 1 second

		When the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;0;First OK          |
		And the_poller received event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Not OK            |

		And file checks.log does not match ^notif host (hostA|hostB)$
		
	Scenario: Passive checks sent from master handled by correct node in pollergroup
		Given I start naemon with merlin nodes connected
			| type   | name         | port |
			| master | the_master   | 4001 |
			| peer   | other_poller | 4002 |

		When the_master sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;0;First OK          |
		And the_master sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;0;First OK          |
		And the_master sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostA;1;Not OK            |
		And the_master sends event EXTERNAL_COMMAND
			| command_type   | 87                        |
			| command_string | PROCESS_HOST_CHECK_RESULT |
			| command_args   | hostB;1;Not OK            |

		Then the_master should not receive EXTERNAL_COMMAND
		And other_poller should not receive EXTERNAL_COMMAND

		And file checks.log has 1 line matching ^notif host (hostA|hostB)$