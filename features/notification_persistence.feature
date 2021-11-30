@daemons @config @merlin @queryhandler
Feature: Notification persistence
    Notification events that are received by the merlin daemon should be
    persisted in a database if available.

    @unstable @unreliable_el8
    Scenario: Merlin daemon receives one notification event
        Given I have merlin configured for port 7000
            | type   | name   | port | hostgroup    |
        And I start merlin
        And ipc connect to merlin at socket test_ipc.sock
        And ipc is connected to merlin

        When ipc sends event CONTACT_NOTIFICATION_METHOD
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |
        And I wait for 2 seconds

        Then CONTACT_NOTIFICATION_METHOD is logged in the database 1 time with data
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |

    @unstable @unreliable_el8
    Scenario: Merlin daemon doesn't overwrite notification event
        Given I have merlin configured for port 7000
            | type   | name   | port | hostgroup    |
        And I start merlin
        And ipc connect to merlin at socket test_ipc.sock
        And ipc is connected to merlin

        When ipc sends event CONTACT_NOTIFICATION_METHOD
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |
        And ipc sends event CONTACT_NOTIFICATION_METHOD
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |

        And I wait for 2 seconds
        Then CONTACT_NOTIFICATION_METHOD is logged in the database 2 times with data
            | ack_author   | testCase         |
            | ack_data     | A little comment |
            | contact_name | myContact        |

    @unreliable_el8
    Scenario: Merlin daemon receives no notification event
        Given I have merlin configured for port 7000
            | type   | name   | port | hostgroup    |
        And I start merlin

        Then CONTACT_NOTIFICATION_METHOD is logged in the database 0 times with data
            | | |
