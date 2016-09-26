@config @daemons @merlin @queryhandler
Feature: Notification execution
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
			| hostgroup_name | alias | members     |
			| pollergroup    | PG    | hostA,hostB |
		And I have naemon host objects
			| use          | host_name | address   | contacts  | max_check_attempts |
			| default-host | hostA     | 127.0.0.1 | myContact | 2                  |
			| default-host | hostB     | 127.0.0.2 | myContact | 2                  |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | hostA     | PONG        |
			| default-service | hostB     | PONG        |
		And I have naemon contact objects
			| use             | contact_name |
			| default-contact | myContact    |

	Scenario: One master notifies if poller doesn't notify
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | no       |
			| peer   | the_peer   | 4002 | ignore      | ignore   |

		When the_poller sends event HOST_CHECK
			| name                  | hostA |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostB |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostA |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostB |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And I wait for 1 second

		# Only one, but not the other, should notify. The other should be
		# notified by the other peer
		Then file checks.log has 1 line matching ^notif host (hostA|hostB)$

	Scenario: No masters notifies if poller notifies
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | yes      |
			| peer   | the_peer   | 4002 | ignore      | ignore   |

		When the_poller sends event HOST_CHECK
			| name                  | hostA |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostB |
			| state.state_type      | 0     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostA |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And the_poller sends event HOST_CHECK
			| name                  | hostB |
			| state.state_type      | 1     |
			| state.current_state   | 1     |
			| state.current_attempt | 1     |

		And I wait for 1 second

		# Only one, but not the other, should notify. The other should be
		# notified by the other peer
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
			| host_name    | gurka     |
			| contact_name | myContact |