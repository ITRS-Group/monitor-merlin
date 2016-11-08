@daemons @config @merlin @queryhandler
Feature: Notification persistence
    Notification events that are received by the merlin daemon should be
    persisted in a database.

    Scenario: Merlin daemon receives notification event
        Given I have merlin configured for port 7000
            | type   | name   | port | hostgroup    |
        And I start merlin

        When ipc connect to merlin at socket test_ipc.sock
        And ipc is connected to merlin
        And ipc sends event CONTACT_NOTIFICATION_METHOD
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |

        And I wait for 1 seconds
        Then CONTACT_NOTIFICATION_METHOD is logged in the database 1 time with data
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |
