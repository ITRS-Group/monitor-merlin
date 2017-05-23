@config @daemons @merlin @queryhandler @livestatus
Feature: Schedule downtimes on hosts or services to avoid notifications during
    a given timespan

    Background: Set up naemon configuration
        Given I have naemon hostgroup objects
            | hostgroup_name | alias |
            | pollergroup    | PG    |
            | emptygroup     | EG    |
        And I have naemon host objects
            | use          | host_name | address   | max_check_attempts | hostgroups  |
            | default-host | hostA     | 127.0.0.1 | 3                  | pollergroup |
        And I have naemon service objects
            | use             | host_name | description |
            | default-service | hostA     | PONG        |

    Scenario: Scheduling a downtime locally
        Given I start naemon with merlin nodes connected
            | type   | name       | port | hostgroup  |
            | peer   | the_peer   | 4001 | ignore     |
            | poller | the_poller | 4002 | emptygroup |
        When I put host hostA in downtime for 10 seconds
        Then the_poller should not receive DOWNTIME
        And the_peer should not receive DOWNTIME
