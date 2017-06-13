@config @daemons @merlin @queryhandler @livestatus
Feature: Merlin splits the configuration so that pollers only gets the configuration they need

    Background: Set up naemon configuration
        Given I have naemon hostgroup objects
            | hostgroup_name | alias |
            | pollergroup    | PG    |
            | emptygroup     | EG    |
        And I have naemon host objects
            | use          | host_name | address   | contacts  | hostgroups  |
            | default-host | hostA     | 127.0.0.1 | myContact | pollergroup |
            | default-host | hostB     | 127.0.0.2 | myContact | pollergroup |
        And I have naemon service objects
            | use             | host_name | description |
            | default-service | hostA     | PING        |
            | default-service | hostA     | PONG        |
            | default-service | hostB     | PING        |
            | default-service | hostB     | PONG        |
        And I have naemon contact objects
            | use             | contact_name |
            | default-contact | myContact    |

    Scenario: Splitting a servicegroup should always result in an ordered member list in the poller configuration
        Given I have naemon servicegroup objects
            | servicegroup_name | alias | members                                     |
            | the_servicegroup  | TSG   | hostA,PING,hostA,PONG,hostB,PING,hostB,PONG |
        And I start naemon with merlin nodes connected
            | type   | name        | port | hostgroup   |
            | poller | poller_one  | 4001 | pollergroup |
            | poller | poller_two  | 4002 | pollergroup |
        Then file poller_one.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And file poller_two.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And files poller_one.cfg and poller_two.cfg are identical

    Scenario: Splitting a servicegroup should always result in an ordered member list in the poller configuration
        Given I have naemon servicegroup objects
            | servicegroup_name | alias | members                                     |
            | the_servicegroup  | TSG   | hostB,PONG,hostB,PING,hostA,PONG,hostA,PING |
        And I start naemon with merlin nodes connected
            | type   | name        | port | hostgroup   |
            | poller | poller_one  | 4001 | pollergroup |
            | poller | poller_two  | 4002 | pollergroup |
        Then file poller_one.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And file poller_two.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And files poller_one.cfg and poller_two.cfg are identical

    Scenario: Splitting a servicegroup should always result in an ordered member list in the poller configuration
        Given I have naemon servicegroup objects
            | servicegroup_name | alias | members                                     |
            | the_servicegroup  | TSG   | hostB,PING,hostA,PING,hostB,PONG,hostA,PONG |
        And I start naemon with merlin nodes connected
            | type   | name        | port | hostgroup   |
            | poller | poller_one  | 4001 | pollergroup |
            | poller | poller_two  | 4002 | pollergroup |
        Then file poller_one.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And file poller_two.cfg matches hostB,PONG,hostB,PING,hostA,PONG,hostA,PING
        And files poller_one.cfg and poller_two.cfg are identical