@config @daemons @merlin @queryhandler
Feature: Notification propagation
	Notifications is normally executed on the node triggering its notifications.
	Notifications should be logged on all other peers too, and possible
	masters. Thus all notifications must be forwarded from the local node to the
	all peers and masters.

	It is also possible to disable that notifications is executed on the local
	node (a poller), and thus, it should be executed on the master instead,
	given that the "notifies" parameter in the merlin.conf is correctly set up

	What should be regulated though is where the notification script is
	executed. For pollers (without masters), it should always be executed on the
	local node. For pollers, it should be posslble to execute the notifiaction
	script on the master instead, given notification configuration. However,
	that should be identified by the notification logic of the master due to
	check results sent from pollers to masters, not the actual notifiaction
	message.

	Background: Set up naemon configuration
		And I have naemon hostgroup objects
			| hostgroup_name | alias |
			| pollergroup    | PG    |
			| emptygroup     | EG    |
		Given I have naemon host objects
			| use          | host_name | address   | contacts  | hostgroups  |
			| default-host | something | 127.0.0.1 | myContact | pollergroup |
			| default-host | gurka     | 127.0.0.2 | myContact | pollergroup |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | something | PONG        |
			| default-service | gurka     | PONG        |
		And I have naemon contact objects
			| use             | contact_name | host_notifications_enabled |
			| default-contact | myContact    | 1                          |

	Scenario: Notifications from local system propagates to daemon
		Given I start naemon with merlin nodes connected
			| type | name     | port |

		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment
		And I wait for 1 second
		Then file checks.log matches ^notif host gurka A little comment$
		And ipc received event CONTACT_NOTIFICATION_METHOD
			| ack_author   | testCase         |
			| ack_data     | A little comment |
			| contact_name | myContact        |


	Scenario: Notifications from local system propagates to peer
		Given I start naemon with merlin nodes connected
			| type | name     | port |
			| peer | the_peer | 4001 |

		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment
		And I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;something;4;testCase;A little comment
		And I wait for 1 second
		Then file checks.log has 1 line matching ^notif host (gurka|something) A little comment$
		And the_peer received event CONTACT_NOTIFICATION_METHOD
			| ack_author   | testCase         |
			| ack_data     | A little comment |
			| contact_name | myContact        |


	Scenario: Notifications from peer propagates to daemon
		Given I start naemon with merlin nodes connected
			| type | name     | port |
			| peer | the_peer | 4001 |

		When the_peer sends event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |
		Then ipc received event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |

		When I wait for 1 second
		Then file checks.log does not match ^notif host gurka

	Scenario: Notifications from local system propagates to master
		Given I start naemon with merlin nodes connected
			| type   | name       | port |
			| master | my_master  | 4001 |

		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment
		And I wait for 1 second
		Then file checks.log matches ^notif host gurka A little comment$
		And my_master received event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka            |
			| contact_name | myContact        |
			| ack_author   | testCase         |
			| ack_data     | A little comment |


	Scenario: Notifications from poller propagates to daemon, poller notifies
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | yes      |

		When the_poller sends event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |
		Then ipc received event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |

		When I wait for 1 second
		Then file checks.log does not match ^notif host gurka


	Scenario: Notifications from poller propagates to daemon, master notifies
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup   | notifies |
			| poller | the_poller | 4001 | pollergroup | no       |

		When the_poller sends event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |
		Then ipc received event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |

		# The notification should not be logged, since it's only the node
		# identifying the notifaciton that actually runs the script.
		# The master should identeify it by the check result messages
		When I wait for 1 second
		Then file checks.log does not match ^notif host gurka

	Scenario: Host notification from master should not be sent to poller not handling the object.
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup  | notifies |
			| poller | the_poller | 4001 | emptygroup | no       |
		And file checks.log does not match ^notif host gurka

		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment

		Then the_poller should not receive CONTACT_NOTIFICATION_METHOD
		And file checks.log matches ^notif host gurka

	Scenario: Service notification from master should not be sent to poller not handling the object.
		Given I start naemon with merlin nodes connected
			| type   | name       | port | hostgroup  | notifies |
			| poller | the_poller | 4001 | emptygroup | no       |
		And file checks.log does not match ^notif service gurka

		When I send naemon command SEND_CUSTOM_SVC_NOTIFICATION;gurka;PONG;4;testCase;A little comment

		Then the_poller should not receive CONTACT_NOTIFICATION_METHOD
		And file checks.log matches ^notif service gurka