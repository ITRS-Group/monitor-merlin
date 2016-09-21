@config @daemons @merlin @queryhandler
Feature: Notification handling
	Notifications is normally executed on the node triggering its notifications.
	But notifications should be logged on all other peers too, and possible
	masters. Thus all notofications must be forwarded over the network

	It is also possible to disable that notifications is executed on the local
	node (a poller), and thus, it should be executed on the master instead,
	given that the "notifies" parameter in the merlin.conf is correctly set up

	Background: Set up naemon configuration
		Given I have naemon host objects
			| use          | host_name | address   | contacts  |
			| default-host | something | 127.0.0.1 | myContact |
			| default-host | gurka     | 127.0.0.2 | myContact |
		And I have naemon service objects
			| use             | host_name | description |
			| default-service | something | PING        |
			| default-service | something | PONG        |
			| default-service | gurka     | PING        |
			| default-service | gurka     | PONG        |
		And I have naemon contact objects
			| use             | contact_name | host_notifications_enabled |
			| default-contact | myContact    | 1                          |

	Scenario: Notifications from local system propagates to daemon
		Given I have merlin configured for port 7000
			| type | name     | port |
		And the_daemon listens for merlin at socket test_ipc.sock

		Given I start naemon
		And I wait for 1 second

		Then the_daemon is connected to merlin
		And the_daemon received event CTRL_ACTIVE

		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment
		And I wait for 1 second
		Then file checks.log matches ^notif host gurka A little comment$
		And the_daemon received event CONTACT_NOTIFICATION_METHOD
			| ack_author   | testCase         |
			| ack_data     | A little comment |
			| contact_name | myContact        |


	Scenario: Notifications from local system propagates to peer
		Given I have merlin configured for port 7000
			| type | name     | port |
			| peer | the_peer | 4001 |

		Given I start naemon
		And I wait for 1 second
		And node the_peer have info hash config_hash at 3000
		And node the_peer have expected hash config_hash at 4000

		Given the_peer connect to merlin at port 7000 from port 11001
		And the_peer sends event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| config_hash        | config_hash |
			| last_cfg_change    |        4000 |
		Then the_peer received event CTRL_ACTIVE


		When I send naemon command SEND_CUSTOM_HOST_NOTIFICATION;gurka;4;testCase;A little comment
		And I wait for 1 second
		Then file checks.log matches ^notif host gurka A little comment$
		And the_peer received event CONTACT_NOTIFICATION_METHOD
			| ack_author   | testCase         |
			| ack_data     | A little comment |
			| contact_name | myContact        |


	Scenario: Notifications from peer propagates to daemon
		Given I have merlin configured for port 7000
			| type | name     | port |
			| peer | the_peer | 4001 |
		And the_daemon listens for merlin at socket test_ipc.sock

		Given I start naemon
		And I wait for 1 second
		And node the_peer have info hash config_hash at 3000
		And node the_peer have expected hash config_hash at 4000

		Given the_peer connect to merlin at port 7000 from port 11001
		And the_peer sends event CTRL_ACTIVE
			| configured_peers   |           1 |
			| configured_pollers |           0 |
			| config_hash        | config_hash |
			| last_cfg_change    |        4000 |
		Then the_peer received event CTRL_ACTIVE

		Then the_daemon is connected to merlin
		And the_daemon received event CTRL_ACTIVE

		When the_peer sends event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |
		Then the_daemon received event CONTACT_NOTIFICATION_METHOD
			| host_name    | gurka     |
			| contact_name | myContact |
			| ack_author   | someUser  |
			| ack_data     | MyMessage |

		When I wait for 1 second
		Then file checks.log does not match ^notif host gurka